# AI Collaboration Runbook

This runbook captures project expectations for AI-assisted engineering work.

## 1. Project Priorities

1. Main page performance is Priority 1.
2. Compatibility across ESP8266 and ESP32 is mandatory.
3. Reliability and determinism of storage/routes are required.

## 2. Required Standards

1. Small, focused changes with clear intent.
2. Preserve API compatibility unless intentionally changed.
3. Update tests for any behavior change.
4. Update documentation for every significant change.
5. Resolve failures by root cause, not by suppression.

## 3. Build, Update, And Device Interaction

### Build

```powershell
platformio run -e esp8266_serial
platformio run -e esp32_serial
```

### Upload Firmware

```powershell
platformio run -e esp8266_serial -t upload
platformio run -e esp32_serial -t upload
```

### Upload Filesystem (when `data/**` changes)

```powershell
platformio run -e esp8266_serial -t uploadfs
platformio run -e esp32_serial -t uploadfs
```

### Important ESP8266 Note

Prefer explicit firmware upload command (`-t upload`) for firmware updates. In this project context, combining targets can produce confusing behavior where filesystem upload is repeated.

### Device Health Checks

1. Confirm `/`, `/api/templates`, `/api/boards`, `/netinfo` return healthy responses.
2. Confirm template and board CRUD flows work on both devices.
3. Confirm `/ws` connect+home path returns expected state payload.

## 4. Test Execution And Quality Gates

### Preferred One-Command Wrappers

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```

### Baseline Full Functional

```powershell
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
```

### Stability (Soak)

```powershell
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

### Required Outcome

1. Full tests must pass.
2. Soak tests should have zero failures for release-readiness.
3. Reports must be captured and reviewed:
   - `scripts/tests/last-functional-report.json`
   - `scripts/tests/soak-report-full.json`

## 5. Root-Cause Troubleshooting Workflow

When failures occur:

1. Reproduce with smallest deterministic call sequence.
2. Capture failing route, status, payload, and affected platform.
3. Validate whether issue is firmware, data, harness, or environment.
4. Fix at root cause and avoid test-only workarounds.
5. Re-run Full and Soak to confirm stability.

## 6. Change Documentation Requirements

For any behavioral change, update at least one of:

1. `README.md` (public behavior/workflow)
2. `scripts/tests/README.md` (test process and expected outputs)
3. This runbook (AI process and standards)
