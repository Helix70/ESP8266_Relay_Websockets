import os
import socket
import subprocess
import time

Import("env")

def is_host_up(host, port=80, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False

def wait_for_device_reboot(host, max_wait_seconds):
    print("Waiting for {} to come back online (web server on port 80, up to {}s)...".format(host, max_wait_seconds))
    # Grace period so we don't immediately succeed against the old firmware's
    # still-open socket before it actually restarts.
    time.sleep(2)
    deadline = time.time() + max_wait_seconds
    while time.time() < deadline:
        if is_host_up(host):
            print("{} is back online.".format(host))
            return
        time.sleep(1)
    print("Timed out after {}s waiting for {}; proceeding anyway.".format(max_wait_seconds, host))

def get_firmware_version():
    try:
        commit_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL,
            cwd=env["PROJECT_DIR"],
        ).decode().strip()
        commit_date = subprocess.check_output(
            ["git", "log", "-1", "--format=%cd", "--date=format:%Y-%m-%d"],
            stderr=subprocess.DEVNULL,
            cwd=env["PROJECT_DIR"],
        ).decode().strip()
        dirty_status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            stderr=subprocess.DEVNULL,
            cwd=env["PROJECT_DIR"],
        ).decode().strip()
        suffix = "-dirty" if dirty_status else ""
        return "{}{} ({})".format(commit_hash, suffix, commit_date)
    except Exception:
        return "unknown"

env.Append(CPPDEFINES=[
    ("FIRMWARE_VERSION", '\\"{}\\"'.format(get_firmware_version()))
])

def upload_firmware_and_fs(source, target, env):
    result = env.Execute('"$PYTHONEXE" -m platformio run -e $PIOENV -t upload')
    if result != 0:
        env.Exit(result)

    # Serial uploads don't reboot onto a network the next step depends on —
    # uploadfs just reuses the same COM port, so no wait is needed there.
    # OTA uploads do reboot the device onto WiFi, so poll for it to actually
    # come back (web server on port 80) instead of guessing a fixed delay.
    if "ota" in env["PIOENV"]:
        max_wait_seconds = 15 if "esp8266" in env["PIOENV"] else 10
        wait_for_device_reboot(env["UPLOAD_PORT"], max_wait_seconds)

    result = env.Execute('"$PYTHONEXE" -m platformio run -e $PIOENV -t uploadfs')
    if result != 0:
        env.Exit(result)

env.AddCustomTarget(
    name="upload_all",
    dependencies=None,
    actions=[upload_firmware_and_fs],
    title="Upload Firmware + Filesystem",
    description="Build and upload firmware and LittleFS image",
)

# Comprehensive hardware soak/functional test suite (scripts/tests/soak/).
# Registered only on the 3 OTA environments that map to a real physical
# board+COM-port pairing (see platformio.ini) -- these scripts drive both the
# board's serial console (reset/wifi wizard) and its HTTP/WS API, so they need
# to know which COM port and relay count go with which OTA environment.
SOAK_TEST_SCRIPTS = {
    "esp8266_ota_16relay": "esp8266_16relay_soak.py",
    "esp8266_ota_8relay": "esp8266_8relay_soak.py",
    "esp32_ota_8relay": "esp32_8relay_soak.py",
}
LONG_SOAK_TEST_SCRIPTS = {
    "esp8266_ota_16relay": "esp8266_16relay_long_soak.py",
    "esp8266_ota_8relay": "esp8266_8relay_long_soak.py",
    "esp32_ota_8relay": "esp32_8relay_long_soak.py",
}

def _run_script(env, filename):
    script_path = os.path.join(env["PROJECT_DIR"], "scripts", "tests", "soak", filename)
    result = env.Execute('"$PYTHONEXE" "{}"'.format(script_path))
    if result != 0:
        env.Exit(result)

def run_soak_test(source, target, env):
    _run_script(env, SOAK_TEST_SCRIPTS[env["PIOENV"]])

def run_all_soak_tests(source, target, env):
    _run_script(env, "run_all_soak.py")

def run_long_soak_test(source, target, env):
    _run_script(env, LONG_SOAK_TEST_SCRIPTS[env["PIOENV"]])

def run_all_long_soak_tests(source, target, env):
    _run_script(env, "run_all_long_soak.py")

if env["PIOENV"] in SOAK_TEST_SCRIPTS:
    env.AddCustomTarget(
        name="soaktest",
        dependencies=None,
        actions=[run_soak_test],
        title="Run Soak Test",
        description="Comprehensive hardware soak/functional test for this board (resets, reprovisions WiFi, exercises the full API, then soaks all relays for 60s+)",
    )
    env.AddCustomTarget(
        name="soaktest_all",
        dependencies=None,
        actions=[run_all_soak_tests],
        title="Run All Soak Tests",
        description="Run the soak test for all 3 boards sequentially (COM6, COM4, COM3)",
    )
    env.AddCustomTarget(
        name="soaktest_long",
        dependencies=None,
        actions=[run_long_soak_test],
        title="Run Long Soak Test",
        description="Same as Run Soak Test, but the relay-combination phase runs for hours (default 4h, SOAK_LONG_DURATION_S to override) -- catches slow heap fragmentation a short burst can hide",
    )
    env.AddCustomTarget(
        name="soaktest_long_all",
        dependencies=None,
        actions=[run_all_long_soak_tests],
        title="Run All Long Soak Tests",
        description="Run the long soak test for all 3 boards sequentially (default 4h each, 12h total)",
    )
