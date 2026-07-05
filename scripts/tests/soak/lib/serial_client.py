"""Serial console driver for the relay board's interactive commands.

Covers the four recognized commands (help/reboot/reset/wifi) as implemented in
src/serial_commands.cpp and src/serial_provision.cpp:
  - reset: requires "y"/"yes" confirmation, wipes all settings (NVS/EEPROM,
    not the filesystem), reboots. Device comes up with no WiFi credentials and
    drops into the SoftAP + HTTP provisioning portal (src/provisioning_portal.cpp)
    -- but critically, processSerialCommands() keeps running even while the
    device spams "Provisioning" every loop iteration, so...
  - wifi: also requires confirmation, then runs a *synchronous serial wizard*
    (src/serial_provision.cpp runSerialWiFiProvisioningWizard) that prompts for
    SSID (by scan index or free text), password, and DHCP-vs-static -- this
    works whether or not the device is currently in provisioning mode, so it's
    the way to get a board back onto the network after a "reset" without ever
    needing this machine to join the board's temporary AP.

A background reader thread timestamps every line so heap/page-nav diagnostic
lines (e.g. "[HeapDiag] ... after=1234") can later be correlated against HTTP
call windows for the benchmark report.
"""
import re
import threading
import time

import serial

CONNECTED_RE = re.compile(r"IP Address: http://(\d+\.\d+\.\d+\.\d+)")
STATUS_CONNECTED = "Status=WL_CONNECTED"
PROVISIONING_MARKERS = ("Provisioning AP started", "Provisioning\n", "Provisioning")
CONFIRM_PROMPT = "Confirm? [y/N]"

# "Available commands:" prints at the very START of setup(), before the
# multi-second (LittleFS mount + WiFi connect) blocking sequence that must
# finish before loop() -- and therefore processSerialCommands() -- ever
# starts running. Sending a command right after seeing that banner is
# premature: the text just sits unread in the UART buffer. Either of these
# markers only appears once setup() has actually finished and loop() has
# begun servicing serial input (WiFi connected -> about to enter the normal
# loop, or WiFi failed -> straight into the provisioning loop, which also
# services serial commands per src/main.cpp).
LOOP_STARTED_RE = re.compile(r"IP Address: http://|Provisioning")


class SerialTimeout(RuntimeError):
    pass


