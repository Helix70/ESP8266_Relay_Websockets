# Build And Validation Commands

## Build

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
```

## Upload Firmware

```powershell
platformio run -e esp8266_serial -t upload
platformio run -e esp32_serial -t upload
```

## Upload Filesystem (when data/ changes)

```powershell
platformio run -e esp8266_serial -t uploadfs
platformio run -e esp32_serial -t uploadfs
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
