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

## 5. Known Platform Constraints

### ESP8266 — WebSocket Sends From Interrupt Context

`AsyncWebSocket` callbacks on ESP8266 run in lwIP interrupt context (`ctx: sys`). Calling `ws.textAll()` or `client->text()` from this context calls `malloc`, which is not reentrant. Symptoms of violating this rule include:

- `rst cause:4` (software WDT reset) — `malloc` enters an infinite loop on a corrupted free-list
- Exception 9 (`LoadStoreAlignmentCause`) — corrupted heap block header dereferenced
- Crashes in unrelated subsystems (mDNS, etc.) — function pointers corrupted by heap overwrites

**Fix pattern:** Set `volatile bool` pending flags in the callback; call notify functions from `loop()` via `dispatchPendingNotifications()` in `src/main.cpp`. Hardware writes (`writeRelaysToShiftRegister`) are safe in the callback. Use `static char[]` (BSS) for JSON payload buffers, never `static String`.

### ESP32 — WebSocket Latency

On ESP32, callbacks run in a FreeRTOS task (safe for `malloc`). Known latency sources:

- **Nagle's algorithm:** lwIP buffers small TCP segments until an ACK arrives. Fixed by `client->client()->setNoDelay(true)` at `WS_EVT_CONNECT`.
- **WiFi modem sleep:** adds up to one DTIM interval per packet. Fixed by `esp_wifi_set_ps(WIFI_PS_NONE)` in `initWiFi()`.

Both fixes are in place. Do not remove them.

## 6. Root-Cause Troubleshooting Workflow

When failures occur:

1. Reproduce with smallest deterministic call sequence.
2. Capture failing route, status, payload, and affected platform.
3. Validate whether issue is firmware, data, harness, or environment.
4. Fix at root cause and avoid test-only workarounds.
5. Re-run Full and Soak to confirm stability.

## 7. Change Documentation Requirements

For any behavioral change, update at least one of:

1. `README.md` (public behavior/workflow)
2. `scripts/tests/README.md` (test process and expected outputs)
3. This runbook (AI process and standards)
