#include "web_runtime.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "board_hardware.h"
#include "config_store.h"
#include "relay_runtime.h"
#include "storage_utils.h"

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

void notifyClients()
{
  DynamicJsonDocument JSONdoc(12288);

  JsonArray array = JSONdoc.createNestedArray("buttons");
  for (int i = 0; i < relayCount; i++)
  {
    JsonObject button = array.createNestedObject();
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["disabled"] = relays[i].disabled;
    button["last"] = relays[i].last;
    button["onLabel"] = relayLabels[i].on;
    button["offLabel"] = relayLabels[i].off;
    button["mode"] = relayLabels[i].mode;
    button["group"] = relayLabels[i].group;
    button["pulseTimeout"] = relayLabels[i].pulseTimeout;
    button["gpio"] = (relays[i].pin == 255) ? -1 : (int)relays[i].pin;
  }

  JSONdoc["boardName"] = boardName;
  JSONdoc["useStaticIp"] = useStaticIp;
  JSONdoc["useDhcp"] = !useStaticIp;

  String activeIp = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
  String activeDns = useStaticIp ? boardDns.toString() : WiFi.dnsIP().toString();
  String activeGateway = useStaticIp ? boardGateway.toString() : WiFi.gatewayIP().toString();
  String activeSubnet = useStaticIp ? boardSubnet.toString() : WiFi.subnetMask().toString();

  JSONdoc["ipAddress"] = activeIp;
  JSONdoc["dns"] = activeDns;
  JSONdoc["gateway"] = activeGateway;
  JSONdoc["subnet"] = activeSubnet;

  JSONdoc["doDelay"] = doDelay;
  JSONdoc["startupDelaySeconds"] = startupDelaySeconds;
  JSONdoc["doLatched"] = doLatched;
  JSONdoc["doInterlocked"] = doInterlocked;
  JSONdoc["doPulsed"] = doPulsed;
  JSONdoc["setupComplete"] = (relayCount > 0 && hardwareVariant.length() > 0);
  JSONdoc["hardwareVariant"] = hardwareVariant;
  JSONdoc["relayCount"] = relayCount;
  JSONdoc["boardHardwareFile"] = boardHardwarePath(hardwareVariant);
  JSONdoc["boardHardwareName"] = activeBoardHardware.name;

#if defined(ESP8266)
  JSONdoc["mcuType"] = "ESP8266";
#elif defined(ESP32)
  JSONdoc["mcuType"] = "ESP32";
#else
  JSONdoc["mcuType"] = "Unknown";
#endif

  String payload;
  serializeJson(JSONdoc, payload);
  ws.textAll(payload);
}

static void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  char buffer[len + 1];
  snprintf(buffer, len + 1, "%s", data);
  String str = buffer;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT))
  {
    return;
  }

  bool notify = false;
  int relayNum = 0;

  if (str.startsWith("{"))
  {
    DynamicJsonDocument command(4096);
    DeserializationError error = deserializeJson(command, str);
    if (!error)
    {
      String cmd = String(command["cmd"] | "");
      if (cmd == "setLabel")
      {
        relayNum = command["relay"] | 0;
        if (relayNum > 0 && relayNum <= relayCount)
        {
          assignRelayLabels(relayNum, String(command["on"] | ""), String(command["off"] | ""));
          if (command.containsKey("mode"))
          {
            String modeStr = String(command["mode"] | "onoff");
            uint8_t mode = RELAY_MODE_ONOFF;
            if (modeStr == "interlocked") mode = RELAY_MODE_INTERLOCKED;
            else if (modeStr == "pulsed") mode = RELAY_MODE_PULSED;
            assignRelayMode(relayNum, mode,
                            (uint8_t)(command["group"] | (uint8_t)0),
                            (uint8_t)(command["pulseTimeout"] | (uint8_t)1));
          }
          saveRelayLabels();
          notify = true;
        }
      }
      else if (cmd == "setLabels")
      {
        JsonArray labels = command["labels"].as<JsonArray>();
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
              assignRelayLabels(relayNum, String(label["on"] | ""), String(label["off"] | ""));
              String modeStr = String(label["mode"] | "onoff");
              uint8_t mode = RELAY_MODE_ONOFF;
              if (modeStr == "interlocked") mode = RELAY_MODE_INTERLOCKED;
              else if (modeStr == "pulsed") mode = RELAY_MODE_PULSED;
              assignRelayMode(relayNum, mode,
                              (uint8_t)(label["group"] | (uint8_t)0),
                              (uint8_t)(label["pulseTimeout"] | (uint8_t)1));
              changed = true;
            }
          }
        }

        if (changed)
        {
          saveRelayLabels();
          notify = true;
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
          notify = true;
        }
      }
      else if (cmd == "setBoardIP")
      {
        bool hasDhcp = command["useDhcp"] | false;

        if (hasDhcp)
        {
          useStaticIp = false;
          saveBoardConfig();
          delay(1000);
          ESP.restart();
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
            delay(1000);
            ESP.restart();
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
        if (command.containsKey("doLatched"))
        {
          doLatched = command["doLatched"] | false;
          changed = true;
        }
        if (command.containsKey("doInterlocked"))
        {
          doInterlocked = command["doInterlocked"] | false;
          changed = true;
        }
        if (command.containsKey("doPulsed"))
        {
          doPulsed = command["doPulsed"] | false;
          changed = true;
        }
        if (changed)
        {
          saveBoardConfig();
          notify = true;
        }
      }
    }
  }
  else if (str == "home")
  {
    notify = true;
  }
  else if (str == "alloff")
  {
    for (relayNum = 1; relayNum <= relayCount; relayNum++)
    {
      relays[relayNum - 1].low();
      relays[relayNum - 1].update();
      relays[relayNum - 1].disabled = 0;
      pulsed_relays[relayNum - 1].counter = 0;
    }
    notify = true;
  }
  else
  {
    relayNum = parseRelayNumberFromCommand(str);
    if (relayNum > 0 && relayNum <= relayCount)
    {
      notify = handlePerRelayModeToggle(relayNum);
    }
  }

  if (notify)
  {
    if (useShiftRegister)
    {
      writeRelaysToShiftRegister();
    }
    notifyClients();
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
    notifyClients();
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, useShiftRegister ? "/index16.html" : "/index.html", "text/html", false, nullptr); });

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

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload); });

  server.serveStatic("/", LittleFS, "/");
}

void startRuntimeServer()
{
  server.begin();
}
