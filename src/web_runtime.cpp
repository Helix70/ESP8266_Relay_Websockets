#include "web_runtime.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "board_hardware.h"
#include "config_store.h"
#include "relay_runtime.h"
#include "storage_utils.h"

// Defined in main.cpp; the CPPDEFINE injected by scripts/pio_custom_targets.py
// applies build-wide, so it's already available here without extra plumbing.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

static const char *kMainGateCookieName = "relay_main_gate";
static String mainGateToken;

struct RuntimeWsMetrics
{
  uint32_t windowStartedAtMs = 0;
  uint32_t sentCount = 0;
  uint32_t skippedDuplicateCount = 0;
  uint32_t totalPayloadBytes = 0;
  uint32_t maxPayloadBytes = 0;
  uint32_t totalBuildMicros = 0;
  uint32_t maxBuildMicros = 0;
};

static RuntimeWsMetrics runtimeWsMetrics;

// Pending notification flags: set from interrupt context (ctx: sys), dispatched from loop().
// ws.textAll() → makeBuffer() → new uint8_t[] must not be called from interrupt context.
static volatile bool pendingNotifyAll = false;
static volatile bool pendingNotifyRelayAll = false;
static volatile bool pendingNotifyRelaySingle = false;
static volatile uint8_t pendingNotifyRelaySingleId = 0;
static volatile bool pendingNotifyRequester = false;
static volatile uint32_t pendingNotifyRequesterId = 0;

// Shared output buffer for outgoing JSON payloads: notifyClients(),
// notifyRelayStates(), notifyRelayState(), notifyClient() (via
// buildRuntimeStatePayload()), and the /netinfo handler each used to have
// their own dedicated static buffer (1024+3584+1024+128+1024 = 6784 bytes
// total). None of these ever run concurrently with each other — on ESP8266
// they're either called sequentially from dispatchPendingNotifications() in
// loop(), or (both platforms) sequentially within a single WS/HTTP callback
// invocation, which is itself a single, non-reentrant execution context (no
// preemption mid-callback on either platform). Each function fully builds,
// serializes, and sends its payload before returning, and ws.textAll()/
// client->text() copy the buffer into their own internal send buffer before
// returning (see the interrupt-context comment above), so the shared buffer
// is safe to reuse immediately after each call. One buffer sized to the
// largest need replaces all five, reclaiming ~3.2KB of static RAM.
static char sharedNotifyPayload[3584];

