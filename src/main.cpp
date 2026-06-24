/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp8266-nodemcu-websocket-server-arduino/
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/

// Import required libraries
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "LittleFS.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#endif

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "config_store.h"
#include "provisioning_portal.h"
#include "board_hardware.h"
#include "serial_provision.h"
#include "storage_utils.h"

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "relay-board"
#endif

uint32_t elapsed = 0;
uint32_t timer = 0;
uint32_t latched_timer = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char *kRelayLabelsPath = "/relay_labels.json";
const size_t kMaxRelayLabelLength = 32;
const char *kBoardConfigPath = "/board_config.json";
const size_t kMaxBoardNameLength = 64;
const size_t kMaxSsidLength = 32;
const size_t kMaxPasswordLength = 64;

String boardName = "Relay Board";
String wifiSsid;
String wifiPassword;
String serialCommandBuffer;
bool reportSignalStrength = true;
bool provisioningMode = false;
bool provisioningScanRunning = false;
bool provisioningScanRequested = false;
bool provisioningScanInitialized = false;
uint32_t provisioningScanStartedAt = 0;
String provisioningScanPayload = "{\"ssids\":[],\"scanning\":false}";

IPAddress boardIp;
IPAddress boardDns;
IPAddress boardGateway;
IPAddress boardSubnet;
bool useStaticIp = false;

// Relay mode settings (moved from compiler flags)
bool doDelay = false;
uint16_t startupDelaySeconds = 60;
bool doLatched = false;
bool doInterlocked = false;
bool doPulsed = false;

bool pendingRestart = false;
uint32_t pendingRestartAt = 0;

bool saveBoardConfig();
void initWiFi();
void notifyClients();
void applyHardwareVariantPinsAndModes();

#define DELAY_INTERVAL_MS 50 // check  exery 50ms
#define DELAY_COUNTER (1000 / DELAY_INTERVAL_MS)

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct OutputPin
{
  uint8_t pin;
  uint8_t on;
  const uint8_t on_state;
  uint8_t disabled;
  uint8_t last;

  void update()
  {
    if (pin != 255)
    {
      digitalWrite(pin, on ? HIGH : LOW);
    }
  }

  void low() { on = !on_state; }
  void high() { on = on_state; }
  void toggle() { on = !on; }
  uint8_t state() { return on; }
};

static const uint8_t MAX_RELAYS = 16;
static const char *kVariant8Relay = "8relay";
static const char *kVariant16Relay = "16relay";

String hardwareVariant = ""; // set by loadBoardConfig(); empty = not yet configured
uint8_t relayCount = 0;
bool useShiftRegister = false;

RelayLabel relayLabels[MAX_RELAYS];

// Pin set at startup from board hardware config
OutputPin onboard_led = {255, false, HIGH, false};

OutputPin relays[MAX_RELAYS] = {
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}};
const int numRegisters = 2;
byte outputData[numRegisters];

struct Latch
{
  uint8_t relay_num;
  uint8_t latched_num;
  uint8_t timeout;
  uint16_t counter;
};

Latch latched_relays[MAX_RELAYS] = {
    {1, 2, 0, 0}, {2, 1, 0, 0}, {3, 4, 0, 0}, {4, 3, 0, 0},
    {5, 6, 0, 0}, {6, 5, 0, 0}, {7, 8, 0, 0}, {8, 7, 0, 0},
    {9, 10, 0, 0}, {10, 9, 0, 0}, {11, 12, 0, 0}, {12, 11, 0, 0},
    {13, 14, 0, 0}, {14, 13, 0, 0}, {15, 16, 0, 0}, {16, 15, 0, 0}};

uint8_t interlocked_buttons[] = {1, 2, 3, 4, 5, 6, 8};

struct Pulse
{
  uint8_t relay_num;
  uint8_t timeout;
  uint16_t counter;
};

