# Cline Project Instructions

Use this file as the default policy for Cline sessions in this repository.

## Priorities

1. Main page performance is Priority 1.
2. Maintain compatibility across ESP8266 and ESP32.
3. Keep persistence and route behavior reliable.

## Required Workflow

1. Understand current behavior before editing.
2. Make the smallest safe change set.
3. Update/add tests for changed behavior.
4. Run required validation and ensure pass.
5. If any failure occurs, find and fix root cause.
6. Update documentation for behavior/workflow/test changes.

## Validation

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1

# Preferred wrappers
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```

## References

1. `docs/AI_ENGINE_MATRIX.md`
2. `docs/AI_COLLABORATION_RUNBOOK.md`
3. `.github/copilot-instructions.md`
4. `AGENTS.md`
