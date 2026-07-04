import subprocess

Import("env")

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
