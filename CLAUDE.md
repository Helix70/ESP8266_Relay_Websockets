# Claude Project Instructions

This file defines project expectations for Claude Code sessions in this repository.

## Priorities

1. Main page performance is Priority 1.
2. Maintain compatibility across ESP8266 and ESP32.
3. Keep persistence and route behavior reliable and deterministic.

## Required Engineering Workflow

1. Understand current behavior before changing code.
2. Make the smallest safe change set.
3. Update/add tests for any behavior change.
4. Run required validation and ensure tests pass.
5. If tests fail, find and fix root cause (do not mask failures).
6. Update documentation for behavior, API, workflow, or test changes.

## Build And Validation Commands

```powershell
# Build
platformio run -e esp8266_serial
platformio run -e esp32_serial

# Firmware upload
platformio run -e esp8266_serial -t upload
platformio run -e esp32_serial -t upload

# Filesystem upload when data/ changes
platformio run -e esp8266_serial -t uploadfs
platformio run -e esp32_serial -t uploadfs

# Full validation
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1

# Preferred wrappers
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```

## Device Interaction Standards

1. Build both platforms when shared code changes.
2. Flash impacted devices.
3. Upload filesystem when web assets or static files change.
4. Verify health endpoints and route behavior after updates.

## Root-Cause Requirement

If any issue appears in tests, runtime, or deployment:

1. Reproduce deterministically.
2. Capture evidence (status/body/logs, platform scope).
3. Identify root cause.
4. Apply a durable fix.
5. Re-run validation to prove resolution.

## Definition Of Done

A task is complete only when:

1. ESP8266 and ESP32 builds pass.
2. Required device updates are completed.
3. Functional tests pass.
4. Documentation is updated.
