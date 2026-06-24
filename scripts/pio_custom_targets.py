Import("env")

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
