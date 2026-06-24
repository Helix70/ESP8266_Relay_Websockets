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
#include "serial_provision.h"
#include "storage_utils.h"

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "relay-board"
#endif

uint32_t elapsed = 0;
uint32_t timer = 0;
uint32_t latched_timer = 0;

#define COUNTDOWN_TIMEOUT_MS 5000
uint32_t countdown = 0;

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

struct RelayLabel
{
  String on;
  String off;
};

static const uint8_t MAX_RELAYS = 16;
static const char *kVariant8Relay = "8relay";
static const char *kVariant16Relay = "16relay";

String hardwareVariant = kVariant8Relay;
uint8_t relayCount = 8;
bool useShiftRegister = false;

RelayLabel relayLabels[MAX_RELAYS];

#if defined(ESP8266)
OutputPin onboard_led = {LED_BUILTIN, false, HIGH, false};
#elif defined(ESP32)
OutputPin onboard_led = {23, false, HIGH, false};
#endif

OutputPin relays[MAX_RELAYS] = {
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}};

const int latchPin = 12;
const int clockPin = 13;
const int dataPin = 14;
int oePin = 5;
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
  if (hardwareVariant == kVariant16Relay)
  {
    relayCount = 16;
    useShiftRegister = true;
    for (uint8_t i = 0; i < MAX_RELAYS; i++)
    {
      relays[i].pin = 255;
    }
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
    relayCount = 8;
    useShiftRegister = false;

#if defined(ESP8266)
    uint8_t pins8[8] = {16, 14, 12, 13, 15, 0, 4, 5};
#else
    uint8_t pins8[8] = {32, 33, 25, 26, 27, 14, 12, 13};
#endif
    for (uint8_t i = 0; i < 8; i++)
    {
      relays[i].pin = pins8[i];
    }
    for (uint8_t i = 8; i < MAX_RELAYS; i++)
    {
      relays[i].pin = 255;
    }

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
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["disabled"] = relays[i].disabled;
    button["last"] = relays[i].last;
    button["onLabel"] = relayLabels[i].on;
    button["offLabel"] = relayLabels[i].off;
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
  JSONdoc["hardwareVariant"] = hardwareVariant;
  JSONdoc["relayCount"] = relayCount;

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

  digitalWrite(latchPin, LOW);
  for (int i = numRegisters; i > 0; i--)
  {
    shiftOut(dataPin, clockPin, MSBFIRST, outputData[i - 1]);
  }
  digitalWrite(dataPin, LOW);
  digitalWrite(clockPin, LOW);
  digitalWrite(latchPin, HIGH);
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
              if (label.isNull())
              {
                continue;
              }

              relayNum = label["relay"] | (i + 1);
              if ((relayNum > 0) && (relayNum <= relayCount))
              {
                assignRelayLabels(relayNum, String(label["on"] | ""), String(label["off"] | ""));
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
      }
      notify = true;
    }
    else
    {
      relayNum = parseRelayNumberFromCommand(str);
      if ((relayNum > 0) && (relayNum <= relayCount))
      {
        if (doLatched)
          handleLatch(relayNum);
        if (doInterlocked)
          handleInterlock(relayNum);
        if (doPulsed)
          handlePulsed(relayNum);
        relays[relayNum - 1].toggle();
        relays[relayNum - 1].update();
        notify = true;
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

// String processor(const String &var)
// {
//   Serial.println(var);

//   int relayNum, relayIndex;
//   // check for buttonx or buttonxx
//   if (var.startsWith("button"))
//   {
//       // extract the button number substring(startindex,endindex)
//       relayNum = var.substring(6, var.length()).toInt();
//       if ((relayNum > 0) && (relayNum < relayCount))
//       {
//         relayIndex = relayNum - 1;
//         return relays[relayIndex].on ? "ON" : "OFF";
//       }
//   }

//   return String();
// }

void setup()
{
  uint8_t i;

  // Serial port for debugging purposes
  Serial.begin(115200);

  pinMode(onboard_led.pin, OUTPUT);
  onboard_led.on = !onboard_led.on_state;
  onboard_led.update();

  // Mount Filesystem
  initLittleFS();
  loadRelayLabels();
  loadBoardConfig();
  applyHardwareVariantPinsAndModes();

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
    digitalWrite(oePin, HIGH); // disable output
    pinMode(oePin, OUTPUT);

    digitalWrite(latchPin, HIGH); // latch idles high
    pinMode(latchPin, OUTPUT);

    digitalWrite(clockPin, LOW); // clock idles low
    pinMode(clockPin, OUTPUT);

    digitalWrite(dataPin, LOW); // data idles low
    pinMode(dataPin, OUTPUT);

    writeRelaysToShiftRegister();

    digitalWrite(oePin, LOW); // enable output
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
      String onKey = "relay" + String(relayNum) + "_on";
      String offKey = "relay" + String(relayNum) + "_off";
      String onLabel = getBodyParam(onKey.c_str());
      String offLabel = getBodyParam(offKey.c_str());
      assignRelayLabels(relayNum, onLabel, offLabel);
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

  if (doLatched || doPulsed)
  {
    uint8_t i;

    if ((elapsed - latched_timer) > DELAY_INTERVAL_MS)
    {
      latched_timer = elapsed;

      // handle latched buttons
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

        if (doPulsed)
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
      }  // for loop
    }    // latched_timer
  }      // (doLatched || doPulsed)

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
