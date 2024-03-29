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

#include "credentials.h"

// Replace with your network credentials
// const char* ssid = "YOUR_SSID";
// const char* password = "YOUR_PASSWORD";
// IPAddress ip(192, 168, 1, 65);
// IPAddress dns(192, 168, 1, 1);
// IPAddress gateway(192, 168, 1, 1);
// IPAddress subnet(255, 255, 255, 0);

uint32_t elapsed = 0;
uint32_t timer = 0;
uint32_t latched_timer = 0;

#define COUNTDOWN_TIMEOUT_MS 5000
uint32_t countdown = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
char buffer[2048];

#define DELAY_INTERVAL_MS 50 // check  exery 50ms
#define DELAY_COUNTER (1000 / DELAY_INTERVAL_MS)

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct OutputPin
{
  // state variables
  const uint8_t pin;
  uint8_t on;
  const uint8_t on_state; // LOW active or HIGH active
  uint8_t disabled;
  uint8_t last;

  // methods
  void update()
  {
    if (pin != 255)
    {
      digitalWrite(pin, on ? HIGH : LOW);
    }
  }

  void low()
  {
    on = !on_state;
  }

  void high()
  {
    on = on_state;
  }

  void toggle()
  {
    on = !on;
  }

  uint8_t state()
  {
    return on;
  }
};

#if NUM_RELAYS == 8
#if defined(ESP8266)
OutputPin onboard_led = {LED_BUILTIN, false, HIGH, false};

OutputPin relays[] = {
    {16, false, HIGH, false},
    {14, false, HIGH, false},
    {12, false, HIGH, false},
    {13, false, HIGH, false},
    {15, false, HIGH, false},
    {0, false, HIGH, false},
    {4, false, HIGH, false},
    {5, false, HIGH, false}};
#elif defined(ESP32)
OutputPin onboard_led = {23, false, HIGH, false};

OutputPin relays[] = {
    {32, false, HIGH, false},
    {33, false, HIGH, false},
    {25, false, HIGH, false},
    {26, false, HIGH, false},
    {27, false, HIGH, false},
    {14, false, HIGH, false},
    {12, false, HIGH, false},
    {13, false, HIGH, false}};
#endif
#elif NUM_RELAYS == 16
OutputPin onboard_led = {LED_BUILTIN, false, HIGH, false};

OutputPin relays[] = {
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false},
    {255, false, HIGH, false}};

const int latchPin = 12;       // Latch Pin of 74hc595
const int clockPin = 13;       // Clock  Pin of 74hc595
const int dataPin = 14;        // Data  Pin of 74hc595
int oePin = 5;                 // Oe Pin of 74hc595
const int numRegisters = 2;    // Number of 74hc595 on the board
byte outputData[numRegisters]; // the bytes used to shift out the data

#endif

#if DO_LATCHED == 1
struct Latch
{
  const uint8_t relay_num;
  const uint8_t latched_num;
  const uint8_t timeout;
  uint16_t counter;
};

#if NUM_RELAYS == 8
// relay number, latched relay number, timeout (seconds)
Latch latched_relays[] = {
    {1, 0, 0},  // 1
    {2, 0, 2},  // 2
    {3, 0, 0}, // 3
    {4, 0, 0}, // 4
    {5, 6, 0},  // 5
    {6, 5, 0},  // 6
    {7, 8, 0},  // 7
    {8, 7, 0}   // 8
};
#elif NUM_RELAYS == 16
// relay number, latched relay number, timeout (seconds)
Latch latched_relays[] = {
    {1, 2, 0},   // 1
    {2, 1, 0},   // 2
    {3, 4, 0},   // 3
    {4, 3, 0},   // 4
    {5, 6, 0},   // 5
    {6, 5, 0},   // 6
    {7, 8, 0},   // 7
    {8, 7, 0},   // 8
    {9, 10, 0},  // 9
    {10, 9, 0},  // 10
    {11, 12, 0}, // 11
    {12, 11, 0}, // 12
    {13, 14, 0}, // 13
    {14, 13, 0}, // 14
    {15, 16, 0}, // 15
    {16, 15, 0}  // 16
};
#endif
#endif