Pulse pulsed_relays[MAX_RELAYS] = {
    {1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {4, 1, 0},
    {5, 1, 0}, {6, 1, 0}, {7, 1, 0}, {8, 1, 0},
    {9, 1, 0}, {10, 1, 0}, {11, 1, 0}, {12, 1, 0},
    {13, 1, 0}, {14, 1, 0}, {15, 1, 0}, {16, 1, 0}};

void applyHardwareVariantPinsAndModes()
{
  if (hardwareVariant.length() == 0)
  {
    relayCount = 0;
    Serial.println("Hardware variant not configured — relay outputs disabled");
    return;
  }

  // Load hardware config from filesystem (falls back to hardcoded defaults)
  loadBoardHardware(hardwareVariant);

  relayCount       = activeBoardHardware.relayCount;
  useShiftRegister = (activeBoardHardware.outputType == BOARD_OUTPUT_SHIFTREGISTER);
  onboard_led.pin  = activeBoardHardware.ledPin;

  for (uint8_t i = 0; i < MAX_RELAYS; i++)
    relays[i].pin = 255;

  if (!useShiftRegister)
  {
    for (uint8_t i = 0; i < relayCount && i < MAX_RELAYS; i++)
      relays[i].pin = activeBoardHardware.relayPins[i];
  }

  if (hardwareVariant == kVariant16Relay)
  {
    for (uint8_t i = 0; i < MAX_RELAYS; i++)
    {
      latched_relays[i].relay_num = i + 1;
      latched_relays[i].latched_num = ((i % 2) == 0) ? (i + 2) : i;
      latched_relays[i].timeout = 0;
      latched_relays[i].counter = 0;
      pulsed_relays[i].relay_num = i + 1;
      pulsed_relays[i].timeout = 1;
      pulsed_relays[i].counter = 0;
    }
  }
  else
  {
    hardwareVariant = kVariant8Relay;

    uint8_t latchedPair[8] = {0, 0, 0, 0, 6, 5, 8, 7};
    uint8_t latchedTimeout[8] = {0, 2, 0, 0, 0, 0, 0, 0};
    uint8_t pulseTimeout[8] = {1, 1, 0, 0, 0, 0, 0, 0};
    for (uint8_t i = 0; i < 8; i++)
    {
      latched_relays[i].relay_num = i + 1;
      latched_relays[i].latched_num = latchedPair[i];
      latched_relays[i].timeout = latchedTimeout[i];
      latched_relays[i].counter = 0;
      pulsed_relays[i].relay_num = i + 1;
      pulsed_relays[i].timeout = pulseTimeout[i];
      pulsed_relays[i].counter = 0;
    }
    for (uint8_t i = 8; i < MAX_RELAYS; i++)
    {
      latched_relays[i].relay_num = i + 1;
      latched_relays[i].latched_num = 0;
      latched_relays[i].timeout = 0;
      latched_relays[i].counter = 0;
      pulsed_relays[i].relay_num = i + 1;
      pulsed_relays[i].timeout = 0;
      pulsed_relays[i].counter = 0;
    }
  }
}


void handleSerialCommand(const String &rawCommand)
{
  String command = rawCommand;
  command.trim();
  command.toLowerCase();

  if (command.length() == 0)
  {
    return;
  }

  if (command == "reset_wifi")
  {
    Serial.println("reset_wifi received.");
    Serial.println("Confirm clear Wi-Fi credentials and start serial Wi-Fi setup? [y/N]");
    String confirmation = readSerialLineBlocking();
    confirmation.trim();
    confirmation.toLowerCase();

    if (!(confirmation == "y" || confirmation == "yes"))
    {
      Serial.println("Cancelled. Wi-Fi credentials were not changed.");
      return;
    }

    Serial.println("Clearing Wi-Fi credentials.");
    wifiSsid = "";
    wifiPassword = "";

    if (saveBoardConfig())
    {
      Serial.println("Wi-Fi credentials cleared.");
      Serial.println("Starting serial Wi-Fi setup...");

      if (runSerialWiFiProvisioningWizard())
      {
        Serial.println("Wi-Fi credentials saved. Restarting...");
        pendingRestart = true;
        pendingRestartAt = millis() + 1000;
      }
      else
      {
        Serial.println("Serial Wi-Fi setup failed or cancelled. Credentials remain blank.");
      }
    }
    else
    {
      Serial.println("Failed to clear Wi-Fi credentials.");
    }
    return;
  }

  if (command == "help")
  {
    Serial.println("Available commands: reset_wifi, help");
    return;
  }

  Serial.printf("Unknown command: %s\n", command.c_str());
}

void processSerialCommands()
{
  while (Serial.available() > 0)
  {
    char c = (char)Serial.read();
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      handleSerialCommand(serialCommandBuffer);
      serialCommandBuffer = "";
      continue;
    }

    if (serialCommandBuffer.length() < 127)
    {
      serialCommandBuffer += c;
    }
  }
}

int parseRelayNumberFromCommand(const String &command)
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
  // Keep this comfortably above worst-case state payload (16 relays + labels + board config).
  DynamicJsonDocument JSONdoc(12288);

  Serial.println("notifying clients");

  JsonArray array = JSONdoc.createNestedArray("buttons");
  for (int i = 0; i < relayCount; i++)
  {
    JsonObject button = array.createNestedObject();
    button["id"]           = i + 1;
    button["on"]           = relays[i].on;
    button["disabled"]     = relays[i].disabled;
    button["last"]         = relays[i].last;
    button["onLabel"]      = relayLabels[i].on;
    button["offLabel"]     = relayLabels[i].off;
    button["mode"]         = relayLabels[i].mode;
    button["group"]        = relayLabels[i].group;
    button["pulseTimeout"] = relayLabels[i].pulseTimeout;
    button["gpio"]         = (relays[i].pin == 255) ? -1 : (int)relays[i].pin;
  }
  JSONdoc["boardName"] = boardName;
  JSONdoc["useStaticIp"] = useStaticIp;
  JSONdoc["useDhcp"] = !useStaticIp;

  String activeIp = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
  String activeDns = useStaticIp ? boardDns.toString() : WiFi.dnsIP().toString();
  String activeGateway = useStaticIp ? boardGateway.toString() : WiFi.gatewayIP().toString();
  String activeSubnet = useStaticIp ? boardSubnet.toString() : WiFi.subnetMask().toString();

  // Always publish network fields so the config page can populate defaults/placeholders.
  JSONdoc["ipAddress"] = activeIp;
  JSONdoc["dns"] = activeDns;
  JSONdoc["gateway"] = activeGateway;
  JSONdoc["subnet"] = activeSubnet;
  JSONdoc["doDelay"] = doDelay;
  JSONdoc["startupDelaySeconds"] = startupDelaySeconds;
  JSONdoc["doLatched"] = doLatched;
  JSONdoc["doInterlocked"] = doInterlocked;
  JSONdoc["doPulsed"] = doPulsed;
  JSONdoc["setupComplete"]      = (relayCount > 0 && hardwareVariant.length() > 0);
  JSONdoc["hardwareVariant"]    = hardwareVariant;
  JSONdoc["relayCount"]         = relayCount;
  JSONdoc["boardHardwareFile"]  = boardHardwarePath(hardwareVariant);
  JSONdoc["boardHardwareName"]  = activeBoardHardware.name;
