/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp8266-nodemcu-websocket-server-arduino/
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/

// Import required libraries
#include <Arduino.h>
#include "LittleFS.h"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "credentials.h"

// Replace with your network credentials
// const char* ssid = "REPLACE_WITH_YOUR_SSID";
// const char* password = "REPLACE_WITH_YOUR_PASSWORD";

#define NUM_RELAYS 8
//#define DO_DELAY 1    // uncomment for a 6 second delay at startup

uint32_t elapsed = 0;
uint32_t timer = 0;

#define COUNTDOWN_TIMEOUT_MS 5000
uint32_t countdown = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct OutputPin
{
  // state variables
  const uint8_t pin;
  uint8_t on;
  const uint8_t on_state; // LOW active or HIGH active
  const char *label;

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

// ----------------------------------------------------------------------------
// LittleFS initialization
// ----------------------------------------------------------------------------

OutputPin onboard_led = {LED_BUILTIN, false, HIGH, "LED"};

#if NUM_RELAYS == 8
OutputPin relays[] = {
    {5, false, HIGH, "RELAY1"},
    {4, false, HIGH, "RELAY2"},
    {0, false, HIGH, "RELAY3"},
    {15, false, HIGH, "RELAY4"},
    {13, false, HIGH, "RELAY5"},
    {12, false, HIGH, "RELAY6"},
    {14, false, HIGH, "RELAY7"},
    {16, false, HIGH, "RELAY8"}};
#elif NUM_RELAYS == 16
OutputPin relays[] = {
    {255, false, HIGH, "RELAY1"},
    {255, false, HIGH, "RELAY2"},
    {255, false, HIGH, "RELAY3"},
    {255, false, HIGH, "RELAY4"},
    {255, false, HIGH, "RELAY5"},
    {255, false, HIGH, "RELAY6"},
    {255, false, HIGH, "RELAY7"},
    {255, false, HIGH, "RELAY8"},
    {255, false, HIGH, "RELAY9"},
    {255, false, HIGH, "RELAY10"},
    {255, false, HIGH, "RELAY11"},
    {255, false, HIGH, "RELAY12"},
    {255, false, HIGH, "RELAY13"},
    {255, false, HIGH, "RELAY14"},
    {255, false, HIGH, "RELAY15"},
    {255, false, HIGH, "RELAY16"}};

const int latchPin = 12;                // Latch Pin of 74hc595
const int clockPin = 13;                // Clock  Pin of 74hc595
const int dataPin = 14;                 // Data  Pin of 74hc595
int oePin = 5;                          // Oe Pin of 74hc595
const int numRegisters = 2;             // Number of 74hc595 on the board
byte outputData[numRegisters];          // the bytes used to shift out the data

#endif

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
  Serial.println("notifying clients");

  char buffer[(((NUM_RELAYS + 1) * 15) + 2)];
  char temp[20];

  sprintf(buffer, "{\"%s\":%s", onboard_led.label, onboard_led.on ? "true" : "false");
  for (int i = 0; i < NUM_RELAYS; i++)
  {
    sprintf(temp, ",\"%s\":%s", relays[i].label, relays[i].on ? "true" : "false");
    strcat(buffer, temp);
  }
  strcat(buffer, "}");
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
    else if (str.startsWith("relay"))
    {
      if (str.endsWith("toggle"))
      {
        // extract the relay number
        relayNum = str.substring(5, str.length() + 5 - 11).toInt();
        if ((relayNum > 0) && (relayNum <= NUM_RELAYS))
        {
          relays[relayNum - 1].toggle();
          relays[relayNum - 1].update();
          notify = true;
        }
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

String processor(const String &var)
{
  Serial.println(var);

  int relayNum, relayIndex;
  // check for RELAYxSTATE or RELAYxxSTATE
  if (var.startsWith("RELAY"))
  {
    if (var.endsWith("STATE"))
    {
      // extract the relay number
      relayNum = var.substring(5, var.length() + 5 - 10).toInt();
      if ((relayNum > 0) && (relayNum < NUM_RELAYS))
      {
        relayIndex = relayNum - 1;
        return relays[relayIndex].on ? "ON" : "OFF";
      }
    }
  }

  return String();
}

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
#if NUM_RELAYS == 8
    pinMode(relays[i].pin, OUTPUT);
    relays[i].update();
#endif
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
    request->send(LittleFS, "/index.html", "text/html",false,processor); });
#elif NUM_RELAYS == 16
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.println("on /");
    request->send(LittleFS, "/index16.html", "text/html",false,processor); });
#endif

  server.serveStatic("/", LittleFS, "/");

  // Start server
  Serial.println("starting server");
  server.begin();
  // notifyClients();
}

void loop()
{
  elapsed = millis();
  if ((elapsed - timer) > 5000)
  {
    onboard_led.on = !onboard_led.on;
    onboard_led.update();
    timer = elapsed;
  }

  /*
    if ((elapsed - countdown) > COUNTDOWN_TIMEOUT_MS && countdown != 0)
    {
      relays[0].low();
      relays[1].low();
      countdown = 0;
      notifyClients();
    }
  */

  ws.cleanupClients();
}
