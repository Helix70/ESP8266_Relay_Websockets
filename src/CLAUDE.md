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

### `sharedNotifyPayload` — one shared static buffer for all outgoing JSON responses

`notifyClients()`, `notifyRelayStates()`, `notifyRelayState()`, `notifyClient()` (via `buildRuntimeStatePayload()`), and the `/netinfo` handler in `web_runtime.cpp` all write their final serialized JSON into `sharedNotifyPayload` (a single file-scope `static char[3584]`) instead of each having their own dedicated buffer. This is safe because none of them ever run concurrently: on ESP8266 they're called sequentially from `dispatchPendingNotifications()` in `loop()`; on both platforms, within a single WS/HTTP callback invocation there is no preemption — each function fully builds, serializes, and hands off to `ws.textAll()`/`client->text()` (which copy the buffer into their own internal send buffer before returning) before the next one can start. Consolidating five separate buffers (1024+3584+1024+128+1024 = 6784 bytes) into one sized to the largest need reclaimed ~3.2KB of static RAM on both platforms (confirmed via `nm --size-sort` on the ELF).

**If you add a new function that builds a JSON response this way**, reuse `sharedNotifyPayload` rather than declaring a new static buffer — but only if it fits the same non-reentrancy argument above (never called from inside another function that's also mid-use of the buffer, e.g. don't call one of these from inside another one of these).

### Static assets are served with `Cache-Control: no-store` — no `?v=` query strings

`server.serveStatic("/", LittleFS, "/")` in `web_runtime.cpp` sets `.setCacheControl("no-store")` deliberately. Do not remove it and do not reintroduce hand-stamped `?v=YYYYMMDDx` cache-busting query strings in the HTML `<link>`/`<script>` tags — that scheme existed before and was fragile (17 references across 6 files to keep in sync by hand; forgotten bumps shipped stale CSS/JS more than once).

Do not "fix" this by relying on `ESPAsyncWebServer`'s default ETag behavior instead (i.e. removing `setCacheControl` entirely). Without an explicit `Cache-Control`, the library auto-generates an ETag from `file.getLastWrite()`, falling back to **file size** when no timestamp is available. `mklittlefs` (the tool `-t uploadfs` uses) has no option to preserve source mtimes into the LittleFS image, so that fallback is what actually happens here — meaning two edits of the same file that happen to produce identical byte counts would incorrectly get a `304 Not Modified` and serve stale content, silently. `no-store` trades away browser caching of these small static files for guaranteed-fresh content with zero maintenance burden.

A firmware-embedded version constant (e.g. `__DATE__`/`__TIME__`, a build hash) was also considered and rejected: firmware and filesystem are uploaded separately in this project's workflow (see above), so a firmware-compiled version string would not change on a `data/`-only `uploadfs`, defeating the purpose.

### mDNS is intentionally disabled

`ArduinoOTA.begin()` in `main.cpp` explicitly disables mDNS (`begin(false)` on ESP8266, `setMdnsEnabled(false)` on ESP32). Root cause: with mDNS on, the board parses every multicast mDNS packet from every device on the LAN, and ESP8266's `MDNSResponder` allocates per resource-record with no ceiling — a single busy packet from an unrelated device exhausted the ~16KB heap margin in one pass and crashed with OOM (heap logging showed a healthy, stable ~16KB free right up until a two-step collapse: ~16KB → ~11KB → ~700B within 4 seconds). OTA doesn't need mDNS here: `platformio.ini` always targets a fixed `upload_port` IP, and the OTA transfer itself runs over its own dedicated UDP port independent of mDNS. Do not re-enable mDNS without also addressing the unbounded-allocation parser, and confirm first whether anything has come to depend on `<hostname>.local` access (as of this writing, nothing in this codebase does — access is IP-only).

`ArduinoOTA.setHostname()` / `OTA_HOSTNAME` was removed along with mDNS — verified in the `ArduinoOTA` library source that `_hostname` is used *only* for `MDNS.begin(hostname)` and a debug log gated behind `#ifdef OTA_DEBUG` (never defined in this project), so with mDNS off it had no remaining effect on either platform.

### HTTP routes must check `otaInProgress` before touching LittleFS

`ArduinoOTA.onStart()` (`main.cpp`) calls `LittleFS.end()` and quiesces the WebSocket layer, but the plain `AsyncWebServer` keeps accepting and dispatching HTTP requests during the OTA flash write — it was never paused. A browser tab left open during a firmware upload got routed to a LittleFS-backed page mid-flash and dereferenced the torn-down filesystem, crashing with Exception 28 (confirmed via a decoded crash dump: fault was inside `AsyncWebServerResponse::addHeader` called from the `/` route handler, mid-`Progress: 10%`).