#if defined(ESP8266)
  JSONdoc["mcuType"] = "ESP8266";
#elif defined(ESP32)
  JSONdoc["mcuType"] = "ESP32";
#else
  JSONdoc["mcuType"] = "Unknown";
#endif

  String payload;
  serializeJson(JSONdoc, payload);

  //Serial.println(payload);
  ws.textAll(payload);
}

void writeRelaysToShiftRegister()
{
  // set the output data bytes based on the relay state array
  for (int i = 0; i < relayCount; i++)
  {
    int regNum = i / 8;
    int bitNum = i % 8;
    if (relays[i].state())
    {
      bitSet(outputData[regNum], bitNum);
    }
    else
    {
      bitClear(outputData[regNum], bitNum);
    }
  }

  digitalWrite(activeBoardHardware.srLatchPin, LOW);
  for (int i = numRegisters; i > 0; i--)
  {
    shiftOut(activeBoardHardware.srDataPin, activeBoardHardware.srClockPin, MSBFIRST, outputData[i - 1]);
  }
  digitalWrite(activeBoardHardware.srDataPin,  LOW);
  digitalWrite(activeBoardHardware.srClockPin, LOW);
  digitalWrite(activeBoardHardware.srLatchPin, HIGH);
}

void handleLatch(uint8_t _relayNum)
{
  uint8_t index;

  if ((_relayNum > 0) && (_relayNum <= relayCount))
  {
    index = _relayNum - 1;
    if (latched_relays[index].timeout != 0)
    {
      if ((latched_relays[index].relay_num > 0) && (latched_relays[index].relay_num <= relayCount))
      {
        relays[index].disabled = 1;
        latched_relays[index].counter = (latched_relays[index].timeout * DELAY_COUNTER);
      }
      if ((latched_relays[index].latched_num > 0) && (latched_relays[index].latched_num <= relayCount))
      {
        relays[latched_relays[index].latched_num - 1].disabled = 1;
        latched_relays[latched_relays[index].latched_num - 1].counter = (latched_relays[index].timeout * DELAY_COUNTER);
      }
    }
  }
}

void handleInterlock(uint8_t _relayNum)
{
  uint8_t i, index;
  index = _relayNum - 1;
  bool doit = false;

  if ((_relayNum > 0) && (_relayNum <= relayCount))
  {
    for (i = 0; i < sizeof(interlocked_buttons); i++)
    {
      if (interlocked_buttons[i] == _relayNum)
      {
        doit = true;
      }
    }
    if (doit)
    {
      // check if the relay is turning on
      if (relays[index].on == 0)
      {
        for (i = 0; i < sizeof(interlocked_buttons); i++)
        {
          if (interlocked_buttons[i] != _relayNum)
          {
            relays[interlocked_buttons[i] - 1].low();
            relays[interlocked_buttons[i] - 1].update();
          }
        }
      }
    }
  }
}

