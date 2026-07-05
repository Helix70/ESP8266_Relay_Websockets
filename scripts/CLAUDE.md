# Build And Validation Commands

## Environments

Three physical boards, each with a serial (COM port) and OTA (IP) environment pair:

| Board | Serial env | OTA env |
|---|---|---|
| ESP8266 8-relay ("Western Tower") | `esp8266_serial_8relay` (COM4) | `esp8266_ota_8relay` (192.168.2.217) |
| ESP8266 16-relay | `esp8266_serial_16relay` (COM6) | `esp8266_ota_16relay` (192.168.2.154) |
| ESP32 8-relay ("Relay Board") | `esp32_serial_8relay` (COM3) | `esp32_ota_8relay` (192.168.2.195) |

There is no `esp32_*_16relay` environment — the ESP32 board only exists in the 8-relay variant.

## Build

```powershell
platformio run -e esp8266_serial_16relay
platformio run -e esp32_serial_8relay
```

## Upload Firmware

```powershell
platformio run -e esp8266_serial_16relay -t upload
platformio run -e esp32_serial_8relay -t upload
```

## Upload Filesystem (when data/ changes)

```powershell
platformio run -e esp8266_serial_16relay -t uploadfs
platformio run -e esp32_serial_8relay -t uploadfs
```

## Validation

```powershell
# Preferred wrappers
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem

# Direct
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

These are quick HTTP-only smoke/soak checks (`Invoke-BoardFunctionalHarness.ps1` + wrappers). They don't touch the serial console and don't require physical access — good for a fast sanity check against boards already on the network.

## Comprehensive hardware soak test (scripts/tests/soak/)

A separate, deeper Python suite that drives BOTH the serial console (`reset`/`wifi` commands, WiFi re-provisioning) and the full HTTP/WS API, then runs a real ≥60s combinatorial relay soak with latency/heap benchmarking. Requires physical USB access to the board (COM port) — this is not a CI-style check, it's a hardware-in-the-loop test that will physically actuate every relay on whatever board it's pointed at.

```powershell
pip install -r scripts/tests/soak/requirements.txt

# One board at a time
python scripts/tests/soak/esp8266_16relay_soak.py
python scripts/tests/soak/esp8266_8relay_soak.py
python scripts/tests/soak/esp32_8relay_soak.py

# All three, sequentially
python scripts/tests/soak/run_all_soak.py
```

Or via PlatformIO's Tasks sidebar (VSCode): each of the three OTA environments (`esp8266_ota_16relay`, `esp8266_ota_8relay`, `esp32_ota_8relay`) has a "Run Soak Test" (this board only) and "Run All Soak Tests" (all three) custom target.

Requires an explicit typed `YES` confirmation before touching any relay (set `SOAK_SKIP_CONFIRM=1` to skip when iterating on fixes in the same session). WiFi credentials for re-provisioning come from `SOAK_WIFI_SSID`/`SOAK_WIFI_PASSWORD` env vars, or an interactive prompt if unset. Reports (pass/fail + latency/heap benchmarks + estimated behavioral coverage + template/board-edit specifics) are written to `scripts/tests/soak/reports/<board>/<timestamp>.json` (gitignored).

### Long-duration soak

A separate, longer-running variant of the same suite: identical reset/reprovision/functional-coverage flow, but the relay-combination phase runs for hours instead of ~60s (default 4h, override with `SOAK_LONG_DURATION_S`), with page navigation, theme changes, and progress checkpoints firing periodically throughout the run instead of once near the start -- built to surface slow heap fragmentation a short burst can hide.

```powershell
# One board at a time
python scripts/tests/soak/esp8266_16relay_long_soak.py
python scripts/tests/soak/esp8266_8relay_long_soak.py
python scripts/tests/soak/esp32_8relay_long_soak.py

# All three, sequentially (default 4h each, 12h total)
python scripts/tests/soak/run_all_long_soak.py

# Quick check that the long-soak entry point itself works, without committing to a full run
SOAK_LONG_DURATION_S=120 python scripts/tests/soak/esp8266_16relay_long_soak.py
```

Or via PlatformIO's Tasks sidebar: "Run Long Soak Test" (this board only) and "Run All Long Soak Tests" (all three), on the same three OTA environments.
