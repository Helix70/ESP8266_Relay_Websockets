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

`ws.cleanupClients()` in `loop()` (`main.cpp`) must stay rate-limited (currently every 250ms via a `millis()` gate), not called unconditionally every pass. It takes the same `std::recursive_mutex` (`_ws_clients_lock` in ESPAsyncWebServer) that `WS_EVT_DATA` dispatch and `textAll()`/`text()` broadcasts use on the AsyncTCP task. Calling it on every `loop()` iteration re-acquires that lock hundreds of thousands of times a second, starving the AsyncTCP task of it and causing intermittent relay-toggle latency — this is what a stray `delayMicroseconds(100)` in `loop()` was previously (accidentally) masking on ESP32.

This rate-limit is ESP32-only. Do not remove the `#ifdef ESP8266` `delayMicroseconds(100)` at the end of `loop()` — ESP8266 never had the lock-contention problem (its WS sends are already deferred out of interrupt context) and removing that delay was tried once, coinciding with an OOM crash in `ArduinoOTA`'s `MDNSResponder`. The crash site was unrelated code, so causation was never confirmed, but there is no upside to changing ESP8266's loop cadence, so leave it untouched.

### ArduinoJson v7: `JsonDocument::clear()` frees its pool — it does not reset-and-reuse

Verified in the vendored source (`ArduinoJson/Memory/MemoryPool.hpp::destroy()` calls `allocator->deallocate()`, invoked by `JsonDocument::clear()` via `ResourceManager::clear()`): calling `.clear()` on a `JsonDocument` — even a `static` one — genuinely frees its internal memory pool back to the heap. It is not a cheap position-reset like ArduinoJson v6's `DynamicJsonDocument`. A `static JsonDocument` that calls `.clear()` every invocation gets no allocation-churn benefit over a local one; both malloc-and-grow from scratch on every call.

To actually avoid per-call heap churn on a hot path: build the structure (array/object slots) once, cache the `JsonObject`/`JsonArray` handles in `static` variables, and on subsequent calls overwrite existing member values in place *without* calling `clear()`. This is safe and zero-allocation for numeric/boolean fields (`ConverterImpl.hpp`'s integer/bool converters call `setInteger()`/`setBoolean()` directly, no pool interaction) — see `notifyRelayStates()`/`notifyRelayState()` in `web_runtime.cpp` for the pattern. It is **not** zero-allocation for string fields: every string assignment goes through `VariantData::clear()` (dereferencing/freeing the old owned string) before `setString()` stores the new one, so a field holding a `String` still frees+reallocates on every call even when the value is unchanged — worth avoiding only if that field is on a genuinely hot path (diff against a cached previous value and skip the assignment when unchanged), not worth the complexity otherwise.

### Static assets are served with `Cache-Control: no-store` — no `?v=` query strings

`server.serveStatic("/", LittleFS, "/")` in `web_runtime.cpp` sets `.setCacheControl("no-store")` deliberately. Do not remove it and do not reintroduce hand-stamped `?v=YYYYMMDDx` cache-busting query strings in the HTML `<link>`/`<script>` tags — that scheme existed before and was fragile (17 references across 6 files to keep in sync by hand; forgotten bumps shipped stale CSS/JS more than once).

Do not "fix" this by relying on `ESPAsyncWebServer`'s default ETag behavior instead (i.e. removing `setCacheControl` entirely). Without an explicit `Cache-Control`, the library auto-generates an ETag from `file.getLastWrite()`, falling back to **file size** when no timestamp is available. `mklittlefs` (the tool `-t uploadfs` uses) has no option to preserve source mtimes into the LittleFS image, so that fallback is what actually happens here — meaning two edits of the same file that happen to produce identical byte counts would incorrectly get a `304 Not Modified` and serve stale content, silently. `no-store` trades away browser caching of these small static files for guaranteed-fresh content with zero maintenance burden.

A firmware-embedded version constant (e.g. `__DATE__`/`__TIME__`, a build hash) was also considered and rejected: firmware and filesystem are uploaded separately in this project's workflow (see above), so a firmware-compiled version string would not change on a `data/`-only `uploadfs`, defeating the purpose.

### mDNS is intentionally disabled

`ArduinoOTA.begin()` in `main.cpp` explicitly disables mDNS (`begin(false)` on ESP8266, `setMdnsEnabled(false)` on ESP32). Root cause: with mDNS on, the board parses every multicast mDNS packet from every device on the LAN, and ESP8266's `MDNSResponder` allocates per resource-record with no ceiling — a single busy packet from an unrelated device exhausted the ~16KB heap margin in one pass and crashed with OOM (heap logging showed a healthy, stable ~16KB free right up until a two-step collapse: ~16KB → ~11KB → ~700B within 4 seconds). OTA doesn't need mDNS here: `platformio.ini` always targets a fixed `upload_port` IP, and the OTA transfer itself runs over its own dedicated UDP port independent of mDNS. Do not re-enable mDNS without also addressing the unbounded-allocation parser, and confirm first whether anything has come to depend on `<hostname>.local` access (as of this writing, nothing in this codebase does — access is IP-only).

`ArduinoOTA.setHostname()` / `OTA_HOSTNAME` was removed along with mDNS — verified in the `ArduinoOTA` library source that `_hostname` is used *only* for `MDNS.begin(hostname)` and a debug log gated behind `#ifdef OTA_DEBUG` (never defined in this project), so with mDNS off it had no remaining effect on either platform.

## Debugging

1. Reproduce deterministically.
2. Capture evidence: status, body, logs, platform scope.
3. Identify root cause — not just symptoms.
4. Apply a durable fix with minimal blast radius.
5. Re-run validation to confirm resolution.