void handlePulsed(uint8_t _relayNum)
{
  uint8_t i;
  uint8_t index;

  if ((_relayNum > 0) && (_relayNum <= relayCount))
  {
    if (pulsed_relays[_relayNum - 1].timeout != 0)
    {
      for (i = 1; i <= relayCount; i++)
      {
        index = i - 1;
        if (pulsed_relays[index].timeout != 0)
        {
          if (i == _relayNum)
          {
            if ((pulsed_relays[index].relay_num > 0) && (pulsed_relays[index].relay_num <= relayCount))
            {
              relays[index].last = 1;
              pulsed_relays[index].counter = (pulsed_relays[index].timeout * DELAY_COUNTER);
            }
          }
          else
          {
            relays[index].last = 0;
          }
        }
      }
    }
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  char buffer[len + 1];
  String str;

  Serial.printf("len=%d\n", len);
  snprintf(buffer, len + 1, "%s", data); // convert char array to null terminated
  Serial.printf("handle web socket message: %s\n", buffer);
  str = buffer;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    bool notify = false;
    Serial.println(str);
    int relayNum;

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
          if ((relayNum > 0) && (relayNum <= relayCount))
          {
            assignRelayLabels(relayNum, String(command["on"] | ""), String(command["off"] | ""));
            if (command.containsKey("mode"))
            {
              String modeStr = String(command["mode"] | "onoff");
              uint8_t mode   = RELAY_MODE_ONOFF;
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
              if ((relayNum > 0) && (relayNum <= relayCount))
              {
                assignRelayLabels(relayNum, String(label["on"] | ""), String(label["off"] | ""));
                String modeStr = String(label["mode"] | "onoff");
                uint8_t mode   = RELAY_MODE_ONOFF;
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
            Serial.println("Switched to DHCP mode. Restarting...");
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
              Serial.printf("Switched to static IP: %s. Restarting...\n", ipStr.c_str());
              delay(1000);
              ESP.restart();
            }
            else
            {
              Serial.println("Invalid IP address configuration");
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
      else
      {
        Serial.printf("Invalid websocket JSON payload (%s)\n", error.c_str());
      }
    }
    else if (str == "home")
    {
      // do nothing, just refresh
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
      if ((relayNum > 0) && (relayNum <= relayCount))
      {
        uint8_t idx   = relayNum - 1;
        uint8_t rmode = relayLabels[idx].mode;

        if (rmode == RELAY_MODE_PULSED && relays[idx].disabled)
        {
          // Relay is locked mid-pulse — ignore press, just refresh client
          notify = true;
        }
        else if (rmode == RELAY_MODE_INTERLOCKED)
        {
          uint8_t grp      = relayLabels[idx].group;
          bool    turningOn = !relays[idx].on;
          relays[idx].toggle();
          relays[idx].update();
          if (turningOn && grp > 0)
          {
            for (uint8_t j = 0; j < relayCount; j++)
            {
              if (j != idx &&
                  relayLabels[j].mode == RELAY_MODE_INTERLOCKED &&
                  relayLabels[j].group == grp)
              {
                relays[j].low();
                relays[j].update();
              }
            }
          }
          notify = true;
        }
        else if (rmode == RELAY_MODE_PULSED)
        {
          uint8_t pt = relayLabels[idx].pulseTimeout;
          if (pt == 0 || pt > 30) pt = 1;
          relays[idx].high();
          relays[idx].update();
          relays[idx].disabled           = 1;
          pulsed_relays[idx].counter     = (uint32_t)pt * DELAY_COUNTER;
          notify = true;
        }
        else
        {
          // RELAY_MODE_ONOFF: standard toggle with global-mode handling
          if (doLatched)
            handleLatch(relayNum);
          if (doInterlocked)
            handleInterlock(relayNum);
          if (doPulsed)
            handlePulsed(relayNum);
          relays[idx].toggle();
          relays[idx].update();
          notify = true;
        }
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
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  Serial.println("on event");
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
  Serial.println("init web socket");
  ws.onEvent(onEvent);
}

// Locking to a specific BSSID can hurt stability on mesh/multi-AP networks.
// Keep this off by default; enable only if you want fixed-AP behavior.
static const bool LOCK_TO_STRONGEST_BSSID = false;

#if defined(ESP32) || defined(ESP8266)
bool beginWiFiOnStrongestBssid(const char *targetSsid, const char *targetPassword)
{
  int scanCount = WiFi.scanNetworks(false, true);
  if (scanCount <= 0)
  {
    Serial.println("WiFi scan found no APs, falling back to default connect");
    return false;
  }

  int bestIndex = -1;
  int bestRssi = -1000;
  String desiredSsid = String(targetSsid);

  for (int i = 0; i < scanCount; i++)
  {
    if (WiFi.SSID(i) == desiredSsid)
    {
      int rssi = WiFi.RSSI(i);
      if (bestIndex < 0 || rssi > bestRssi)
      {
        bestIndex = i;
        bestRssi = rssi;
      }
    }
  }

  if (bestIndex < 0)
  {
    Serial.printf("SSID '%s' not found in scan, falling back to default connect\n", targetSsid);
    WiFi.scanDelete();
    return false;
  }

  uint8_t bestBssid[6];
  memcpy(bestBssid, WiFi.BSSID(bestIndex), sizeof(bestBssid));
  int bestChannel = WiFi.channel(bestIndex);
  String bestBssidStr = WiFi.BSSIDstr(bestIndex);

  Serial.printf("Connecting to strongest AP for '%s': BSSID=%s ch=%d RSSI=%d dBm\n",
                targetSsid, bestBssidStr.c_str(), bestChannel, bestRssi);

  WiFi.scanDelete();
  WiFi.begin(targetSsid, targetPassword, bestChannel, bestBssid, true);
  return true;
}
#endif

void initWiFi()
{
  int xc = 0;
  int attempts = 1;
  Serial.printf("\n\n");
  if (doDelay)
  {
    uint32_t delayIterations = (uint32_t)startupDelaySeconds * 2;
    Serial.printf("Delaying start %u seconds ", startupDelaySeconds);
    while (xc < delayIterations)
    {
      onboard_led.on = !onboard_led.on;
      onboard_led.update();
      if ((xc % 2) == 0)
      {
        Serial.print(".");
      }
      delay(500);
      xc++;
    }
    onboard_led.on = !onboard_led.on_state;
    onboard_led.update();
  }
  Serial.println("*");

  if (useStaticIp)
  {
    Serial.printf("Configuring static IP: %s\n", boardIp.toString().c_str());
    WiFi.config(boardIp, boardDns, boardGateway, boardSubnet);
  }
  else
  {
    Serial.println("Using DHCP");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

// make ESP32 as responsive as the ESP8266 by turning off WiFi power saving
#if defined(ESP32)
  esp_wifi_set_ps(WIFI_PS_NONE);
#elif defined(ESP8266)
  // Disable modem sleep to reduce latency spikes and websocket disconnects.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

  while ((WiFi.status() != WL_CONNECTED) && (attempts <= 5))
  {
    WiFi.disconnect();
#if defined(ESP32) || defined(ESP8266)
    if (LOCK_TO_STRONGEST_BSSID && !beginWiFiOnStrongestBssid(wifiSsid.c_str(), wifiPassword.c_str()))
    {
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
    else if (!LOCK_TO_STRONGEST_BSSID)
    {
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
#else
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
#endif
    Serial.printf("Trying to connect [%s], attempt %d.", WiFi.macAddress().c_str(), attempts);

    xc = 0;
    attempts++;
    while ((WiFi.status() != WL_CONNECTED) && (xc < 60))
    {
      Serial.print(".");
      delay(500);
      xc++;
    }
    Serial.println("*");
  }

  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Status=WL_CONNECTED");
    Serial.print("CONNECTED To: ");
    Serial.println(wifiSsid);
    Serial.print("IP Address: http://");
    Serial.println(WiFi.localIP().toString().c_str());
    WiFi.softAPdisconnect(true);
  }
  else
  {
    WiFi.disconnect();
    Serial.println("Status= NOT WL_CONNECTED");
    Serial.println("Rebooting....");
    delay(500);
    ESP.restart();
  }
}



void setup()
{
  uint8_t i;

  // Serial port for debugging purposes
  Serial.begin(115200);

  // Mount Filesystem
  initLittleFS();
  bool labelsFound = loadRelayLabels();
  loadBoardConfig();
  applyHardwareVariantPinsAndModes(); // also loads board hardware config

  if (!labelsFound && relayCount > 0)
    loadLabelsFromTemplate(relayCount);

  // LED pin is now sourced from board hardware config
  if (onboard_led.pin != 255)
    pinMode(onboard_led.pin, OUTPUT);
  onboard_led.on = !onboard_led.on_state;
  onboard_led.update();

  // initialise relays
  for (i = 0; i < MAX_RELAYS; i++)
  {
    relays[i].low();
    relays[i].disabled = 0;
    relays[i].last = 0;
    if (!useShiftRegister && i < relayCount && relays[i].pin != 255)
    {
      pinMode(relays[i].pin, OUTPUT);
    }
    relays[i].update();
  }

  if (useShiftRegister)
  {
    digitalWrite(activeBoardHardware.srOePin,    HIGH); // disable output
    pinMode(activeBoardHardware.srOePin,    OUTPUT);

    digitalWrite(activeBoardHardware.srLatchPin, HIGH); // latch idles high
    pinMode(activeBoardHardware.srLatchPin, OUTPUT);

    digitalWrite(activeBoardHardware.srClockPin, LOW);  // clock idles low
    pinMode(activeBoardHardware.srClockPin, OUTPUT);

    digitalWrite(activeBoardHardware.srDataPin,  LOW);  // data idles low
    pinMode(activeBoardHardware.srDataPin,  OUTPUT);

    writeRelaysToShiftRegister();

    digitalWrite(activeBoardHardware.srOePin,    LOW);  // enable output
  }

  if (wifiSsid.length() == 0)
  {
    startProvisioningPortal();
    Serial.println("You can also provision via serial command: reset_wifi");
    return;
  }

  // Connect to Wi-Fi
  initWiFi();

  initWebSocket();
  server.addHandler(&ws);

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.println("on /");
    request->send(LittleFS, useShiftRegister ? "/index16.html" : "/index.html", "text/html",false,nullptr); });

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

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    auto getBodyParam = [request](const char *name) -> String {
      if (request->hasParam(name, true))
      {
        return request->getParam(name, true)->value();
      }
      return "";
    };

    auto parseBool = [](String value, bool defaultValue) -> bool {
      value.toLowerCase();
      value.trim();
      if (value == "1" || value == "true" || value == "on" || value == "yes")
        return true;
      if (value == "0" || value == "false" || value == "off" || value == "no")
        return false;
      return defaultValue;
    };

    String name = getBodyParam("name");
    name.trim();
    if (name.length() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"name required\"}");
      return;
    }
    if (name.length() > kMaxBoardNameLength)
    {
      name = name.substring(0, kMaxBoardNameLength);
    }

    bool oldDoDelay = doDelay;
    uint16_t oldStartupDelaySeconds = startupDelaySeconds;
    bool oldDoLatched = doLatched;
    bool oldDoInterlocked = doInterlocked;
    bool oldDoPulsed = doPulsed;
    String oldHardwareVariant = hardwareVariant;

    boardName = name;
    doDelay = parseBool(getBodyParam("doDelay"), doDelay);
    String delaySecondsStr = getBodyParam("delaySeconds");
    if (delaySecondsStr.length() > 0)
    {
      int parsedDelaySeconds = delaySecondsStr.toInt();
      if (parsedDelaySeconds < 0)
        parsedDelaySeconds = 0;
      if (parsedDelaySeconds > 300)
        parsedDelaySeconds = 300;
      startupDelaySeconds = (uint16_t)parsedDelaySeconds;
    }
    doLatched = parseBool(getBodyParam("doLatched"), doLatched);
    doInterlocked = parseBool(getBodyParam("doInterlocked"), doInterlocked);
    doPulsed = parseBool(getBodyParam("doPulsed"), doPulsed);

    String requestedVariant = getBodyParam("hardwareVariant");
    requestedVariant.trim();
    requestedVariant.toLowerCase();
    if (requestedVariant.length() == 0)
    {
      requestedVariant = hardwareVariant;
    }
    if (requestedVariant != kVariant8Relay && requestedVariant != kVariant16Relay)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid hardware variant\"}");
      return;
    }
    hardwareVariant = requestedVariant;
    applyHardwareVariantPinsAndModes();

    bool restartNeeded = false;
    bool useDhcp = parseBool(getBodyParam("useDhcp"), !useStaticIp);

    if (useDhcp)
    {
      if (useStaticIp)
      {
        restartNeeded = true;
      }
      useStaticIp = false;
    }
    else
    {
      String ipStr = getBodyParam("ip");
      String dnsStr = getBodyParam("dns");
      String gatewayStr = getBodyParam("gateway");
      String subnetStr = getBodyParam("subnet");

      IPAddress newIp, newDns, newGateway, newSubnet;
      if (ipStr.length() == 0 || dnsStr.length() == 0 || gatewayStr.length() == 0 || subnetStr.length() == 0 ||
          !newIp.fromString(ipStr) || !newDns.fromString(dnsStr) || !newGateway.fromString(gatewayStr) || !newSubnet.fromString(subnetStr))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid ip config\"}");
        return;
      }

      if (!useStaticIp || boardIp != newIp || boardDns != newDns || boardGateway != newGateway || boardSubnet != newSubnet)
      {
        restartNeeded = true;
      }

      boardIp = newIp;
      boardDns = newDns;
      boardGateway = newGateway;
      boardSubnet = newSubnet;
      useStaticIp = true;
    }

    if (doDelay != oldDoDelay || doLatched != oldDoLatched ||
        doInterlocked != oldDoInterlocked || doPulsed != oldDoPulsed ||
        startupDelaySeconds != oldStartupDelaySeconds)
    {
      restartNeeded = true;
    }

    if (hardwareVariant != oldHardwareVariant)
    {
      restartNeeded = true;
    }

    Serial.printf("/api/config: Attempting to save (name=%s, dhcp=%d, delay=%d, delaySeconds=%u, latched=%d, interlocked=%d, pulsed=%d, variant=%s)\n",
            name.c_str(), useDhcp, doDelay, startupDelaySeconds, doLatched, doInterlocked, doPulsed, hardwareVariant.c_str());

    if (!saveBoardConfig())
    {
      Serial.println("ERROR: /api/config - saveBoardConfig() failed");
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    Serial.println("/api/config: Save successful, notifying clients");
    notifyClients();

    DynamicJsonDocument response(128);
    response["ok"] = true;
    response["restart"] = restartNeeded;
    response["useDhcp"] = !useStaticIp;
    response["appliedIp"] = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);

    if (restartNeeded)
    {
      pendingRestart = true;
      pendingRestartAt = millis() + 1200;
    } });

  server.on("/api/clearwifi", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    bool confirmed = false;
    if (request->hasParam("confirm", true))
    {
      String value = request->getParam("confirm", true)->value();
      value.toLowerCase();
      value.trim();
      confirmed = (value == "1" || value == "true" || value == "yes" || value == "y");
    }

    if (!confirmed)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"confirmation required\"}");
      return;
    }

    wifiSsid = "";
    wifiPassword = "";

    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    pendingRestart = true;
    pendingRestartAt = millis() + 1200; });

  server.on("/api/labels", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    auto getBodyParam = [request](const char *name) -> String {
      if (request->hasParam(name, true))
      {
        return request->getParam(name, true)->value();
      }
      return "";
    };

    bool changed = false;
    for (uint8_t relayNum = 1; relayNum <= relayCount; relayNum++)
    {
      String prefix   = "relay" + String(relayNum);
      String onLabel  = getBodyParam((prefix + "_on").c_str());
      String offLabel = getBodyParam((prefix + "_off").c_str());
      assignRelayLabels(relayNum, onLabel, offLabel);

      String modeStr  = getBodyParam((prefix + "_mode").c_str());
      uint8_t mode    = RELAY_MODE_ONOFF;
      if (modeStr == "interlocked") mode = RELAY_MODE_INTERLOCKED;
      else if (modeStr == "pulsed") mode = RELAY_MODE_PULSED;
      uint8_t group   = (uint8_t)getBodyParam((prefix + "_group").c_str()).toInt();
      uint8_t pt      = (uint8_t)getBodyParam((prefix + "_pulseTimeout").c_str()).toInt();
      assignRelayMode(relayNum, mode, group, pt);
      changed = true;
    }

    if (!changed)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"no labels provided\"}");
      return;
    }

    Serial.println("/api/labels: saving relay labels");
    if (!saveRelayLabels())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    notifyClients();

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/templates", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("templates");

    if (LittleFS.exists("/templates")) {
#if defined(ESP8266)
      Dir dir = LittleFS.openDir("/templates");
      while (dir.next()) {
        if (!dir.isFile()) continue;
        String fname = dir.fileName();
        if (!fname.endsWith(".json")) continue;
        File f = dir.openFile("r");
        DynamicJsonDocument td(2048);
        if (deserializeJson(td, f) == DeserializationError::Ok) {
          JsonObject t = arr.createNestedObject();
          t["filename"] = fname;
          t["title"] = td["title"] | fname.c_str();
          t["relayCount"] = td["relayCount"] | 8;
        }
        f.close();
      }
#elif defined(ESP32)
      File root = LittleFS.open("/templates");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
          if (!f.isDirectory()) {
            String fname = String(f.name());
            int slash = fname.lastIndexOf('/');
            if (slash >= 0) fname = fname.substring(slash + 1);
            if (fname.endsWith(".json")) {
              DynamicJsonDocument td(2048);
              if (deserializeJson(td, f) == DeserializationError::Ok) {
                JsonObject t = arr.createNestedObject();
                t["filename"] = fname;
                t["title"] = td["title"] | fname.c_str();
                t["relayCount"] = td["relayCount"] | 8;
              }
            }
          }
          f = root.openNextFile();
        }
      }