static uint32_t hashPayload(const char *data, size_t len)
{
  // FNV-1a 32-bit hash for fast duplicate detection without retaining a full copy.
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; i++)
  {
    hash ^= (uint8_t)data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void maybePrintRuntimeWsMetrics()
{
  uint32_t now = millis();
  if (runtimeWsMetrics.windowStartedAtMs == 0)
  {
    runtimeWsMetrics.windowStartedAtMs = now;
    return;
  }

  uint32_t elapsedMs = now - runtimeWsMetrics.windowStartedAtMs;
  if (elapsedMs < 5000)
  {
    return;
  }

  float seconds = elapsedMs / 1000.0f;
  float sentPerSecond = (seconds > 0.0f) ? (runtimeWsMetrics.sentCount / seconds) : 0.0f;
  float avgPayloadBytes = (runtimeWsMetrics.sentCount > 0)
                              ? ((float)runtimeWsMetrics.totalPayloadBytes / (float)runtimeWsMetrics.sentCount)
                              : 0.0f;
  float avgBuildUs = (runtimeWsMetrics.sentCount > 0)
                         ? ((float)runtimeWsMetrics.totalBuildMicros / (float)runtimeWsMetrics.sentCount)
                         : 0.0f;

  Serial.printf(
      "[Perf][WS] %lus sent=%lu skip=%lu rate=%.2f/s avgBytes=%.1f maxBytes=%lu avgBuildUs=%.1f maxBuildUs=%lu\n",
      (unsigned long)(elapsedMs / 1000),
      (unsigned long)runtimeWsMetrics.sentCount,
      (unsigned long)runtimeWsMetrics.skippedDuplicateCount,
      sentPerSecond,
      avgPayloadBytes,
      (unsigned long)runtimeWsMetrics.maxPayloadBytes,
      avgBuildUs,
      (unsigned long)runtimeWsMetrics.maxBuildMicros);

  runtimeWsMetrics.windowStartedAtMs = now;
  runtimeWsMetrics.sentCount = 0;
  runtimeWsMetrics.skippedDuplicateCount = 0;
  runtimeWsMetrics.totalPayloadBytes = 0;
  runtimeWsMetrics.maxPayloadBytes = 0;
  runtimeWsMetrics.totalBuildMicros = 0;
  runtimeWsMetrics.maxBuildMicros = 0;
}

static String generateMainGateToken()
{
  uint32_t seed = (uint32_t)micros() ^ (uint32_t)millis() ^ (uint32_t)ESP.getFreeHeap();
  randomSeed(seed);
  uint32_t a = (uint32_t)random(0x7fffffff);
  uint32_t b = (uint32_t)random(0x7fffffff);

  String token = String(seed, HEX);
  token += String(a, HEX);
  token += String(b, HEX);
  token.toLowerCase();
  return token;
}

static void ensureMainGateToken()
{
  if (mainGateToken.length() > 0)
  {
    return;
  }

  mainGateToken = generateMainGateToken();
  Serial.println("Main-page access gate initialized for current boot session");
}

String getBootSessionToken()
{
  ensureMainGateToken();
  return mainGateToken;
}

static bool hasValidMainGateCookie(AsyncWebServerRequest *request)
{
  if (mainGateToken.length() == 0)
  {
    return false;
  }

  if (!request->hasHeader("Cookie"))
  {
    return false;
  }

  String cookieHeader = request->header("Cookie");
  String key = String(kMainGateCookieName) + "=";
  int start = cookieHeader.indexOf(key);
  if (start < 0)
  {
    return false;
  }

  start += key.length();
  int end = cookieHeader.indexOf(';', start);
  String cookieValue = (end < 0) ? cookieHeader.substring(start) : cookieHeader.substring(start, end);
  cookieValue.trim();

  return (cookieValue.length() > 0 && cookieValue == mainGateToken);
}

#ifdef ESP32
// ESPAsyncWebServer v3.x AsyncFileResponse(FS,...) uses available() to probe the file after
// open. available() = size() - position(), and if it returns 0 (zero-length file or position
// already at EOF due to a framework bug), the response falls back to the .gz path. When the .gz
// doesn't exist _content is left as a null File; _fillBuffer then calls File::read() which
// returns (size_t)-1 = SIZE_MAX, causing the chunked sender to emit a chunk-size header of
// 0xFFFFFFFF. The browser waits for that many bytes and hangs (spinner, no 404).
//
// Fix: open the file explicitly and use the File-based beginResponse overload, which sets
// _code = 200 directly from _content.size() without any available() check or gz fallback.
static AsyncWebServerResponse *beginResponseFromFile(AsyncWebServerRequest *request, const char *path, const char *contentType)
{
  File f = LittleFS.open(path, "r");
  if (!f)
  {
    Serial.printf("[Web] LittleFS.open failed: %s\n", path);
    return nullptr;
  }
  Serial.printf("[Web] %s size=%u pos=%u avail=%d\n", path, (unsigned)f.size(), (unsigned)f.position(), f.available());
  return request->beginResponse(f, String(path), contentType);
}
#endif

// Rejects any request while an OTA update is in progress: LittleFS is
// unmounted for the duration (see ArduinoOTA.onStart in main.cpp), so any
// handler that would touch it must not run. A rejected filter falls through
// to AsyncWebServer's catch-all (a plain 404), not a crash — verified in
// AsyncWebServer::_attachHandler, which checks filter() before canHandle().
static bool rejectDuringOta(AsyncWebServerRequest *request)
{
  (void)request;
  return !otaInProgress;
}

// [HeapDiag] TEMPORARY: remove once the heap-decline investigation is closed out.
static void logHeapDiag(const char *label, uint32_t before)
{
  Serial.printf("[HeapDiag] %s: before=%lu after=%lu\n", label, (unsigned long)before, (unsigned long)ESP.getFreeHeap());
}

static void sendMainPageAndIssueGateCookie(AsyncWebServerRequest *request)
{
  // [HeapDiag] TEMPORARY: remove once the heap-decline investigation is closed out.
  uint32_t heapBeforeMainPage = ESP.getFreeHeap();

  ensureMainGateToken();

#ifdef ESP32
  AsyncWebServerResponse *response = beginResponseFromFile(request, "/index.html", "text/html");
  if (!response)
  {
    request->send(404);
    return;
  }
#else
  AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html", false, nullptr);
#endif
  // Built into a fixed buffer instead of chained String concatenation
  // (String(kMainGateCookieName) + "=" + mainGateToken + "...") to avoid
  // several sequential heap reallocations for one header value on every
  // single main-page load — cheap normally, but this is the exact call that
  // failed (24-byte alloc) on a 16-relay ESP8266 board with heap already
  // near zero, per a captured OOM crash in AsyncWebServerResponse::addHeader.
  char cookieHeader[96];
  snprintf(cookieHeader, sizeof(cookieHeader), "%s=%s; Path=/; HttpOnly; SameSite=Lax",
           kMainGateCookieName, mainGateToken.c_str());
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Set-Cookie", cookieHeader);
  request->send(response);

  // [HeapDiag] TEMPORARY: remove once the heap-decline investigation is closed out.
  Serial.printf("[HeapDiag] main page serve: before=%lu after=%lu\n",
                (unsigned long)heapBeforeMainPage, (unsigned long)ESP.getFreeHeap());
}

static bool enforceChildPageGate(AsyncWebServerRequest *request)
{
  if (hasValidMainGateCookie(request))
  {
    return true;
  }

  request->redirect("/");
  return false;
}

static int parseRelayNumberFromCommand(const char *cmd, size_t cmdLen)
{
  if (cmdLen > 6 && strncmp(cmd, "button", 6) == 0)
  {
    return atoi(cmd + 6);
  }

  if (cmdLen > 11 && strncmp(cmd, "relay", 5) == 0 && strcmp(cmd + cmdLen - 6, "toggle") == 0)
  {
    return atoi(cmd + 5);
  }

  return 0;
}

static void buildRuntimeStatePayload(char *payload, size_t payloadSize)
{
  payload[0] = '\0';
  ensureMainGateToken();

  // Guard threshold is 3072: the build-once doc below needs no new heap after
  // its first build (only per-call String field churn, see below); this just
  // covers the AsyncWebSocket frame buffer (~2600 bytes) needing contiguous
  // space to actually send the result.
#ifdef ESP8266
  if (ESP.getMaxFreeBlockSize() < 3072)
  {
    Serial.printf("[WS] Heap too fragmented for full state: free=%lu maxBlock=%lu\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxFreeBlockSize());
    return;
  }
#endif

  // Serves the main page only: relay states, labels, and basic board info.
  // Network/WiFi/MCU/delay fields removed — config page fetches those via /netinfo.
  // Build-once, mutate-in-place (same pattern as notifyRelayStates()): this
  // was assumed to be a low-frequency path ("once per client connect"), but
  // the client re-sends "home" on every WebSocket reconnect — on a marginal
  // WiFi link that reconnects often, calling JSONdoc.clear() every time was
  // a ~3900-byte pool destroy+rebuild on every reconnect (ArduinoJson v7's
  // clear() really frees the pool, see notifyRelayStates()), which fragmented
  // the heap badly enough to eventually trip the guard above permanently.
  static JsonDocument JSONdoc;
  static JsonObject buttons[APP_MAX_RELAYS];
  static uint8_t builtCount = 255; // sentinel: forces (re)build on first call

  if (builtCount != relayCount)
  {
    JSONdoc.clear();
    JsonArray array = JSONdoc["buttons"].template to<JsonArray>();
    for (uint8_t i = 0; i < relayCount; i++)
    {
      buttons[i] = array.add<JsonObject>();
    }
    builtCount = relayCount;
  }

  for (int i = 0; i < relayCount; i++)
  {
    buttons[i]["id"] = i + 1;
    buttons[i]["on"] = relays[i].on;
    buttons[i]["d"] = relays[i].disabled;
    buttons[i]["last"] = relays[i].last;
    buttons[i]["onLabel"] = relayLabels[i].on;
    buttons[i]["offLabel"] = relayLabels[i].off;
    buttons[i]["m"] = relayLabels[i].mode;
    buttons[i]["g"] = relayLabels[i].group;
    buttons[i]["p"] = relayLabels[i].pulseTimeout;
    buttons[i]["gpio"] = (relays[i].pin == 255) ? -1 : (int)relays[i].pin;
  }

  JSONdoc["boardName"] = boardName;
  JSONdoc["bootSessionId"] = mainGateToken;
  JSONdoc["setupComplete"] = (relayCount > 0 && hardwareVariant.length() > 0);
  JSONdoc["n"] = relayCount;
  JSONdoc["selectedRelayTemplate"] = selectedRelayTemplateFilename;
  JSONdoc["bootWarning"] = bootWarning;
  JSONdoc["firmwareVersion"] = FIRMWARE_VERSION;

  serializeJson(JSONdoc, payload, payloadSize);
}

void notifyClients()
{
  // Called on config changes (setLabel, setBoardName, etc.) — not per relay
  // toggle (those go through notifyRelayStates()/notifyRelayState() instead),
  // so it's low-frequency enough that a local JsonDocument (fresh pool each
  // call) is fine; a build-once cache here would also need to diff the
  // boardName/bootSessionId/selectedRelayTemplate strings against their last
  // value to actually avoid allocation, since ArduinoJson frees+reallocates a
  // string slot on every assignment even when the value is unchanged
  // (VariantData::clear() dereferences the old owned string before setString()
  // stores the new one) — not worth the complexity at this call frequency.
  // Uses char[] to avoid String realloc from interrupt context (ctx: sys).
  // Sized for 16 relays × 4 fields + 6 top-level fields: worst-case ~750 chars.
  JsonDocument JSONdoc;
  char *payload = sharedNotifyPayload;
  size_t payloadCapacity = sizeof(sharedNotifyPayload);
  static uint32_t lastPayloadHash = 0;
  static size_t lastPayloadLen = 0;
  uint32_t buildStartUs = micros();

  ensureMainGateToken();
  JsonArray array = JSONdoc["buttons"].template to<JsonArray>();
  for (int i = 0; i < relayCount; i++)
  {
    JsonObject button = array.add<JsonObject>();
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["d"] = relays[i].disabled;
    button["last"] = relays[i].last;
  }
  JSONdoc["boardName"] = boardName;
  JSONdoc["bootSessionId"] = mainGateToken;
  JSONdoc["setupComplete"] = (relayCount > 0 && hardwareVariant.length() > 0);
  JSONdoc["n"] = relayCount;
  JSONdoc["selectedRelayTemplate"] = selectedRelayTemplateFilename;

  size_t payloadLen = serializeJson(JSONdoc, payload, payloadCapacity);
  uint32_t buildElapsedUs = micros() - buildStartUs;
  uint32_t payloadHash = hashPayload(payload, payloadLen);

  if (payloadHash == lastPayloadHash && payloadLen == lastPayloadLen)
  {
    runtimeWsMetrics.skippedDuplicateCount++;
    maybePrintRuntimeWsMetrics();
    return;
  }

  uint32_t payloadBytes = (uint32_t)payloadLen;
  runtimeWsMetrics.sentCount++;
  runtimeWsMetrics.totalPayloadBytes += payloadBytes;
  if (payloadBytes > runtimeWsMetrics.maxPayloadBytes)
  {
    runtimeWsMetrics.maxPayloadBytes = payloadBytes;
  }
  runtimeWsMetrics.totalBuildMicros += buildElapsedUs;
  if (buildElapsedUs > runtimeWsMetrics.maxBuildMicros)
  {
    runtimeWsMetrics.maxBuildMicros = buildElapsedUs;
  }

  ws.textAll(payload);
  lastPayloadHash = payloadHash;
  lastPayloadLen = payloadLen;
  maybePrintRuntimeWsMetrics();
}

static void notifyClient(AsyncWebSocketClient *client)
{
  if (client == nullptr)
  {
    return;
  }

  // Sized for 16 relays × ~150 chars + ~500 chars config fields, plus margin
  // for bootWarning/firmwareVersion added after this was first sized (worst
  // realistic case with a long board name, template filename, and one
  // boot-warning message is ~3100 bytes — this leaves headroom instead of the
  // ~94 bytes the original 3200 left). Shares sharedNotifyPayload (see top of
  // file) rather than its own buffer — avoids the String realloc chain
  // (1→2→4→...→4096 bytes) on every "home" response either way.
  char *payload = sharedNotifyPayload;
  buildRuntimeStatePayload(payload, sizeof(sharedNotifyPayload));
  if (payload[0] == '\0')
  {
    return;
  }
  Serial.printf("[WS][Home] client=%u ip=%s heap=%lu payload=%u relayCount=%u selected=%s\n",
                (unsigned)client->id(),
                client->remoteIP().toString().c_str(),
                (unsigned long)ESP.getFreeHeap(),
                (unsigned)strlen(payload),
                (unsigned)relayCount,
                selectedRelayTemplateFilename.c_str());
  client->text(payload);
}

void notifyRelayStates()
{
  // Hot path: runs on every relay toggle. The buttons array/object structure
  // is built once and reused — ArduinoJson v7's JsonDocument::clear() actually
  // frees its memory pools (MemoryPool::destroy() calls allocator->deallocate(),
  // see ArduinoJson/Memory/MemoryPool.hpp), so calling clear() every call would
  // silently reallocate from scratch each time despite the "static" keyword.
  // Only rebuilding when relayCount changes (boot/hardware reconfig, not a
  // per-toggle event) means every ordinary call just overwrites existing
  // numeric/boolean slots in place — verified zero-allocation: the integer/
  // bool converters (ArduinoJson/Variant/ConverterImpl.hpp) call setInteger()/
  // setBoolean() directly with no pool interaction, unlike string assignment.
  //
  // char[] payload avoids String realloc from interrupt context (ctx: sys).
  // Sized for the actual serialized JSON, not the ArduinoJson pool: each
  // button is `{"id":16,"on":false,"d":0,"last":4294967295}` (~46 bytes),
  // so 16 relays + the "partial"/"buttons" wrapper is ~750 bytes worst case.
  // (A prior 640-byte buffer silently truncated this on 16-relay boards —
  // every toggle broadcast invalid JSON that only 16-relay clients ever saw.)
  static JsonDocument doc;
  static JsonObject buttons[APP_MAX_RELAYS];
  static uint8_t builtCount = 255; // sentinel: forces (re)build on first call
  char *payload = sharedNotifyPayload;

  if (builtCount != relayCount)
  {
    doc.clear();
    doc["partial"] = true;
    JsonArray array = doc["buttons"].template to<JsonArray>();
    for (uint8_t i = 0; i < relayCount; i++)
    {
      buttons[i] = array.add<JsonObject>();
    }
    builtCount = relayCount;
  }

  for (uint8_t i = 0; i < relayCount; i++)
  {
    buttons[i]["id"] = i + 1;
    buttons[i]["on"] = relays[i].on;
    buttons[i]["d"] = relays[i].disabled;
    buttons[i]["last"] = relays[i].last;
  }

  serializeJson(doc, payload, sizeof(sharedNotifyPayload));
  ws.textAll(payload);
}

void notifyRelayState(uint8_t relayNum)
{
  if (relayNum < 1 || relayNum > relayCount)
  {
    return;
  }

  uint8_t idx = relayNum - 1;
  // Hot path: runs on every relay toggle. Built once (always exactly one
  // button object, unlike notifyRelayStates() this never needs to resize) and
  // reused thereafter — see notifyRelayStates() above for why clear() isn't
  // called per-call. char[] payload avoids String realloc from interrupt
  // context (ctx: sys).
  static JsonDocument doc;
  static JsonObject button;
  static bool built = false;
  char *payload = sharedNotifyPayload;

  if (!built)
  {
    doc.clear();
    doc["partial"] = true;
    JsonArray array = doc["buttons"].template to<JsonArray>();
    button = array.add<JsonObject>();
    built = true;
  }

  button["id"] = relayNum;
  button["on"] = relays[idx].on;
  button["d"] = relays[idx].disabled;
  button["last"] = relays[idx].last;

  serializeJson(doc, payload, sizeof(sharedNotifyPayload));
  ws.textAll(payload);
}

void dispatchPendingNotifications()
{
  // TEMPORARY diagnostic: isolates the heap cost of a single button-press's
  // notification dispatch (JSON build + ws.textAll()/text()) from everything
  // else happening in loop(). Remove once the heap-decline investigation is
  // closed out.
  bool willDispatch = pendingNotifyAll || pendingNotifyRelayAll || pendingNotifyRelaySingle || pendingNotifyRequester;
  uint32_t heapBeforeDispatch = willDispatch ? ESP.getFreeHeap() : 0;

  if (pendingNotifyAll)
  {
    pendingNotifyAll = false;
    notifyClients();
  }
  if (pendingNotifyRelayAll)
  {
    pendingNotifyRelayAll = false;
    notifyRelayStates();
  }
  if (pendingNotifyRelaySingle)
  {
    uint8_t relayId = pendingNotifyRelaySingleId;
    pendingNotifyRelaySingle = false;
    notifyRelayState(relayId);
  }
  if (pendingNotifyRequester)
  {
    uint32_t clientId = pendingNotifyRequesterId;
    pendingNotifyRequester = false;
    AsyncWebSocketClient *c = ws.client(clientId);
    if (c)
    {
      notifyClient(c);
    }
  }

  if (willDispatch)
  {
    uint32_t heapAfterDispatch = ESP.getFreeHeap();
    Serial.printf("[HeapDiag] button dispatch: before=%lu after=%lu delta=%ld\n",
                  (unsigned long)heapBeforeDispatch,
                  (unsigned long)heapAfterDispatch,
                  (long)heapBeforeDispatch - (long)heapAfterDispatch);
  }
}

static void handleWebSocketMessage(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT))
  {
    return;
  }

  // Static buffer avoids malloc from interrupt context (ctx: sys).
  // Sized for worst-case setLabels: 16 relays × ~100-char labels ≈ 1700 bytes.
  // Messages longer than this are truncated and will fail JSON parse safely.
  static char str_buf[1700];
  size_t copyLen = (len < sizeof(str_buf) - 1) ? len : (sizeof(str_buf) - 1);
  memcpy(str_buf, data, copyLen);
  str_buf[copyLen] = '\0';

  bool notifyAll = false;
  bool notifyRelayAll = false;
  bool notifyRelaySingle = false;
  bool notifyRequester = false;
  int relayNum = 0;

  if (str_buf[0] == '{')
  {
    JsonDocument command;
    DeserializationError error = deserializeJson(command, str_buf, copyLen);
    if (!error)
    {
      String cmd = String(command["cmd"] | "");
      if (cmd == "setLabel")
      {
        relayNum = command["relay"] | 0;
        if (relayNum > 0 && relayNum <= relayCount)
        {
          assignRelayLabels(relayNum, String(command["o"] | ""), String(command["f"] | ""));
          if (command["mode"].is<JsonVariantConst>())
          {
            String modeStr = String(command["m"] | "L");
            uint8_t mode = RELAY_MODE_ONOFF;
            if (modeStr == "I") mode = RELAY_MODE_INTERLOCKED;
            else if (modeStr == "P") mode = RELAY_MODE_PULSED;
            assignRelayMode(relayNum, mode,
                            (uint8_t)(command["g"] | (uint8_t)0),
                            (uint8_t)(command["p"] | (uint8_t)1));
          }
          saveRelayLabels();
          notifyAll = true;
        }
      }
      else if (cmd == "setLabels")
      {
        JsonArray labels = command["l"].as<JsonArray>();
        bool changed = false;

        if (!labels.isNull())
        {
          uint8_t count = labels.size() < relayCount ? labels.size() : relayCount;
          for (uint8_t i = 0; i < count; i++)
          {
            JsonObject label = labels[i];
            if (label.isNull()) continue;

            relayNum = label["relay"] | (i + 1);
            if (relayNum > 0 && relayNum <= relayCount)
            {
              assignRelayLabels(relayNum, String(label["o"] | ""), String(label["f"] | ""));
              String modeStr = String(label["m"] | "L");
              uint8_t mode = RELAY_MODE_ONOFF;
              if (modeStr == "I") mode = RELAY_MODE_INTERLOCKED;
              else if (modeStr == "P") mode = RELAY_MODE_PULSED;
              assignRelayMode(relayNum, mode,
                              (uint8_t)(label["g"] | (uint8_t)0),
                              (uint8_t)(label["p"] | (uint8_t)1));
              changed = true;
            }
          }
        }

        if (changed)
        {
          saveRelayLabels();
          notifyAll = true;
        }
      }
      else if (cmd == "setBoardName")
      {
        String newName = String(command["name"] | "");
        if (newName.length() > 0)
        {
          newName.trim();
          if (newName.length() > kMaxBoardNameLength)
          {
            newName = newName.substring(0, kMaxBoardNameLength);
          }
          boardName = newName;
          saveBoardConfig();
          notifyAll = true;
        }
      }
      else if (cmd == "setBoardIP")
      {
        bool hasDhcp = command["useDhcp"] | false;

        if (hasDhcp)
        {
          useStaticIp = false;
          saveBoardConfig();
          pendingRestart = true;
          pendingRestartAt = millis() + 1200;
          notifyAll = true;
        }
        else
        {
          String ipStr = String(command["ip"] | "");
          String dnsStr = String(command["dns"] | "");
          String gatewayStr = String(command["gateway"] | "");
          String subnetStr = String(command["subnet"] | "");

          IPAddress testIp, testDns, testGateway, testSubnet;
          if (ipStr.length() > 0 && testIp.fromString(ipStr) &&
              dnsStr.length() > 0 && testDns.fromString(dnsStr) &&
              gatewayStr.length() > 0 && testGateway.fromString(gatewayStr) &&
              subnetStr.length() > 0 && testSubnet.fromString(subnetStr))
          {
            boardIp = testIp;
            boardDns = testDns;
            boardGateway = testGateway;
            boardSubnet = testSubnet;
            useStaticIp = true;
            saveBoardConfig();
            pendingRestart = true;
            pendingRestartAt = millis() + 1200;
            notifyAll = true;
          }
        }
      }
      else if (cmd == "setRelayModes")
      {
        bool changed = false;
        if (command["doDelay"].is<JsonVariantConst>())
        {
          doDelay = command["doDelay"] | false;
          changed = true;
        }
        if (command["startupDelaySeconds"].is<JsonVariantConst>())
        {
          uint16_t parsed = (uint16_t)(command["startupDelaySeconds"] | startupDelaySeconds);
          if (parsed > 300)
          {
            parsed = 300;
          }
          startupDelaySeconds = parsed;
          changed = true;
        }
        if (changed)
        {
          saveBoardConfig();
          notifyAll = true;
        }
      }
    }
  }
  else if (len == 4 && strcmp(str_buf, "home") == 0)
  {
    notifyRequester = true;
  }
  else if (len == 6 && strcmp(str_buf, "alloff") == 0)
  {
    for (relayNum = 1; relayNum <= relayCount; relayNum++)
    {
      relays[relayNum - 1].low();
      relays[relayNum - 1].update();
      relays[relayNum - 1].disabled = 0;
      pulsed_relays[relayNum - 1].counter = 0;
    }
    notifyRelayAll = true;
  }
  else
  {
    relayNum = parseRelayNumberFromCommand(str_buf, copyLen);
    if (relayNum > 0 && relayNum <= relayCount)
    {
      notifyRelaySingle = handlePerRelayModeToggle(relayNum);
      if (notifyRelaySingle)
      {
        uint8_t idx = (uint8_t)(relayNum - 1);
        if (relayLabels[idx].group > 0)
        {
          // Grouped modes (latched / interlocked / pulsed) can switch or
          // disable several relays at once, so refresh the grid.
          notifyRelayAll = true;
          notifyRelaySingle = false;
        }
      }
    }
  }

  if (notifyAll || notifyRelayAll || notifyRelaySingle)
  {
    if (useShiftRegister)
    {
      writeRelaysToShiftRegister();
    }
  }

#ifdef ESP8266
  // On ESP8266, ws.textAll() → makeBuffer() → new uint8_t[] allocates from
  // lwIP interrupt context (ctx: sys), corrupting the heap allocator.
  // Defer sends to loop(); dispatchPendingNotifications() drains these flags.
  if (notifyAll)      pendingNotifyAll = true;
  if (notifyRelayAll) pendingNotifyRelayAll = true;
  if (notifyRelaySingle)
  {
    pendingNotifyRelaySingleId = (uint8_t)relayNum;
    pendingNotifyRelaySingle = true;
  }
  if (notifyRequester)
  {
    pendingNotifyRequesterId = client->id();
    pendingNotifyRequester = true;
  }
#else
  // On ESP32, the WebSocket handler runs in a FreeRTOS task where malloc is safe.
  // Call directly for minimum UI latency.
  if (notifyAll)         notifyClients();
  if (notifyRelayAll)    notifyRelayStates();
  if (notifyRelaySingle) notifyRelayState((uint8_t)relayNum);
  if (notifyRequester)   notifyClient(client);
#endif
}

