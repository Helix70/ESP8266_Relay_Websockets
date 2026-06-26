# AI Engine Matrix

This document keeps AI tool configuration aligned across engines.

## Canonical Standards

Treat these as the source of truth:

1. `docs/AI_COLLABORATION_RUNBOOK.md`
2. `.github/copilot-instructions.md`
3. `AGENTS.md`
4. `scripts/ai/README.md`

## Tool-Specific Entry Files

1. Copilot:
   - `.github/copilot-instructions.md`
   - `.github/instructions/firmware-workflow.instructions.md`
   - `.github/prompts/*.prompt.md`

2. Claude:
   - `CLAUDE.md`
   - `.claude/README.md`
   - `.claude/commands/*.md`

3. Cursor:
   - `.cursor/rules/project-standards.mdc`

4. Aider:
   - `.aider.conf.yml`

5. Continue:
   - `.continue/README.md`

6. Cline:
   - `CLINE.md`

7. OpenHands:
   - `.openhands/README.md`

## Universal Quality Gates

1. Main page performance is Priority 1.
2. Compatibility across ESP8266 and ESP32 is mandatory.
3. Update tests for behavior changes.
4. Update documentation for meaningful changes.
5. Ensure full tests pass.
6. If failures occur, find and fix root cause.

## Universal Validation Commands

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

## One-Command Wrappers

Use these wrappers to keep AI engine execution consistent:

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```