#endif
    }

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/templates", HTTP_POST, [](AsyncWebServerRequest *request) {
    auto getBodyParam = [request](const char *name) -> String {
      if (request->hasParam(name, true)) {
        return request->getParam(name, true)->value();
      }
      return "";
    };

    String title = getBodyParam("title");
    title.trim();
    if (title.length() == 0) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"title required\"}");
      return;
    }
    if (title.length() > 64) title = title.substring(0, 64);

    int rc = getBodyParam("relayCount").toInt();
    if (rc <= 0 || rc > 16) rc = (int)relayCount;

    // Build a safe filename from the title
    String filename = "";
    for (size_t i = 0; i < title.length() && filename.length() < 48; i++) {
      char c = title[i];
      if (isAlphaNumeric(c)) {
        filename += (char)tolower(c);
      } else if ((c == ' ' || c == '-' || c == '_') &&
                 filename.length() > 0 &&
                 filename[filename.length() - 1] != '-') {
        filename += '-';
      }
    }
    while (filename.length() > 0 && filename[filename.length() - 1] == '-') {
      filename = filename.substring(0, filename.length() - 1);
    }
    if (filename.length() == 0) filename = "template";
    filename += ".json";

    if (!LittleFS.exists("/templates")) {
      LittleFS.mkdir("/templates");
    }

    DynamicJsonDocument doc(4096);
    doc["title"] = title;
    doc["relayCount"] = rc;
    JsonArray labels = doc.createNestedArray("labels");
    for (int i = 1; i <= rc; i++) {
      String prefix   = "relay" + String(i);
      String onLabel  = getBodyParam((prefix + "_on").c_str());
      String offLabel = getBodyParam((prefix + "_off").c_str());
      onLabel.trim();
      offLabel.trim();

      String modeStr  = getBodyParam((prefix + "_mode").c_str());
      if (modeStr != "interlocked" && modeStr != "pulsed") modeStr = "onoff";
      uint8_t group   = (uint8_t)getBodyParam((prefix + "_group").c_str()).toInt();
      uint8_t pt      = (uint8_t)getBodyParam((prefix + "_pulseTimeout").c_str()).toInt();
      if (pt == 0 || pt > 30) pt = 1;

      JsonObject label    = labels.createNestedObject();
      label["on"]          = onLabel;
      label["off"]         = offLabel;
      label["mode"]        = modeStr;
      label["group"]       = group;
      label["pulseTimeout"] = pt;
    }

    File f = LittleFS.open("/templates/" + filename, "w");
    if (!f) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }
    serializeJson(doc, f);
    f.close();
    Serial.printf("Saved template: /templates/%s\n", filename.c_str());

    DynamicJsonDocument response(256);
    response["ok"] = true;
    response["filename"] = filename;
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);
  });

  // ── GET /api/boards ─────────────────────────────────────────────────────
  server.on("/api/boards", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    doc["activeBoardFile"] = boardHardwarePath(hardwareVariant);

    JsonArray arr = doc.createNestedArray("boards");

#if defined(ESP8266)
    const char *variants[] = {"8relay", "16relay"};
#else
    const char *variants[] = {"8relay", "16relay"};
#endif

    for (int vi = 0; vi < 2; vi++) {
      String path = boardHardwarePath(String(variants[vi]));
      if (!LittleFS.exists(path)) continue;

      File f = LittleFS.open(path, "r");
      if (!f) continue;

      DynamicJsonDocument entry(1024);
      if (deserializeJson(entry, f) != DeserializationError::Ok) { f.close(); continue; }
      f.close();

      JsonObject board = arr.createNestedObject();
      board["filename"]   = path.substring(1); // strip leading /
      board["name"]       = entry["name"]       | "";
      board["cpu"]        = entry["cpu"]        | "";
      board["relayCount"] = entry["relayCount"] | 0;
      board["outputType"] = entry["outputType"] | "gpio";
    }

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  // ── POST /api/boards ────────────────────────────────────────────────────
  server.on("/api/boards", HTTP_POST, [](AsyncWebServerRequest *request) {
    auto getP = [request](const char *name) -> String {
      return request->hasParam(name, true)
             ? request->getParam(name, true)->value() : String();
    };

    String variant    = getP("variant");   // "8relay" or "16relay"
    String outputType = getP("outputType");
    if (variant != "8relay" && variant != "16relay") {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid variant\"}");
      return;
    }

    // Load existing config so unchanged fields stay intact
    BoardHardware tmp;
    tmp.loaded = false;
    {
      String path = boardHardwarePath(variant);
      if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        DynamicJsonDocument d(2048);
        if (f && deserializeJson(d, f) == DeserializationError::Ok) {
          tmp.name       = d["name"]       | "";
          tmp.cpu        = d["cpu"]        | BOARD_CPU_TYPE;
          tmp.relayCount = d["relayCount"] | (variant == "16relay" ? 16 : 8);
          tmp.ledPin     = d["ledPin"]     | 2;
          String ot      = String(d["outputType"] | "gpio");
          tmp.outputType = (ot == "shiftregister") ? BOARD_OUTPUT_SHIFTREGISTER : BOARD_OUTPUT_GPIO;
          for (uint8_t i = 0; i < 16; i++) tmp.relayPins[i] = 255;
          JsonArray relays = d["relays"].as<JsonArray>();
          if (!relays.isNull()) {
            for (JsonObject r : relays) {
              uint8_t n = r["relay"] | 0;
              if (n >= 1 && n <= 16) tmp.relayPins[n-1] = r["pin"] | (uint8_t)255;
            }
          }
          JsonObject sr = d["shiftRegister"];
          tmp.srLatchPin = sr.isNull() ? 12 : (uint8_t)(sr["latchPin"] | 12);
          tmp.srClockPin = sr.isNull() ? 13 : (uint8_t)(sr["clockPin"] | 13);
          tmp.srDataPin  = sr.isNull() ? 14 : (uint8_t)(sr["dataPin"]  | 14);
          tmp.srOePin    = sr.isNull() ?  5 : (uint8_t)(sr["oePin"]    |  5);
          tmp.loaded = true;
        }
        if (f) f.close();
      }
    }

    // Apply submitted values
    if (getP("name").length())       tmp.name       = getP("name");
    if (getP("ledPin").length())     tmp.ledPin     = (uint8_t)getP("ledPin").toInt();
    if (outputType == "gpio" || outputType == "shiftregister")
      tmp.outputType = (outputType == "shiftregister") ? BOARD_OUTPUT_SHIFTREGISTER : BOARD_OUTPUT_GPIO;

    if (tmp.outputType == BOARD_OUTPUT_GPIO) {
      uint8_t rc = (variant == "16relay") ? 16 : 8;
      for (uint8_t i = 1; i <= rc; i++) {
        String key = "relay" + String(i) + "_pin";
        if (getP(key.c_str()).length())
          tmp.relayPins[i-1] = (uint8_t)getP(key.c_str()).toInt();
      }
    } else {
      if (getP("sr_latchPin").length()) tmp.srLatchPin = (uint8_t)getP("sr_latchPin").toInt();
      if (getP("sr_clockPin").length()) tmp.srClockPin = (uint8_t)getP("sr_clockPin").toInt();
      if (getP("sr_dataPin").length())  tmp.srDataPin  = (uint8_t)getP("sr_dataPin").toInt();
      if (getP("sr_oePin").length())    tmp.srOePin    = (uint8_t)getP("sr_oePin").toInt();
    }

    // Write to LittleFS
    DynamicJsonDocument doc(2048);
    doc["name"]       = tmp.name;
    doc["cpu"]        = tmp.cpu.length() ? tmp.cpu : String(BOARD_CPU_TYPE);
    doc["relayCount"] = (variant == "16relay") ? 16 : 8;
    doc["ledPin"]     = tmp.ledPin;
    doc["outputType"] = (tmp.outputType == BOARD_OUTPUT_SHIFTREGISTER) ? "shiftregister" : "gpio";

    if (tmp.outputType == BOARD_OUTPUT_GPIO) {
      uint8_t rc = doc["relayCount"].as<uint8_t>();
      JsonArray relays = doc.createNestedArray("relays");
      for (uint8_t i = 0; i < rc; i++) {
        JsonObject r = relays.createNestedObject();
        r["relay"] = i + 1;
        r["pin"]   = tmp.relayPins[i];
      }
    } else {
      JsonObject sr   = doc.createNestedObject("shiftRegister");
      sr["latchPin"]  = tmp.srLatchPin;
      sr["clockPin"]  = tmp.srClockPin;
      sr["dataPin"]   = tmp.srDataPin;
      sr["oePin"]     = tmp.srOePin;
    }

    if (!LittleFS.exists("/boards")) LittleFS.mkdir("/boards");
    String path = boardHardwarePath(variant);
    File f = LittleFS.open(path, "w");
    if (!f) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"write failed\"}");
      return;
    }
    serializeJsonPretty(doc, f);
    f.close();

    // If the saved config is for the currently active variant, reload it
    if (variant == hardwareVariant) {
      loadBoardHardware(hardwareVariant);
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.serveStatic("/", LittleFS, "/");

  // Start server
  Serial.println("starting server");

  server.begin();

  /**
   * Enable OTA update
   */
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "firmware";
    } 
    else 
    { // U_FS
      type = "filesystem";
    }
        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    LittleFS.end();
    Serial.println("Start updating " + type); 
    // Disable client connections    
    ws.enable(false);

    // Close them
    ws.closeAll(); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  Serial.printf("OTA hostname: %s\n", OTA_HOSTNAME);
  ArduinoOTA.begin();
}