static void onEvent(AsyncWebSocket *serverInstance, AsyncWebSocketClient *client, AwsEventType type,
                    void *arg, uint8_t *data, size_t len)
{
  (void)serverInstance;

  switch (type)
  {
  case WS_EVT_CONNECT:
#ifdef ESP32
    // Disable Nagle's algorithm per-connection: lwIP buffers small segments until
    // an ACK arrives, adding 40-200ms per send for 50-byte relay state messages.
    client->client()->setNoDelay(true);
#endif
    // [HeapDiag] TEMPORARY: remove once the heap-decline investigation is closed out.
    Serial.printf("WebSocket client #%u connected from %s heap=%lu\n", client->id(),
                  client->remoteIP().toString().c_str(), (unsigned long)ESP.getFreeHeap());
    break;
  case WS_EVT_DISCONNECT:
    // [HeapDiag] TEMPORARY: remove once the heap-decline investigation is closed out.
    Serial.printf("WebSocket client #%u disconnected heap=%lu\n", client->id(),
                  (unsigned long)ESP.getFreeHeap());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(client, arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
}

void registerRuntimeHttpRoutes()
{
  server.addHandler(&ws);

#ifdef ESP32
  // Disable Nagle's algorithm for all HTTP connections on ESP32.
  // ESPAsyncWebServer v3.x iterates handlers and calls canHandle() on each; a
  // handler that returns false is skipped. We exploit this: return false so the
  // actual handler still runs, but set TCP_NODELAY as a side effect so every
  // HTTP response (HTML, CSS, JS) is sent without the 40-200ms delayed-ACK
  // penalty that Nagle imposes on the final partial TCP segment.
  // The WebSocket client already gets setNoDelay at WS_EVT_CONNECT; this covers
  // all plain HTTP connections that serveStatic and the route handlers use.
  static struct : AsyncWebHandler
  {
    bool canHandle(AsyncWebServerRequest *request) const override
    {
      request->client()->setNoDelay(true);
      return false;
    }
    void handleRequest(AsyncWebServerRequest *) override {}
  } noDelayHandler;
  server.addHandler(&noDelayHandler);
#endif

  // [HeapDiag] TEMPORARY: logs every incoming request's path + heap, in
  // request order, before it's dispatched to its real handler — same
  // canHandle()-returns-false observer trick as noDelayHandler above. Fills
  // the gap the per-route before/after logging can't: visibility into static
  // asset requests (style.css, script.js, config.js, ...) that have no
  // dedicated handler to instrument, so a full page navigation's request
  // sequence (HTML + CSS + JS + any API calls) shows up as one clearly
  // ordered block in the log instead of being inferred from timing. Remove
  // once the heap-decline investigation is closed out.
  static struct : AsyncWebHandler
  {
    // canHandle() is const on the ESP32 fork's AsyncWebHandler but non-const
    // on ESP8266's older fork — match whichever this platform expects.
#ifdef ESP32
    bool canHandle(AsyncWebServerRequest *request) const override
#else
    bool canHandle(AsyncWebServerRequest *request) override
#endif
    {
      Serial.printf("[PageNav] %s heap=%lu\n", request->url().c_str(), (unsigned long)ESP.getFreeHeap());
      return false;
    }
    void handleRequest(AsyncWebServerRequest *) override {}
  } pageNavLogger;
  server.addHandler(&pageNavLogger);

  ensureMainGateToken();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { sendMainPageAndIssueGateCookie(request); })
      .setFilter(rejectDuringOta);

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request)) return;
    uint32_t heapBefore = ESP.getFreeHeap(); // [HeapDiag] TEMPORARY