#if DO_INTERLOCKED == 1
uint8_t interlocked_buttons[] = {1, 2, 3, 4, 5, 6, 8};
#endif

#if DO_PULSED
struct Pulse
{
  const uint8_t relay_num;
  const uint8_t timeout;
  uint16_t counter;
};

#if NUM_RELAYS == 8
// relay number, timeout (seconds)
Pulse pulsed_relays[] = {
    {1, 1}, // 1
    {2, 1}, // 2
    {3, 0}, // 3
    {4, 0}, // 4
    {5, 0}, // 5
    {6, 0}, // 6
    {7, 0}, // 7
    {8, 0}  // 8
};
#elif NUM_RELAYS == 16
// relay number, timeout (seconds)
Pulse pulsed_relays[] = {
    {1, 1},  // 1
    {2, 1},  // 2
    {3, 1},  // 3
    {4, 1},  // 4
    {5, 1},  // 5
    {6, 1},  // 6
    {7, 1},  // 7
    {8, 1},  // 8
    {9, 1},  // 9
    {10, 1}, // 10
    {11, 1}, // 11
    {12, 1}, // 12
    {13, 1}, // 13
    {14, 1}, // 14
    {15, 1}, // 15
    {16, 1}  // 16
};
#endif
#endif

// ----------------------------------------------------------------------------
// LittleFS initialization
// ----------------------------------------------------------------------------

void initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("Cannot mount LittleFS volume...");
    while (1)
    {
      onboard_led.on = millis() % 200 < 50;
      onboard_led.update();
    }
  }
  Serial.println("LittleFS volume mounted...");
}

void notifyClients()
{
  DynamicJsonDocument JSONdoc(2048);

  Serial.println("notifying clients");

  JsonArray array = JSONdoc.createNestedArray("buttons");
  for (int i = 0; i < NUM_RELAYS; i++)
  {
    JsonObject button = array.createNestedObject();
    button["id"] = i + 1;
    button["on"] = relays[i].on;
    button["disabled"] = relays[i].disabled;
    button["last"] = relays[i].last;
  }
  serializeJson(JSONdoc, buffer);

  //Serial.println(buffer);
  ws.textAll(buffer);
}

