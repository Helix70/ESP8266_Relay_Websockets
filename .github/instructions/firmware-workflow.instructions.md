---
applyTo: "src/**,data/**,scripts/tests/**,platformio.ini,README.md"
description: "Use when editing firmware, web UI, tests, or docs in this project; enforce performance-first main page changes, ESP8266/ESP32 compatibility, test updates, documentation updates, and root-cause-driven fixes."
---

# Firmware Workflow Instructions

## Mandatory Priorities

1. Main page runtime performance is the highest priority.
2. Maintain platform compatibility across ESP8266 and ESP32.
3. Keep storage and route behavior deterministic and reliable.

## Mandatory Process

1. For behavior changes, update tests first or in the same change.
2. Run relevant functional tests and report outcomes.
3. If tests fail, continue until root cause is found and fixed.
4. Update documentation in the same change set.

## Validation Requirements

Always validate with:

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
```

When touching persistence, CRUD routes, or concurrency-sensitive behavior, also run:

```powershell
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

## Completion Checklist

1. Builds pass for both platforms.
2. Tests pass.
3. Docs updated.
4. Any failures were root-caused and fixed.