#ifdef ESP32
    AsyncWebServerResponse *r = beginResponseFromFile(request, "/config.html", "text/html");
    if (!r) { request->send(404); return; }
    request->send(r);
#else
    request->send(LittleFS, "/config.html", "text/html", false, nullptr);
#endif
    logHeapDiag("config.html serve", heapBefore); // [HeapDiag] TEMPORARY
  }).setFilter(rejectDuringOta);

  server.on("/relay-config.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request)) return;
    uint32_t heapBefore = ESP.getFreeHeap(); // [HeapDiag] TEMPORARY
#ifdef ESP32
    AsyncWebServerResponse *r = beginResponseFromFile(request, "/relay-config.html", "text/html");
    if (!r) { request->send(404); return; }
    request->send(r);
#else
    request->send(LittleFS, "/relay-config.html", "text/html", false, nullptr);
#endif
    logHeapDiag("relay-config.html serve", heapBefore); // [HeapDiag] TEMPORARY
  }).setFilter(rejectDuringOta);

  server.on("/boards.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request)) return;
    uint32_t heapBefore = ESP.getFreeHeap(); // [HeapDiag] TEMPORARY
#ifdef ESP32
    AsyncWebServerResponse *r = beginResponseFromFile(request, "/boards.html", "text/html");
    if (!r) { request->send(404); return; }
    request->send(r);
#else
    request->send(LittleFS, "/boards.html", "text/html", false, nullptr);
#endif
    logHeapDiag("boards.html serve", heapBefore); // [HeapDiag] TEMPORARY
  }).setFilter(rejectDuringOta);

  server.on("/template-editor.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request)) return;
    uint32_t heapBefore = ESP.getFreeHeap(); // [HeapDiag] TEMPORARY
#ifdef ESP32
    AsyncWebServerResponse *r = beginResponseFromFile(request, "/template-editor.html", "text/html");
    if (!r) { request->send(404); return; }
    request->send(r);
#else
    request->send(LittleFS, "/template-editor.html", "text/html", false, nullptr);
#endif
    logHeapDiag("template-editor.html serve", heapBefore); // [HeapDiag] TEMPORARY
  }).setFilter(rejectDuringOta);

  server.on("/netinfo", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    // 18 fields × ~16 bytes slot overhead + ~600 bytes string data ≈ 900 bytes;
    // shares sharedNotifyPayload (3584 bytes) rather than its own buffer.
    JsonDocument doc;
    char *payload = sharedNotifyPayload;

    String activeIp = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
    String activeDns = useStaticIp ? boardDns.toString() : WiFi.dnsIP().toString();
    String activeGateway = useStaticIp ? boardGateway.toString() : WiFi.gatewayIP().toString();
    String activeSubnet = useStaticIp ? boardSubnet.toString() : WiFi.subnetMask().toString();

    doc["useDhcp"] = !useStaticIp;
    doc["useStaticIp"] = useStaticIp;
    doc["ipAddress"] = activeIp;
    doc["dns"] = activeDns;
    doc["gateway"] = activeGateway;
    doc["subnet"] = activeSubnet;

    wl_status_t wifiStatus = WiFi.status();
    bool wifiConnected = (wifiStatus == WL_CONNECTED);
    doc["wifiConnected"] = wifiConnected;
    doc["wifiConfiguredSsid"] = wifiSsid;
    doc["wifiConnectedSsid"] = wifiConnected ? WiFi.SSID() : "";
    doc["wifiRssi"] = wifiConnected ? WiFi.RSSI() : 0;
    doc["wifiRescanInProgress"] = wifiRescanInProgress;
    doc["wifiRescanStatus"] = wifiRescanStatus;

    doc["boardName"] = boardName;
    doc["hardwareVariant"] = hardwareVariant;
    doc["n"] = relayCount;
    doc["boardHardwareFile"] = activeBoardHardwareFilename;
    doc["boardHardwareName"] = activeBoardHardware.name;

  #if defined(ESP8266)
    doc["mcuType"] = "ESP8266";
  #elif defined(ESP32)
    doc["mcuType"] = "ESP32";
  #else
    doc["mcuType"] = "Unknown";
  #endif

    doc["doDelay"] = doDelay;
    doc["startupDelaySeconds"] = startupDelaySeconds;
    doc["connectStrongestOnStartup"] = connectStrongestOnStartup;
    doc["setupComplete"] = (relayCount > 0 && hardwareVariant.length() > 0);
    ensureMainGateToken();
    doc["bootSessionId"] = mainGateToken;

    serializeJson(doc, payload, sizeof(sharedNotifyPayload));
    request->send(200, "application/json", payload); });

  // no-store: static assets are small and change during development/updates.
  // The library's default (no explicit Cache-Control) falls back to an ETag
  // based on file size when LittleFS doesn't preserve mtimes (mklittlefs has
  // no such option), which can miss same-size content edits. no-store trades
  // browser caching for guaranteed-fresh assets without needing hand-stamped
  // ?v= query strings on every reference.
  server.serveStatic("/", LittleFS, "/").setCacheControl("no-store").setFilter(rejectDuringOta);
}

void startRuntimeServer()
{
  server.begin();
}
