# Firmware Development

## Device Interaction

1. Build both platforms when shared code changes.
2. Flash impacted devices after firmware changes.
3. Upload filesystem (`-t uploadfs`) when `data/` changes — do this separately from firmware upload.
4. After updates, verify `/`, `/api/templates`, `/api/boards`, `/netinfo`, and `/ws` return healthy responses.

## Platform Constraints

### ESP8266 — WebSocket Interrupt Safety

`AsyncWebSocket` callbacks run in lwIP interrupt context. Never call `ws.textAll()`, `ws.text()`, or `client->text()` from these callbacks — `malloc` is not reentrant there, causing silent heap corruption. Use `volatile bool` pending flags set in the callback, drained by `dispatchPendingNotifications()` in `loop()`. Use `static char[]` buffers for JSON payloads, never `String`.

### ESP32 — WebSocket Latency

Call `client->client()->setNoDelay(true)` at `WS_EVT_CONNECT` to disable Nagle buffering. Do not remove `esp_wifi_set_ps(WIFI_PS_NONE)` from `initWiFi()`. Both fixes are in place — preserve them.

## Debugging

1. Reproduce deterministically.
2. Capture evidence: status, body, logs, platform scope.
3. Identify root cause — not just symptoms.
4. Apply a durable fix with minimal blast radius.
5. Re-run validation to confirm resolution.
