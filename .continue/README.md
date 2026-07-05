# Continue Setup Guide

This folder is the project entrypoint for Continue users.

## Recommended Project Context Files

When working with Continue in this repository, always load and follow:

1. `docs/AI_ENGINE_MATRIX.md`
2. `.github/copilot-instructions.md`
3. `AGENTS.md`
4. `CLAUDE.md`
5. `docs/AI_COLLABORATION_RUNBOOK.md`

## Required Standards

1. Main page performance is Priority 1.
2. Keep compatibility across ESP8266 and ESP32.
3. Update tests when behavior changes.
4. Ensure full validation passes before completion.
5. If issues appear, find and fix root cause.
6. Update docs with meaningful behavior/workflow changes.

## Validation Commands

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

Preferred wrappers:

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```