void loop()
{
  if (pendingRestart)
  {
    uint32_t now = millis();
    if ((int32_t)(now - pendingRestartAt) >= 0)
    {
      pendingRestart = false;
      Serial.println("Restarting to apply network configuration");
      ESP.restart();
    }
  }

  if (provisioningMode)
  {
    pollProvisioningScan();

    // Keep provisioning mode lightweight and responsive.
    processSerialCommands();
    delay(2);
    return;
  }

  elapsed = millis();
  if ((elapsed - timer) > 2000)
  {
    onboard_led.on = !onboard_led.on;
    onboard_led.update();
    timer = elapsed;
    if (reportSignalStrength)
    {
      Serial.printf("WiFi Signal Strength: %d\n", WiFi.RSSI());
    }
  }

  bool notify = false;

  bool needsTimerLoop = doLatched || doPulsed;
  if (!needsTimerLoop)
  {
    for (uint8_t j = 0; j < relayCount; j++)
    {
      if (relayLabels[j].mode == RELAY_MODE_PULSED && pulsed_relays[j].counter > 0)
      {
        needsTimerLoop = true;
        break;
      }
    }
  }

  if (needsTimerLoop)
  {
    uint8_t i;

    if ((elapsed - latched_timer) > DELAY_INTERVAL_MS)
    {
      latched_timer = elapsed;

      for (i = 0; i < relayCount; i++)
      {
        if (doLatched)
        {
          if (latched_relays[i].counter)
          {
            latched_relays[i].counter--;
            if (latched_relays[i].counter == 0)
            {
              relays[i].low();
              relays[i].update();
              relays[i].disabled = false;
              notify = true;
            }
          }
        }

        // Global doPulsed handles relays not configured with per-relay pulsed mode
        if (doPulsed && relayLabels[i].mode != RELAY_MODE_PULSED)
        {
          if (pulsed_relays[i].counter)
          {
            pulsed_relays[i].counter--;
            if (pulsed_relays[i].counter == 0)
            {
              relays[i].low();
              relays[i].update();
              notify = true;
            }
          }
        }

        // Per-relay pulsed mode countdown
        if (relayLabels[i].mode == RELAY_MODE_PULSED && pulsed_relays[i].counter)
        {
          pulsed_relays[i].counter--;
          if (pulsed_relays[i].counter == 0)
          {
            relays[i].low();
            relays[i].update();
            relays[i].disabled = 0;
            notify = true;
          }
        }
      }
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

  ws.cleanupClients();

  processSerialCommands();

  // Check for over the air update request and (if present) flash it
  ArduinoOTA.handle();
}