#if NUM_RELAYS == 16
void writeRelaysToShiftRegister()
{
  // set the output data bytes based on the relay state array
  for (int i = 0; i < NUM_RELAYS; i++)
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
#endif

#if DO_LATCHED == 1
void handleLatch(uint8_t _relayNum)
{
  uint8_t index;

  if ((_relayNum > 0) && (_relayNum <= NUM_RELAYS))
  {
    index = _relayNum - 1;
    if (latched_relays[index].timeout != 0)
    {
      if ((latched_relays[index].relay_num > 0) && (latched_relays[index].relay_num <= NUM_RELAYS))
      {
        relays[index].disabled = 1;
        latched_relays[index].counter = (latched_relays[index].timeout * DELAY_COUNTER);
      }
      if ((latched_relays[index].latched_num > 0) && (latched_relays[index].latched_num <= NUM_RELAYS))
      {
        relays[latched_relays[index].latched_num - 1].disabled = 1;
        latched_relays[latched_relays[index].latched_num - 1].counter = (latched_relays[index].timeout * DELAY_COUNTER);
      }
    }
  }
}
#endif

#if DO_INTERLOCKED == 1
void handleInterlock(uint8_t _relayNum)
{
  uint8_t i, index;
  index = _relayNum - 1;
  bool doit = false;

  if ((_relayNum > 0) && (_relayNum <= NUM_RELAYS))
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
#endif

#if DO_PULSED
void handlePulsed(uint8_t _relayNum)
{
  uint8_t i;
  uint8_t index;

  if ((_relayNum > 0) && (_relayNum <= NUM_RELAYS))
  {
    if (pulsed_relays[_relayNum - 1].timeout != 0)
    {
      for (i = 1; i <= NUM_RELAYS; i++)
      {
        index = i - 1;
        if (pulsed_relays[index].timeout != 0)
        {
          if (i == _relayNum)
          {
            if ((pulsed_relays[index].relay_num > 0) && (pulsed_relays[index].relay_num <= NUM_RELAYS))
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
#endif

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
    // check for relayxtoggle or relayxxtoggle
    if (str == "home")
    {
      // do nothing, just refresh
      notify = true;
    }
    else if (str == "alloff")
    {
      for (relayNum = 1; relayNum <= NUM_RELAYS; relayNum++)
      {
        relays[relayNum - 1].low();
        relays[relayNum - 1].update();
      }
      notify = true;
    }
    else if (str.startsWith("button"))
    {
      // extract the relay number
      relayNum = str.substring(6, str.length()).toInt();
      if ((relayNum > 0) && (relayNum <= NUM_RELAYS))
      {
#if DO_LATCHED == 1
        handleLatch(relayNum);
#endif
#if DO_INTERLOCKED == 1
        handleInterlock(relayNum);
#endif
#if DO_PULSED == 1
        handlePulsed(relayNum);
#endif
        relays[relayNum - 1].toggle();
        relays[relayNum - 1].update();
        notify = true;
      }
    }

    if (notify)
    {
#if NUM_RELAYS == 16
      writeRelaysToShiftRegister();
#endif
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
  server.addHandler(&ws);
}

void initWiFi()
{
  int xc = 0;
  int attempts = 1;
  Serial.printf("\n\n");
#if DO_DELAY
  Serial.print("Delaying start ");
  while (xc < 120)
  {
    Serial.print(".");
    delay(500);
    xc++;
  }
#endif
  Serial.println("*");

  WiFi.config(ip, dns, gateway, subnet);
  WiFi.mode(WIFI_STA);

// make ESP32 as responsive as the ESP8266 by turning off WiFi power saving
#if defined(ESP32)
  esp_wifi_set_ps(WIFI_PS_NONE);
#endif

  while ((WiFi.status() != WL_CONNECTED) && (attempts <= 5))
  {
    WiFi.disconnect();
    WiFi.begin(ssid, password);
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
    Serial.println(ssid);
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
//       if ((relayNum > 0) && (relayNum < NUM_RELAYS))
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

  // initialise relays
  for (i = 0; i < NUM_RELAYS; i++)
  {
    relays[i].low();
    relays[i].disabled = 0;
    relays[i].last = 0;
#if NUM_RELAYS == 8
    pinMode(relays[i].pin, OUTPUT);
#endif
    relays[i].update();
  }

#if NUM_RELAYS == 16
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
#endif

  // Mount Filesystem
  initLittleFS();
  // Connect to Wi-Fi
  initWiFi();

  initWebSocket();

#if NUM_RELAYS == 8
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.println("on /");
    request->send(LittleFS, "/index.html", "text/html",false,nullptr); });
#elif NUM_RELAYS == 16
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.println("on /");
    request->send(LittleFS, "/index16.html", "text/html",false,nullptr); });
#endif

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

  ArduinoOTA.begin();
}

void loop()
{
  elapsed = millis();
  if ((elapsed - timer) > 2000)
  {
    onboard_led.on = !onboard_led.on;
    onboard_led.update();
    timer = elapsed;
    Serial.printf("WiFi Signal Strength: %d\n", WiFi.RSSI());
  }

  bool notify = false;

#if (DO_LATCHED || DO_PULSED)
  uint8_t i;

  if ((elapsed - latched_timer) > DELAY_INTERVAL_MS)
  {
    latched_timer = elapsed;

    // handle latched buttons
    for (i = 0; i < NUM_RELAYS; i++)
    {

#if DO_LATCHED
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
#endif // DO_LATCHED

#if DO_PULSED
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
#endif // DO_PULSED
    }  // for loop
  }    // latched_timer
#endif // (DO_LATCHED || DO_PULSED)

  if (notify)
  {
#if NUM_RELAYS == 16
    writeRelaysToShiftRegister();
#endif
    notifyClients();
  }

  ws.cleanupClients();

  // Check for over the air update request and (if present) flash it
  ArduinoOTA.handle();
}
