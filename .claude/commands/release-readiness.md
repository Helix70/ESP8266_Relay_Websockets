# Release Readiness Check

Use this workflow before release.

## Inputs

- ESP8266 IP
- ESP32 IP
- Changed areas
- Optional soak iterations

## Workflow

1. Build both platforms.
2. Update impacted devices (firmware + filesystem if needed).
3. Run Full functional validation.
4. Run Soak validation.
5. If any failures occur, perform root-cause fix and re-run checks.
6. Confirm docs and tests are updated.

## Required Commands

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
platformio run -e esp8266_serial -t upload
platformio run -e esp32_serial -t upload
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

## Output

- Build and deploy summary
- Test and soak outcomes
- Issues and root-cause status
- Final ship/hold recommendation