Fix: a `volatile bool otaInProgress` (`app_state.h/cpp`), set `true` at the very start of `onStart` (before `LittleFS.end()`) and reset `false` in both `onEnd` and `onError`. `onError` also re-mounts LittleFS (`initLittleFS()`) and re-enables WS (`ws.enable(true)`) — unlike `onEnd` (success, device reboots immediately after), the device keeps running after a failed/aborted OTA, so leaving the filesystem unmounted would strand it. All 5 HTML page routes (`/`, `/config.html`, `/relay-config.html`, `/boards.html`, `/template-editor.html`) and `server.serveStatic(...)` have `.setFilter(rejectDuringOta)` — a rejected filter falls through to the framework's catch-all (a plain 404, verified in `AsyncWebServer::_attachHandler`), not a crash. `/netinfo` doesn't touch LittleFS (pure in-memory state) so it's unguarded and doesn't need to be. If a new LittleFS-backed route is added later, it needs this same filter.

### ESP8266: static file requests cost ~1000-2300 bytes that isn't freed by the next request (known, mitigated not root-caused)

Measured directly via temporary request-level heap logging on a 16-relay ESP8266 board: serving each static file (`style.css`, a page's own `.js`, and previously `theme-apply.js`) via `server.serveStatic("/", LittleFS, "/")` costs roughly 1000-2300 bytes of heap that is still gone by the time the *next* request arrives — repeatable across multiple page loads. This is the dominant heap-decline driver on that board, well above the cost of button-press notifications or the page-route handlers themselves.

Traced as far as: `AsyncAbstractResponse::_ack()` (the per-TCP-ACK chunk sender in the vendored `me-no-dev/ESPAsyncWebServer` v1.2.3) properly `malloc`s and `free`s its send buffer within the same call — not the source. The likely cause is something held for the life of the request/response object itself (the `AsyncWebServerRequest`, the `File` handle, or the response object) that this older library doesn't free synchronously when the response completes, similar in spirit to why `ws.cleanupClients()` needed rate-limited attention — not yet root-caused further than this.

**2026-07-05: this became a real-world problem, not just a theoretical one.** After toggling all 16 relays and repeated WS reconnects on the live 16-relay board, heap settled around 9648/8920 bytes free (the normal stable "728-byte transient" pattern, not a leak). Navigating to `config.html` from that baseline stacked the HTML + `style.css` + `theme-apply.js` costs on top, bottoming out around 1824 bytes free — too low/fragmented for lwIP/AsyncTCP to allocate a send buffer, causing the request to silently stall (no crash, no reboot, browser just hangs). The board's `bootSessionId` had changed by the time it was checked again, meaning it eventually recovered via a watchdog reset, not gracefully.

**Mitigation applied: inlined `theme-apply.js` into every page's own `<head>`** instead of loading it as a 6th shared static file. This removes one static-file request (and its ~1500-2300 byte unrecovered cost) from every page load. The script is now duplicated identically across all 6 HTML pages in `data/` (`index.html`, `config.html`, `relay-config.html`, `boards.html`, `template-editor.html`, `theme.html`) — **if you edit this logic, you must update every copy** (grep `"Inlined from theme-apply.js"` across `data/*.html` to find them all). `data/theme-apply.js` no longer exists; `README.md` and `Invoke-BoardFunctionalHarness.ps1`'s boot-guard checks were updated to match (the harness now checks each page's own body for the guard logic instead of checking a shared file + script-tag).

This is a mitigation (reduces exposure by one request per page), **not a root-cause fix** — `style.css` and each page's own `.js` still cost heap the same way. Options still not acted on:
- Dig further into the library's request lifecycle/cleanup — addresses the actual cause but is a bigger, more uncertain effort in an old, unmaintained fork.
- A low-heap guard on page routes (return a fast 503 instead of a silent hang when heap is critically low before serving) — doesn't reduce heap usage but converts a silent lockup into a fast, recoverable error. Considered, not yet implemented.

If picking this back up: the per-request `[PageNav]`/`[HeapDiag]` heap logging used to find this (see git history / session notes around 2026-07-04/05) is the fastest way to re-verify a fix — log heap immediately before each static file request and confirm the delta disappears.

## Debugging

1. Reproduce deterministically.
2. Capture evidence: status, body, logs, platform scope.
3. Identify root cause — not just symptoms.
4. Apply a durable fix with minimal blast radius.
5. Re-run validation to confirm resolution.
