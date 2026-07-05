# Copilot Project Instructions

Apply these rules for all work in this repository.

## Priorities

1. Priority 1 is runtime performance of the main page (`/`, `data/index.html`, `data/script.js`, runtime endpoints used by it).
2. Keep behavior compatible across both firmware platforms: ESP8266 and ESP32.
3. Preserve reliability of persistent storage and route behavior; avoid race-prone write patterns.

## Required Workflow For Any Change

1. Understand current behavior before editing (routes, UI flow, and persistence effects).
2. Make the smallest safe change set.
3. Update or add tests to cover the changed behavior.
4. Run test suites and ensure they pass before finishing.
5. If any test fails, find the root cause and fix it; do not mask failures.
6. Update documentation for every behavior, API, workflow, or test change.

## Performance Rules

1. Treat extra allocations/copies in runtime and WebSocket paths as regressions unless justified.
2. Prefer compact payloads and avoid unnecessary JSON expansion in frequently called routes.
3. For ESP8266 paths, avoid large temporary documents and keep memory use bounded.
4. **ESP8266 WebSocket interrupt rule:** Never call `ws.textAll()`, `ws.text()`, or `client->text()` from inside an `AsyncWebSocket` event callback on ESP8266. These callbacks run in lwIP interrupt context (`ctx: sys`); `malloc` is not reentrant there and heap corruption results. Set a `volatile bool` pending flag instead and dispatch from `loop()` via `dispatchPendingNotifications()`. Hardware writes (`writeRelaysToShiftRegister`) may remain in the callback since they do not allocate heap.
5. **ESP32 WebSocket latency:** Set `TCP_NODELAY` per client at `WS_EVT_CONNECT` (`client->client()->setNoDelay(true)`) to prevent Nagle's algorithm from buffering small relay-state messages. WiFi modem sleep must remain disabled (`esp_wifi_set_ps(WIFI_PS_NONE)` in `initWiFi`).

## Compatibility Rules

1. Verify all logic paths for both `esp8266_serial` and `esp32_serial` builds.
2. Keep API contracts stable unless a versioned change is explicitly intended.
3. When changing storage schema or semantics, include migration/compatibility handling.
4. WebSocket notification paths are platform-divergent by design: ESP8266 uses deferred flag dispatch; ESP32 calls notification functions directly. Preserve this split when editing `web_runtime.cpp`.

## Device Update And Verification

Use these baseline commands:

```powershell
# Build
platformio run -e esp8266_serial
platformio run -e esp32_serial

# Upload firmware
platformio run -e esp8266_serial -t upload
platformio run -e esp32_serial -t upload

# Upload filesystem when required by web/data changes
platformio run -e esp8266_serial -t uploadfs
platformio run -e esp32_serial -t uploadfs

# Full test passes
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

Preferred wrappers for AI-driven sessions:

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```

## Quality Gate (Definition Of Done)

A task is complete only when:

1. Both firmware environments build.
2. Required device updates are performed for impacted targets.
3. Functional tests pass.
4. New/changed behavior is covered by tests.
5. Documentation is updated.