class SerialClient:
    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self._lines = []  # list of (timestamp, line) -- full history, for heap correlation
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None

    def open(self):
        # pyserial asserts DTR/RTS by default on open(), and this board's
        # auto-reset circuit (same one platformio.ini's monitor_dtr=0/
        # monitor_rts=0 settings exist to defeat for `pio device monitor`)
        # holds the chip in hardware reset the entire time DTR is asserted --
        # confirmed empirically: opening with default DTR/RTS produced ZERO
        # serial output at all, because the board never actually booted.
        # Constructing with the port unset and clearing dtr/rts BEFORE the
        # real open() avoids ever asserting them.
        self.ser = serial.Serial()
        self.ser.port = self.port
        self.ser.baudrate = self.baud
        self.ser.timeout = 0.2
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()
        return self

    def close(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    def _read_loop(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(4096)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").rstrip("\r")
                with self._lock:
                    self._lines.append((time.time(), text))

    def send_line(self, text):
        self.ser.write((text + "\n").encode("utf-8"))
        self.ser.flush()

    def lines_since(self, since_ts):
        with self._lock:
            return [(t, l) for t, l in self._lines if t >= since_ts]

    def all_lines(self):
        with self._lock:
            return list(self._lines)

    def wait_for(self, pattern, timeout=15, since_ts=None):
        """Block until a line matching `pattern` (str substring or compiled
        regex) appears, or raise SerialTimeout. Returns the matching line."""
        if since_ts is None:
            since_ts = time.time()
        deadline = time.time() + timeout
        is_regex = hasattr(pattern, "search")
        checked = 0
        while time.time() < deadline:
            with self._lock:
                candidates = self._lines[checked:]
                checked = len(self._lines)
            for _, line in candidates:
                if (is_regex and pattern.search(line)) or (not is_regex and pattern in line):
                    return line
            time.sleep(0.05)
        raise SerialTimeout(f"timed out waiting for {pattern!r} on {self.port}")

    def heap_near(self, timestamp, window_s=1.5):
        """Best-effort: find the nearest 'heap=' or 'Heap free:' figure in the
        log within window_s of the given wall-clock timestamp. Returns an int
        or None."""
        heap_re = re.compile(r"heap=(\d+)|Heap free:\s*(\d+)")
        best = None
        best_dt = None
        with self._lock:
            snapshot = list(self._lines)
        for t, line in snapshot:
            dt = abs(t - timestamp)
            if dt > window_s:
                continue
            m = heap_re.search(line)
            if not m:
                continue
            value = int(m.group(1) or m.group(2))
            if best_dt is None or dt < best_dt:
                best, best_dt = value, dt
        return best

    # ---- Composite command flows -------------------------------------

    def ensure_clean_state(self, timeout=20):
        """Trigger a real hardware reset via the RTS line (confirmed wired to
        RESET/EN on this board; DTR is wired to BOOT/GPIO0 and is already
        held inactive throughout this class's lifetime -- see open() -- so
        this reset boots normal firmware, not the UART bootloader), then wait
        for the post-boot banner printed unconditionally at the very start of
        setup() on every boot.

        This guarantees the device is sitting in its normal command loop
        before we send anything else -- not stuck at a dangling y/N confirm
        prompt, or mid-way through the wifi wizard's SSID/password/DHCP
        sub-prompts, left behind by a previous interrupted connection.
        Sending a text command like "reboot" to *try* to reach this same
        state is NOT reliable for this purpose: if a previous session left
        the device mid-wizard, "reboot" would just be consumed as literal
        input to whatever prompt it's sitting at (e.g. typed in as the WiFi
        password), never actually rebooting -- confirmed as a real failure
        mode. A genuine hardware reset has no such ambiguity: it unconditionally
        restarts execution from power-on-equivalent state, wiping any stuck
        readSerialLineBlocking() call or wizard state along with it."""
        since = time.time()
        self.ser.rts = True
        time.sleep(0.15)
        self.ser.rts = False
        self.wait_for("Available commands:", timeout=5, since_ts=since)
        self.wait_for(LOOP_STARTED_RE, timeout=timeout, since_ts=since)

    def run_help(self, timeout=5):
        """Send 'help', confirm the unconditional command list reprints (no
        confirmation prompt, no state change) -- exercises printSerialHelp()
        via the command dispatcher specifically, distinct from the identical
        text that also prints once automatically at every boot."""
        since = time.time()
        self.send_line("help")
        self.wait_for("Available commands:", timeout=timeout, since_ts=since)

    def run_reboot_command(self, timeout=20):
        """Send the serial 'reboot' text command itself (no confirmation
        required) and wait for the device to come back up -- distinct from
        ensure_clean_state()'s hardware RTS pulse, which never actually
        exercises this command's own code path in serial_commands.cpp."""
        since = time.time()
        self.send_line("reboot")
        self.wait_for(LOOP_STARTED_RE, timeout=timeout, since_ts=since)

    def run_reset(self, timeout=20):
        """Send 'reset', confirm, wait for the device to reboot and enter
        provisioning (since this always wipes WiFi credentials too)."""
        since = time.time()
        self.send_line("reset")
        self.wait_for(CONFIRM_PROMPT, timeout=5, since_ts=since)
        self.send_line("y")
        self.wait_for("Settings erased", timeout=timeout, since_ts=since)
        self.wait_for("Provisioning", timeout=timeout, since_ts=since)

    def run_wifi_wizard(self, ssid, password, use_dhcp=True, timeout=25):
        """Send 'wifi', confirm, and drive the serial provisioning wizard.
        Works whether the device is already in provisioning mode or not."""
        since = time.time()
        self.send_line("wifi")
        self.wait_for(CONFIRM_PROMPT, timeout=5, since_ts=since)
        self.send_line("y")
        self.wait_for("Enter SSID number", timeout=10, since_ts=since)
        self.send_line(ssid)
        self.wait_for("Enter Wi-Fi password", timeout=10, since_ts=since)
        self.send_line(password or "")
        self.wait_for("Use DHCP?", timeout=10, since_ts=since)
        self.send_line("Y" if use_dhcp else "n")
        if not use_dhcp:
            raise NotImplementedError("static IP entry not needed by this suite's reprovisioning flow")
        self.wait_for("Rebooting", timeout=timeout, since_ts=since)

    def wait_for_reconnect(self, timeout=30, since_ts=None):
        """After a reboot with valid credentials, wait for the normal
        CONNECTED/IP Address banner and return the new IP as a string."""
        if since_ts is None:
            since_ts = time.time()
        self.wait_for(STATUS_CONNECTED, timeout=timeout, since_ts=since_ts)
        line = self.wait_for(CONNECTED_RE, timeout=5, since_ts=since_ts)
        m = CONNECTED_RE.search(line)
        return m.group(1)
