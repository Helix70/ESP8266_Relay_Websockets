#include "web_runtime.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "board_hardware.h"
#include "config_store.h"
#include "relay_runtime.h"
#include "storage_utils.h"

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

static void sendMainPageAndIssueGateCookie(AsyncWebServerRequest *request)
{
  ensureMainGateToken();

  AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html", false, nullptr);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Set-Cookie", String(kMainGateCookieName) + "=" + mainGateToken + "; Path=/; HttpOnly; SameSite=Lax");
  request->send(response);
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

static int parseRelayNumberFromCommand(const String &command)
{
  if (command.startsWith("button"))
  {
    return command.substring(6, command.length()).toInt();
  }

  if (command.startsWith("relay") && command.endsWith("toggle"))
  {
    return command.substring(5, command.length() - 6).toInt();
  }

  return 0;
}

static void buildRuntimeStatePayload(String &payload)
{
  ensureMainGateToken();

  // Full state path: only called when a new client connects ("home" command).
  // Allocated on demand and freed on return so 6 KB is not held permanently.
#ifdef ESP8266
  if (ESP.getMaxFreeBlockSize() < 8192)
  {
    Serial.printf("[WS] Heap too fragmented for full state: free=%lu maxBlock=%lu\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxFreeBlockSize());
    payload = "";
    return;
  }
#endif

  DynamicJsonDocument JSONdoc(6144);

  JsonArray array = JSONdoc.createNestedArray("buttons");
  for (int i = 0; i < relayCount; i++)
  {
    JsonObject button = array.createNestedObject();
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["d"] = relays[i].disabled;
    button["last"] = relays[i].last;
    button["onLabel"] = relayLabels[i].on;
    button["offLabel"] = relayLabels[i].off;
    button["m"] = relayLabels[i].mode;
    button["g"] = relayLabels[i].group;
    button["p"] = relayLabels[i].pulseTimeout;
    button["gpio"] = (relays[i].pin == 255) ? -1 : (int)relays[i].pin;
  }

  JSONdoc["boardName"] = boardName;
  JSONdoc["bootSessionId"] = mainGateToken;
  JSONdoc["setupComplete"] = (relayCount > 0 && hardwareVariant.length() > 0);
  JSONdoc["n"] = relayCount;
  JSONdoc["selectedRelayTemplate"] = selectedRelayTemplateFilename;

  JSONdoc["useStaticIp"] = useStaticIp;
  JSONdoc["useDhcp"] = !useStaticIp;

  String activeIp = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
  String activeDns = useStaticIp ? boardDns.toString() : WiFi.dnsIP().toString();
  String activeGateway = useStaticIp ? boardGateway.toString() : WiFi.gatewayIP().toString();
  String activeSubnet = useStaticIp ? boardSubnet.toString() : WiFi.subnetMask().toString();

  wl_status_t wifiStatus = WiFi.status();
  bool wifiConnected = (wifiStatus == WL_CONNECTED);

  JSONdoc["ipAddress"] = activeIp;
  JSONdoc["dns"] = activeDns;
  JSONdoc["gateway"] = activeGateway;
  JSONdoc["subnet"] = activeSubnet;
  JSONdoc["wifiConnected"] = wifiConnected;
  JSONdoc["wifiConfiguredSsid"] = wifiSsid;
  JSONdoc["wifiConnectedSsid"] = wifiConnected ? WiFi.SSID() : "";
  JSONdoc["wifiRssi"] = wifiConnected ? WiFi.RSSI() : 0;
  JSONdoc["wifiRescanInProgress"] = wifiRescanInProgress;
  JSONdoc["wifiRescanStatus"] = wifiRescanStatus;

  JSONdoc["doDelay"] = doDelay;
  JSONdoc["startupDelaySeconds"] = startupDelaySeconds;
  JSONdoc["connectStrongestOnStartup"] = connectStrongestOnStartup;
  JSONdoc["mcuType"] = BOARD_CPU_TYPE;

  payload = "";
  serializeJson(JSONdoc, payload);
}

void notifyClients()
{
  // Hot path: called on every config change (setLabel, setBoardName, etc.).
  // Uses char[] to avoid String realloc from interrupt context (ctx: sys).
  // Sized for 16 relays × 4 fields + 6 top-level fields: worst-case ~750 chars.
  static DynamicJsonDocument JSONdoc(1536);
  static char payload[1024];
  static uint32_t lastPayloadHash = 0;
  static size_t lastPayloadLen = 0;
  uint32_t buildStartUs = micros();

  ensureMainGateToken();
  JSONdoc.clear();
  JsonArray array = JSONdoc.createNestedArray("buttons");
  for (int i = 0; i < relayCount; i++)
  {
    JsonObject button = array.createNestedObject();
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

  size_t payloadLen = serializeJson(JSONdoc, payload, sizeof(payload));
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

  static String payload;
  buildRuntimeStatePayload(payload);
  if (payload.length() == 0)
  {
    return;
  }
  Serial.printf("[WS][Home] client=%u ip=%s heap=%lu payload=%u relayCount=%u selected=%s\n",
                (unsigned)client->id(),
                client->remoteIP().toString().c_str(),
                (unsigned long)ESP.getFreeHeap(),
                (unsigned)payload.length(),
                (unsigned)relayCount,
                selectedRelayTemplateFilename.c_str());
  client->text(payload);
}

void notifyRelayStates()
{
  // Capacity: JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(MAX) + MAX*JSON_OBJECT_SIZE(4)
  // On ESP8266 (sizeof(VariantSlot)=16): 32+256+1024 = 1312 bytes minimum.
  // char[] avoids String realloc from interrupt context (ctx: sys).
  static DynamicJsonDocument doc(
      JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(APP_MAX_RELAYS) + APP_MAX_RELAYS * JSON_OBJECT_SIZE(4));
  static char payload[640];

  doc.clear();
  doc["partial"] = true;
  JsonArray array = doc.createNestedArray("buttons");
  for (uint8_t i = 0; i < relayCount; i++)
  {
    JsonObject button = array.createNestedObject();
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["d"] = relays[i].disabled;
    button["last"] = relays[i].last;
  }

  serializeJson(doc, payload, sizeof(payload));
  ws.textAll(payload);
}

void notifyRelayState(uint8_t relayNum)
{
  if (relayNum < 1 || relayNum > relayCount)
  {
    return;
  }

  uint8_t idx = relayNum - 1;
  // char[] avoids String realloc from interrupt context (ctx: sys).
  static DynamicJsonDocument doc(256);
  static char payload[128];

  doc.clear();
  doc["partial"] = true;
  JsonArray array = doc.createNestedArray("buttons");
  JsonObject button = array.createNestedObject();
  button["id"] = relayNum;
  button["on"] = relays[idx].on;
  button["d"] = relays[idx].disabled;
  button["last"] = relays[idx].last;

  serializeJson(doc, payload, sizeof(payload));
  ws.textAll(payload);
}

static void handleWebSocketMessage(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT))
  {
    return;
  }

  String str;
  str.reserve(len + 1);
  for (size_t i = 0; i < len; i++)
  {
    str += (char)data[i];
  }

  bool notifyAll = false;
  bool notifyRelayAll = false;
  bool notifyRelaySingle = false;
  bool notifyRequester = false;
  int relayNum = 0;

  if (str.startsWith("{"))
  {
#ifdef ESP8266
    // Guard threshold: 2560 (doc) + 512 (str already allocated) + 512 (library overhead).
    if (ESP.getMaxFreeBlockSize() < 3072)
    {
      Serial.printf("[WS] Heap too fragmented for JSON command: free=%lu maxBlock=%lu\n",
                    (unsigned long)ESP.getFreeHeap(),
                    (unsigned long)ESP.getMaxFreeBlockSize());
      return;
    }
#endif
    // Sized for worst-case setLabels: 16 relays × {relay,on(32),off(32),mode,group,pulse} = ~2200 bytes.
    DynamicJsonDocument command(2560);
    DeserializationError error = deserializeJson(command, str);
    if (!error)
    {
      String cmd = String(command["cmd"] | "");
      if (cmd == "setLabel")
      {
        relayNum = command["relay"] | 0;
        if (relayNum > 0 && relayNum <= relayCount)
        {
          assignRelayLabels(relayNum, String(command["o"] | ""), String(command["f"] | ""));
          if (command.containsKey("mode"))
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
        if (command.containsKey("doDelay"))
        {
          doDelay = command["doDelay"] | false;
          changed = true;
        }
        if (command.containsKey("startupDelaySeconds"))
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
  else if (len == 4 && str == "home")
  {
    notifyRequester = true;
  }
  else if (len == 6 && str == "alloff")
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
    relayNum = parseRelayNumberFromCommand(str);
    if (relayNum > 0 && relayNum <= relayCount)
    {
      notifyRelaySingle = handlePerRelayModeToggle(relayNum);
      if (notifyRelaySingle)
      {
        uint8_t idx = (uint8_t)(relayNum - 1);
        if (relayLabels[idx].mode == RELAY_MODE_INTERLOCKED)
        {
          // Interlocked toggles can switch multiple relays in the same group.
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

  if (notifyAll)
  {
    notifyClients();
  }

  if (notifyRelayAll)
  {
    notifyRelayStates();
  }

  if (notifyRelaySingle)
  {
    notifyRelayState((uint8_t)relayNum);
  }

  if (notifyRequester)
  {
    notifyClient(client);
  }
}

static void onEvent(AsyncWebSocket *serverInstance, AsyncWebSocketClient *client, AwsEventType type,
                    void *arg, uint8_t *data, size_t len)
{
  (void)serverInstance;

  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
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

  ensureMainGateToken();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { sendMainPageAndIssueGateCookie(request); });

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request))
    {
      return;
    }
    request->send(LittleFS, "/config.html", "text/html", false, nullptr); });

  server.on("/relay-config.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request))
    {
      return;
    }
    request->send(LittleFS, "/relay-config.html", "text/html", false, nullptr); });

  server.on("/boards.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request))
    {
      return;
    }
    request->send(LittleFS, "/boards.html", "text/html", false, nullptr); });

  server.on("/template-editor.html", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!enforceChildPageGate(request))
    {
      return;
    }
    request->send(LittleFS, "/template-editor.html", "text/html", false, nullptr); });

  server.on("/netinfo", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    DynamicJsonDocument doc(512);

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

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload); });

  server.serveStatic("/", LittleFS, "/");
}

void startRuntimeServer()
{
  server.begin();
}
