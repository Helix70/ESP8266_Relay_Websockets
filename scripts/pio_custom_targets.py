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
