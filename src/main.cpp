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
//const char* ssid = "REPLACE_WITH_YOUR_SSID";
//const char* password = "REPLACE_WITH_YOUR_PASSWORD";

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

struct OutputPin {
    // state variables
    const uint8_t pin;
    uint8_t  on;
    const uint8_t on_state;   // LOW active or HIGH active

    // methods
    void update() {
        digitalWrite(pin, on ? HIGH : LOW);
    }

    void low() {
      on = !on_state;
    }

    void high() {
      on = on_state;
    }

    void toggle() {
      on = !on;
    }
};

// ----------------------------------------------------------------------------
// LittleFS initialization
// ----------------------------------------------------------------------------

OutputPin onboard_led = { LED_BUILTIN, false, HIGH };

OutputPin relays[] = {
  { 5, false, HIGH },
  { 4, false, HIGH },
  { 0, false, HIGH },
  { 15, false, HIGH },
  { 13, false, HIGH },
  { 12, false, HIGH },
  { 14, false, HIGH },
  { 16, false, HIGH }
};

void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("Cannot mount LittleFS volume...");
    while (1) {
        onboard_led.on = millis() % 200 < 50;
        onboard_led.update();
    }
  }
  Serial.println("LittleFS volume mounted...");
}

void notifyClients() {
  Serial.println("notifying clients");

  char buffer[50];
  sprintf(
    buffer, 
    "{\"LED\":%s,\"RELAY1\":%s,\"RELAY2\":%s}",
    onboard_led.on ? "true" : "false",
    relays[0].on ? "true" : "false",
    relays[1].on ? "true" : "false"
    );
  ws.textAll(buffer);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  char buffer[len+1];   
  snprintf(buffer, len+1, "%s", data);  // convert char array to null terminated
  Serial.printf("handle web socket message: %s\n", buffer);
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    bool notify = false;
    if (strcmp((char*)buffer, "toggle") == 0) {
      onboard_led.on = !onboard_led.on;
      onboard_led.update();
      notify = true;
    }

    if (relays[0].on || relays[1].on) {
      // skip updates
    }
    else if (strcmp((char*)buffer, "relay1toggle") == 0) {
      relays[0].toggle();
      relays[0].update();
      notify = true;
      countdown = millis();
    }
    else if (strcmp((char*)buffer, "relay2toggle") == 0) {
      relays[1].toggle();
      relays[1].update();
      notify = true;
      countdown = millis();
    }

    if (notify) {
      notifyClients();
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    Serial.println("on event");
    switch (type) {
      case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
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

void initWebSocket() {
  Serial.println("init web socket");
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
  }
  Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
}

String processor(const String& var){
  Serial.println(var);

  if(var == "STATE"){
    if (onboard_led.on){
      return "ON";
    }
    else{
      return "OFF";
    }
  }
  if (var == "RELAY1STATE") {
    return relays[0].on ? "ON" : "OFF";
  }
  if (var == "RELAY2STATE") {
    return relays[1].on ? "ON" : "OFF";
  }
  if (var == "RELAY1DISABLED") {
    return relays[1].on ? "disabled" : "";
  }
  if (var == "RELAY2DISABLED") {
    return relays[0].on ? "disabled" : "";
  }

  return String();
}

void setup(){
  uint8_t i;

  // Serial port for debugging purposes
  Serial.begin(115200);

  pinMode(onboard_led.pin, OUTPUT);
  onboard_led.on = !onboard_led.on_state;
  onboard_led.update();

  for (i=0; i<8; i++) {
    pinMode(relays[i].pin, OUTPUT);
    relays[i].low();
    relays[i].update();
  }

  initLittleFS();
  // Connect to Wi-Fi
  initWiFi();

  initWebSocket();

// Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("on /");
    request->send(LittleFS, "/index.html", "text/html",false,processor);
  });
  server.serveStatic("/", LittleFS, "/");

  // Start server
  Serial.println("starting server");
  server.begin();
  notifyClients();
}

void loop() {
  uint8_t i;

  elapsed = millis();
  if ((elapsed - timer) > 5000) {
    onboard_led.on = !onboard_led.on;
    timer = elapsed;
    notifyClients();
  }

  if ((elapsed - countdown) > COUNTDOWN_TIMEOUT_MS && countdown != 0) {
    relays[0].low();
    relays[1].low();
    countdown = 0;
    notifyClients();
  }

  ws.cleanupClients();

  for (i=0; i<8; i++) {
    relays[i].update();
  }
  onboard_led.update();
}

