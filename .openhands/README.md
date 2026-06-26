# OpenHands Setup Guide

Use this guide when OpenHands is used for autonomous or semi-autonomous coding tasks in this repository.

## Required Policy Inputs

Provide these files as mandatory context:

1. `docs/AI_ENGINE_MATRIX.md`
2. `docs/AI_COLLABORATION_RUNBOOK.md`
3. `.github/copilot-instructions.md`
4. `AGENTS.md`
5. `CLAUDE.md`

## Task Execution Rules

1. Main page performance is Priority 1.
2. Preserve ESP8266 and ESP32 compatibility.
3. Update tests and docs whenever behavior changes.
4. Require passing validation before task completion.
5. Resolve failures by root cause.

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
