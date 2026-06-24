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
#include <Preferences.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#endif

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

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

#if defined(ESP32)
portMUX_TYPE provisioningScanMux = portMUX_INITIALIZER_UNLOCKED;
#define PROVISIONING_LOCK() portENTER_CRITICAL(&provisioningScanMux)
#define PROVISIONING_UNLOCK() portEXIT_CRITICAL(&provisioningScanMux)
#else
#define PROVISIONING_LOCK() noInterrupts()
#define PROVISIONING_UNLOCK() interrupts()
#endif

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
void startProvisioningScan();
void pollProvisioningScan();

String sanitizeSsidForJson(const String &raw)
{
  String cleaned;
  cleaned.reserve(raw.length());

  for (size_t i = 0; i < raw.length(); i++)
  {
    uint8_t c = (uint8_t)raw[i];
    if (c >= 32 && c <= 126)
    {
      cleaned += (char)c;
    }
    else
    {
      cleaned += '?';
    }
  }

  if (cleaned.length() > kMaxSsidLength)
  {
    cleaned = cleaned.substring(0, kMaxSsidLength);
  }

  return cleaned;
}

String getProvisioningApName()
{
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  uint32_t id = (uint32_t)(mac & 0xFFFFFFu);
#elif defined(ESP8266)
  uint32_t id = ESP.getChipId() & 0xFFFFFFu;
#else
  uint32_t id = 0x123456u;
#endif

  char name[32];
  snprintf(name, sizeof(name), "RelaySetup-%06lX", (unsigned long)id);
  return String(name);
}

String getProvisioningHtml()
{
  String html;
  html.reserve(4096);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Wi-Fi Provisioning</title>";
  html += "<style>body{font-family:Arial,sans-serif;background:#eef2f7;margin:0;padding:24px;}";
  html += ".card{max-width:640px;margin:0 auto;background:#fff;border-radius:12px;padding:20px;box-shadow:0 8px 24px rgba(0,0,0,.12);}";
  html += "h1{margin:0 0 6px 0;font-size:24px;}p{color:#444;}label{display:block;margin:14px 0 6px;font-weight:600;}";
  html += "input,select,button{width:100%;padding:10px;border:1px solid #ccd3de;border-radius:8px;font-size:16px;box-sizing:border-box;}";
  html += "button{background:#0b5ed7;color:#fff;border:none;cursor:pointer;margin-top:14px;}button.secondary{background:#5c636a;margin-top:8px;}";
  html += "small{color:#666;}#status{margin-top:12px;font-weight:600;}</style></head><body>";
  html += "<div class='card'><h1>Configure Wi-Fi</h1><p>Select an SSID from scan results or enter one manually.</p>";
  html += "<label for='ssidSelect'>Scanned SSIDs</label><select id='ssidSelect'><option value=''>Scanning...</option></select>";
  html += "<button class='secondary' type='button' onclick='scan(true)'>Rescan</button>";
  html += "<label for='ssidInput'>SSID (manual or selected)</label><input id='ssidInput' maxlength='32' placeholder='Wi-Fi SSID'>";
  html += "<label for='pwdInput'>Password</label><input id='pwdInput' maxlength='64' type='password' placeholder='Leave blank for open network'>";
  html += "<button type='button' onclick='saveCfg()'>Save and Reboot</button><div id='status'></div><small>After save, this device will reboot and join your Wi-Fi network.</small></div>";
  html += "<script>const s=document.getElementById('ssidSelect');const i=document.getElementById('ssidInput');const p=document.getElementById('pwdInput');const st=document.getElementById('status');";
  html += "s.addEventListener('change',()=>{if(s.value)i.value=s.value;});";
  html += "function scan(force){try{const u='/scan?t='+Date.now()+(force?'&rescan=1':'');const xhr=new XMLHttpRequest();xhr.open('GET',u,true);xhr.onreadystatechange=function(){if(xhr.readyState!==4)return;s.innerHTML='';if(xhr.status<200||xhr.status>=300){s.innerHTML='<option value="">Scan failed</option>';st.textContent='Scan request failed: HTTP '+xhr.status;return;}let d={};try{d=JSON.parse(xhr.responseText||'{}');}catch(e){s.innerHTML='<option value="">Scan failed</option>';st.textContent='Scan parse/error: '+e;return;}if(d.scanning){s.innerHTML='<option value="">Scanning...</option>';st.textContent='Scanning...';setTimeout(function(){scan(false);},900);return;}const list=Array.isArray(d.ssids)?d.ssids:[];if(!list.length){s.innerHTML='<option value="">No SSIDs found</option>';st.textContent='No SSIDs found. Enter one manually.';return;}s.innerHTML='<option value="">Choose SSID...</option>';let visible=0;let hidden=0;list.forEach(function(x){const name=(x&&typeof x==='object')?String(x.ssid||''):String(x||'');const rssi=(x&&typeof x==='object'&&x.rssi!==undefined)?x.rssi:'?';const display=name.length?name:'(hidden network)';if(name.length)visible++;else hidden++;const o=document.createElement('option');o.value=name;o.textContent=display+' ('+rssi+' dBm)';s.appendChild(o);});st.textContent='Scan complete: '+visible+' visible, '+hidden+' hidden';};xhr.onerror=function(){s.innerHTML='<option value="">Scan failed</option>';st.textContent='Scan request failed.';};xhr.send();}catch(e){s.innerHTML='<option value="">Scan failed</option>';st.textContent='Scan error: '+e;}}";
  html += "async function saveCfg(){const ssid=i.value.trim();if(!ssid){st.textContent='SSID is required.';return;}st.textContent='Saving...';const body=`ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(p.value)}`;const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();if(d.ok){st.textContent='Saved. Rebooting...';}else{st.textContent='Save failed: '+(d.error||'unknown');}}scan(true);</script></body></html>";
  return html;
}

void startProvisioningScan()
{
  if (!provisioningMode)
  {
    return;
  }

  PROVISIONING_LOCK();
  provisioningScanInitialized = true;
  provisioningScanRequested = true;
  provisioningScanPayload = "{\"ssids\":[],\"scanning\":true}";
  PROVISIONING_UNLOCK();
}

void pollProvisioningScan()
{
  PROVISIONING_LOCK();
  bool canRun = provisioningMode && !provisioningScanRunning && provisioningScanRequested;
  if (canRun)
  {
    provisioningScanRunning = true;
    provisioningScanRequested = false;
    provisioningScanStartedAt = millis();
  }
  PROVISIONING_UNLOCK();

  if (!canRun)
  {
    return;
  }

  Serial.println("Provisioning scan started");

  WiFi.scanDelete();
  int scanState = WiFi.scanNetworks(false, true);

  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("ssids");
  doc["scanning"] = false;
  int visibleCount = 0;
  int hiddenCount = 0;

  if (scanState > 0)
  {
    for (int i = 0; i < scanState; i++)
    {
      String rawSsidName = WiFi.SSID(i);
      String ssidName = sanitizeSsidForJson(rawSsidName);

      if (rawSsidName.length() > 0)
      {
        visibleCount++;
      }
      else
      {
        hiddenCount++;
      }

      JsonObject row = arr.createNestedObject();
      row["ssid"] = ssidName;
      row["rssi"] = WiFi.RSSI(i);
    }
  }

  doc["count"] = scanState > 0 ? scanState : 0;
  doc["visibleCount"] = visibleCount;
  doc["hiddenCount"] = hiddenCount;

  WiFi.scanDelete();
  String updatedPayload;
  serializeJson(doc, updatedPayload);

  PROVISIONING_LOCK();
  provisioningScanPayload = updatedPayload;
  provisioningScanRunning = false;
  PROVISIONING_UNLOCK();

  Serial.printf("Provisioning scan complete: %d results (%d visible, %d hidden)\n",
                scanState > 0 ? scanState : 0, visibleCount, hiddenCount);
}

void startProvisioningPortal()
{
  provisioningMode = true;
  reportSignalStrength = false;

  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);

  String apName = getProvisioningApName();
  WiFi.softAP(apName.c_str());

  Serial.println("Wi-Fi credentials are not configured.");
  Serial.printf("Provisioning AP started: %s\n", apName.c_str());
  Serial.printf("Connect to AP and open: http://%s/\n", WiFi.softAPIP().toString().c_str());
  Serial.println("To configure over serial, enter: reset_wifi");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.printf("Provisioning HTTP GET / from %s\n", request->client()->remoteIP().toString().c_str());
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", getProvisioningHtml());
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    request->send(response); });

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "ok"); });

  // Common captive portal probe URLs.
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->redirect("/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->redirect("/"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->redirect("/"); });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    Serial.printf("Provisioning HTTP GET /scan from %s\n", request->client()->remoteIP().toString().c_str());
    bool rescan = request->hasParam("rescan");

    bool running = false;
    bool initialized = false;
    PROVISIONING_LOCK();
    running = provisioningScanRunning;
    initialized = provisioningScanInitialized;
    PROVISIONING_UNLOCK();

    if (!running && (rescan || !initialized))
    {
      startProvisioningScan();
    }

    String payloadCopy;
    PROVISIONING_LOCK();
    payloadCopy = provisioningScanPayload;
    PROVISIONING_UNLOCK();

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", payloadCopy);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    request->send(response); });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    Serial.printf("Provisioning HTTP POST /save from %s\n", request->client()->remoteIP().toString().c_str());
    String newSsid = "";
    String newPassword = "";

    if (request->hasParam("ssid", true))
      newSsid = request->getParam("ssid", true)->value();
    if (request->hasParam("password", true))
      newPassword = request->getParam("password", true)->value();

    newSsid.trim();
    if (newSsid.length() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
      return;
    }

    if (newSsid.length() > kMaxSsidLength)
      newSsid = newSsid.substring(0, kMaxSsidLength);
    if (newPassword.length() > kMaxPasswordLength)
      newPassword = newPassword.substring(0, kMaxPasswordLength);

    wifiSsid = newSsid;
    wifiPassword = newPassword;

    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    request->send(200, "application/json", "{\"ok\":true}");
    pendingRestart = true;
    pendingRestartAt = millis() + 1500; });

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->redirect("/"); });

  server.begin();
}

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

struct RelayLabel
{
  String on;
  String off;
};

RelayLabel relayLabels[NUM_RELAYS];

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

uint8_t interlocked_buttons[] = {1, 2, 3, 4, 5, 6, 8};

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

String defaultRelayOnLabel(uint8_t relayNum)
{
  return "Relay " + String(relayNum) + " On";
}

String defaultRelayOffLabel(uint8_t relayNum)
{
  return "Relay " + String(relayNum) + " Off";
}

String clampRelayLabel(const String &label, uint8_t relayNum, bool isOnLabel)
{
  String cleaned = label;
  cleaned.trim();

  if (cleaned.length() == 0)
  {
    return isOnLabel ? defaultRelayOnLabel(relayNum) : defaultRelayOffLabel(relayNum);
  }

  if (cleaned.length() > kMaxRelayLabelLength)
  {
    cleaned = cleaned.substring(0, kMaxRelayLabelLength);
  }

  return cleaned;
}

void assignRelayLabels(uint8_t relayNum, const String &requestedOnLabel, const String &requestedOffLabel)
{
  String normalizedOff = clampRelayLabel(requestedOffLabel, relayNum, false);
  String normalizedOn = requestedOnLabel;
  normalizedOn.trim();

  // If ON is blank, treat it as the same as OFF.
  if (normalizedOn.length() == 0)
  {
    normalizedOn = normalizedOff;
  }
  else
  {
    normalizedOn = clampRelayLabel(normalizedOn, relayNum, true);
  }

  relayLabels[relayNum - 1].on = normalizedOn;
  relayLabels[relayNum - 1].off = normalizedOff;
}

void setDefaultRelayLabels()
{
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    relayLabels[i].on = defaultRelayOnLabel(i + 1);
    relayLabels[i].off = defaultRelayOffLabel(i + 1);
  }
}

bool saveRelayLabels()
{
#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("labels", false))
  {
    Serial.println("ERROR: Failed to open NVS labels namespace");
    return false;
  }
  char key[8];
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    snprintf(key, sizeof(key), "on_%d", i);
    prefs.putString(key, relayLabels[i].on);
    snprintf(key, sizeof(key), "off_%d", i);
    prefs.putString(key, relayLabels[i].off);
  }
  prefs.end();
  Serial.println("Saved relay labels to NVS");
  return true;
#else
  StaticJsonDocument<4096> doc;
  JsonArray labels = doc.createNestedArray("labels");

  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    JsonObject label = labels.createNestedObject();
    label["on"] = relayLabels[i].on;
    label["off"] = relayLabels[i].off;
  }

  File file = LittleFS.open(kRelayLabelsPath, "w");
  if (!file)
  {
    Serial.println("Failed to open relay labels file for writing");
    return false;
  }

  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write relay labels file");
    file.close();
    return false;
  }

  file.close();
  Serial.println("Saved relay labels");
  return true;
#endif
}

void loadRelayLabels()
{
  setDefaultRelayLabels();

#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("labels", true))
  {
    Serial.println("NVS labels namespace empty, using defaults");
    return;
  }
  char key[8];
  bool anyFound = false;
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    snprintf(key, sizeof(key), "on_%d", i);
    if (prefs.isKey(key))
    {
      anyFound = true;
      assignRelayLabels(i + 1, prefs.getString(key, ""), relayLabels[i].off);
    }
    snprintf(key, sizeof(key), "off_%d", i);
    if (prefs.isKey(key))
    {
      anyFound = true;
      assignRelayLabels(i + 1, relayLabels[i].on, prefs.getString(key, ""));
    }
  }
  prefs.end();
  if (anyFound)
    Serial.println("Loaded relay labels from NVS");
  else
    Serial.println("NVS labels not found, using defaults");
#else
  if (!LittleFS.exists(kRelayLabelsPath))
  {
    Serial.println("Relay labels file not found, using defaults");
    return;
  }

  File file = LittleFS.open(kRelayLabelsPath, "r");
  if (!file)
  {
    Serial.println("Failed to open relay labels file for reading, using defaults");
    return;
  }

  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse relay labels file (%s), using defaults\n", error.c_str());
    return;
  }

  JsonArray labels = doc["labels"].as<JsonArray>();
  if (labels.isNull())
  {
    Serial.println("Relay labels file missing labels array, using defaults");
    return;
  }

  uint8_t count = labels.size() < NUM_RELAYS ? labels.size() : NUM_RELAYS;
  for (uint8_t i = 0; i < count; i++)
  {
    JsonObject label = labels[i];
    if (label.isNull())
    {
      continue;
    }

    assignRelayLabels(i + 1, String(label["on"] | ""), String(label["off"] | ""));
  }

  Serial.println("Loaded relay labels");
#endif
}

String defaultBoardName()
{
  return "Relay Board";
}

uint32_t getCryptoSeed()
{
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac ^ (mac >> 32) ^ 0xA5A55A5Au);
#elif defined(ESP8266)
  return ESP.getChipId() ^ 0xA5A55A5Au;
#else
  return 0x5A5AA5A5u;
#endif
}

String hexEncode(const uint8_t *data, size_t len)
{
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++)
  {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

int hexNibble(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool hexDecode(const String &hexText, String &decoded)
{
  decoded = "";
  if ((hexText.length() % 2) != 0)
  {
    return false;
  }

  decoded.reserve(hexText.length() / 2);
  for (size_t i = 0; i < hexText.length(); i += 2)
  {
    int hi = hexNibble(hexText[i]);
    int lo = hexNibble(hexText[i + 1]);
    if (hi < 0 || lo < 0)
    {
      decoded = "";
      return false;
    }
    decoded += (char)((hi << 4) | lo);
  }
  return true;
}

String encryptConfigSecret(const String &plain)
{
  if (plain.length() == 0)
  {
    return "";
  }

  uint32_t seed = getCryptoSeed();
  const size_t len = plain.length();
  uint8_t *buf = (uint8_t *)malloc(len);
  if (!buf)
  {
    return "";
  }

  for (size_t i = 0; i < len; i++)
  {
    uint8_t keyByte = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
    buf[i] = ((uint8_t)plain[i]) ^ keyByte ^ (uint8_t)(0x3Du + (i * 17u));
  }

  String encoded = hexEncode(buf, len);
  free(buf);
  return encoded;
}

String decryptConfigSecret(const String &encoded)
{
  if (encoded.length() == 0)
  {
    return "";
  }

  String cipher;
  if (!hexDecode(encoded, cipher))
  {
    return "";
  }

  uint32_t seed = getCryptoSeed();
  String plain;
  plain.reserve(cipher.length());
  for (size_t i = 0; i < cipher.length(); i++)
  {
    uint8_t keyByte = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
    plain += (char)(((uint8_t)cipher[i]) ^ keyByte ^ (uint8_t)(0x3Du + (i * 17u)));
  }
  return plain;
}

String readSerialLineBlocking()
{
  String line;
  while (true)
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
        return line;
      }
      line += c;
    }
    delay(10);
    yield();
  }
}

bool runSerialWiFiProvisioningWizard()
{
  Serial.println("\n=== Wi-Fi provisioning ===");
  Serial.println("Scanning for SSIDs...");
  reportSignalStrength = false;

  WiFi.mode(WIFI_STA);
  int scanCount = WiFi.scanNetworks();

  if (scanCount > 0)
  {
    for (int i = 0; i < scanCount; i++)
    {
      Serial.printf("%d) %s (RSSI %d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }
  else
  {
    Serial.println("No SSIDs found by scan. You can still enter one manually.");
  }

  Serial.println("Enter SSID number from the list, or type SSID text directly:");
  String ssidInput = readSerialLineBlocking();
  ssidInput.trim();
  if (ssidInput.length() == 0)
  {
    Serial.println("No SSID provided. Aborting Wi-Fi provisioning.");
    WiFi.scanDelete();
    return false;
  }

  String selectedSsid;
  bool numericChoice = true;
  for (size_t i = 0; i < ssidInput.length(); i++)
  {
    if (!isDigit((unsigned char)ssidInput[i]))
    {
      numericChoice = false;
      break;
    }
  }

  if (numericChoice && scanCount > 0)
  {
    int choice = ssidInput.toInt();
    if (choice >= 1 && choice <= scanCount)
    {
      selectedSsid = WiFi.SSID(choice - 1);
    }
  }

  if (selectedSsid.length() == 0)
  {
    selectedSsid = ssidInput;
  }

  if (selectedSsid.length() > kMaxSsidLength)
  {
    selectedSsid = selectedSsid.substring(0, kMaxSsidLength);
  }

  WiFi.scanDelete();

  Serial.printf("Selected SSID: %s\n", selectedSsid.c_str());
  Serial.println("Enter Wi-Fi password (empty for open network):");
  String enteredPassword = readSerialLineBlocking();

  if (enteredPassword.length() > kMaxPasswordLength)
  {
    enteredPassword = enteredPassword.substring(0, kMaxPasswordLength);
  }

  wifiSsid = selectedSsid;
  wifiPassword = enteredPassword;

  if (!saveBoardConfig())
  {
    Serial.println("Failed to save Wi-Fi credentials to configuration");
    return false;
  }

  Serial.println("Wi-Fi credentials saved to configuration.");
  return true;
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

bool saveBoardConfig()
{
#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("board", false))
  {
    Serial.println("ERROR: Failed to open NVS board namespace");
    return false;
  }
  prefs.putString("name", boardName);
  prefs.putBool("doDelay",       doDelay);
  prefs.putUShort("delaySec", startupDelaySeconds);
  prefs.putBool("doLatched",     doLatched);
  prefs.putBool("doInterlocked", doInterlocked);
  prefs.putBool("doPulsed",      doPulsed);
  prefs.putBool("useStatic",     useStaticIp);
  prefs.putString("wifiSsidEnc", encryptConfigSecret(wifiSsid));
  prefs.putString("wifiPwdEnc", encryptConfigSecret(wifiPassword));
  if (useStaticIp)
  {
    prefs.putString("ip",      boardIp.toString());
    prefs.putString("dns",     boardDns.toString());
    prefs.putString("gateway", boardGateway.toString());
    prefs.putString("subnet",  boardSubnet.toString());
    Serial.printf("saveBoardConfig: saving static IP %s\n", boardIp.toString().c_str());
  }
  else
  {
    if (prefs.isKey("ip")) prefs.remove("ip");
    if (prefs.isKey("dns")) prefs.remove("dns");
    if (prefs.isKey("gateway")) prefs.remove("gateway");
    if (prefs.isKey("subnet")) prefs.remove("subnet");
    Serial.println("saveBoardConfig: saving DHCP mode");
  }
  prefs.end();
  Serial.printf("Saved board config to NVS: name=%s, dhcp=%d\n", boardName.c_str(), !useStaticIp);
  return true;
#else
  StaticJsonDocument<1536> doc;
  doc["name"] = boardName;
  doc["doDelay"] = doDelay;
  doc["startupDelaySeconds"] = startupDelaySeconds;
  doc["doLatched"] = doLatched;
  doc["doInterlocked"] = doInterlocked;
  doc["doPulsed"] = doPulsed;
  doc["wifiSsidEnc"] = encryptConfigSecret(wifiSsid);
  doc["wifiPwdEnc"] = encryptConfigSecret(wifiPassword);

  if (useStaticIp)
  {
    JsonObject ipConfig = doc.createNestedObject("ipConfig");
    ipConfig["ip"] = boardIp.toString();
    ipConfig["dns"] = boardDns.toString();
    ipConfig["gateway"] = boardGateway.toString();
    ipConfig["subnet"] = boardSubnet.toString();
  }
  else
  {
    doc["ipConfig"] = nullptr;
  }

  File file = LittleFS.open(kBoardConfigPath, "w");
  if (!file) return false;

  size_t bytesWritten = serializeJson(doc, file);
  file.close();
  if (bytesWritten == 0) return false;

  Serial.printf("Saved board config: name=%s, dhcp=%d\n", boardName.c_str(), !useStaticIp);
  return true;
#endif
}

void loadBoardConfig()
{
  boardName = defaultBoardName();
  useStaticIp = false;
  doDelay = false;
  startupDelaySeconds = 60;
  doLatched = false;
  doInterlocked = false;
  doPulsed = false;
  wifiSsid = "";
  wifiPassword = "";

#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("board", true))
  {
    Serial.println("NVS board namespace empty, using defaults");
    return;
  }

  if (prefs.isKey("name"))
  {
    String name = prefs.getString("name", "");
    name.trim();
    if (name.length() > kMaxBoardNameLength)
      name = name.substring(0, kMaxBoardNameLength);
    if (name.length() > 0)
      boardName = name;
  }

  doDelay       = prefs.getBool("doDelay",       false);
  startupDelaySeconds = prefs.getUShort("delaySec", 60);
  doLatched     = prefs.getBool("doLatched",     false);
  doInterlocked = prefs.getBool("doInterlocked", false);
  doPulsed      = prefs.getBool("doPulsed",      false);
  if (prefs.isKey("wifiSsidEnc"))
    wifiSsid = decryptConfigSecret(prefs.getString("wifiSsidEnc", ""));
  if (prefs.isKey("wifiPwdEnc"))
    wifiPassword = decryptConfigSecret(prefs.getString("wifiPwdEnc", ""));

  if (wifiSsid.length() > kMaxSsidLength)
    wifiSsid = wifiSsid.substring(0, kMaxSsidLength);
  if (wifiPassword.length() > kMaxPasswordLength)
    wifiPassword = wifiPassword.substring(0, kMaxPasswordLength);

  if (prefs.getBool("useStatic", false))
  {
    String ipStr      = prefs.getString("ip",      "");
    String dnsStr     = prefs.getString("dns",     "");
    String gatewayStr = prefs.getString("gateway", "");
    String subnetStr  = prefs.getString("subnet",  "");

    if (ipStr.length() > 0 && boardIp.fromString(ipStr) &&
        dnsStr.length() > 0 && boardDns.fromString(dnsStr) &&
        gatewayStr.length() > 0 && boardGateway.fromString(gatewayStr) &&
        subnetStr.length() > 0 && boardSubnet.fromString(subnetStr))
    {
      useStaticIp = true;
      Serial.printf("Loaded static IP: %s\n", boardIp.toString().c_str());
    }
  }

  prefs.end();
  Serial.printf("Loaded board config from NVS: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, doLatched=%d, doInterlocked=%d, doPulsed=%d, wifiSsidSet=%d\n",
                boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, doLatched, doInterlocked, doPulsed, wifiSsid.length() > 0);
#else
  if (!LittleFS.exists(kBoardConfigPath))
  {
    Serial.printf("Board config file not found, using defaults\n");
    return;
  }

  File file = LittleFS.open(kBoardConfigPath, "r");
  if (!file) return;

  StaticJsonDocument<1536> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse board config (%s), using defaults\n", error.c_str());
    return;
  }

  String name = doc["name"];
  if (name.length() > 0)
  {
    name.trim();
    if (name.length() > kMaxBoardNameLength)
      name = name.substring(0, kMaxBoardNameLength);
    boardName = name;
  }

  if (doc.containsKey("doDelay"))       doDelay       = doc["doDelay"]       | false;
  if (doc.containsKey("startupDelaySeconds")) startupDelaySeconds = doc["startupDelaySeconds"] | 60;
  if (doc.containsKey("doLatched"))     doLatched     = doc["doLatched"]     | false;
  if (doc.containsKey("doInterlocked")) doInterlocked = doc["doInterlocked"] | false;
  if (doc.containsKey("doPulsed"))      doPulsed      = doc["doPulsed"]      | false;
  wifiSsid = decryptConfigSecret(String(doc["wifiSsidEnc"] | ""));
  wifiPassword = decryptConfigSecret(String(doc["wifiPwdEnc"] | ""));
  if (wifiSsid.length() > kMaxSsidLength)
    wifiSsid = wifiSsid.substring(0, kMaxSsidLength);
  if (wifiPassword.length() > kMaxPasswordLength)
    wifiPassword = wifiPassword.substring(0, kMaxPasswordLength);

  JsonObject ipConfig = doc["ipConfig"];
  if (!ipConfig.isNull())
  {
    String ipStr = ipConfig["ip"], dnsStr = ipConfig["dns"],
           gatewayStr = ipConfig["gateway"], subnetStr = ipConfig["subnet"];
    if (ipStr.length() > 0 && boardIp.fromString(ipStr) &&
        dnsStr.length() > 0 && boardDns.fromString(dnsStr) &&
        gatewayStr.length() > 0 && boardGateway.fromString(gatewayStr) &&
        subnetStr.length() > 0 && boardSubnet.fromString(subnetStr))
      useStaticIp = true;
  }

  Serial.printf("Loaded board config: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, wifiSsidSet=%d\n", boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, wifiSsid.length() > 0);
#endif
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
  for (int i = 0; i < NUM_RELAYS; i++)
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

  String payload;
  serializeJson(JSONdoc, payload);

  //Serial.println(payload);
  ws.textAll(payload);
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
          if ((relayNum > 0) && (relayNum <= NUM_RELAYS))
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
            uint8_t count = labels.size() < NUM_RELAYS ? labels.size() : NUM_RELAYS;
            for (uint8_t i = 0; i < count; i++)
            {
              JsonObject label = labels[i];
              if (label.isNull())
              {
                continue;
              }

              relayNum = label["relay"] | (i + 1);
              if ((relayNum > 0) && (relayNum <= NUM_RELAYS))
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
      for (relayNum = 1; relayNum <= NUM_RELAYS; relayNum++)
      {
        relays[relayNum - 1].low();
        relays[relayNum - 1].update();
      }
      notify = true;
    }
    else
    {
      relayNum = parseRelayNumberFromCommand(str);
      if ((relayNum > 0) && (relayNum <= NUM_RELAYS))
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
  loadRelayLabels();
  loadBoardConfig();

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

    Serial.printf("/api/config: Attempting to save (name=%s, dhcp=%d, delay=%d, delaySeconds=%u, latched=%d, interlocked=%d, pulsed=%d)\n",
                  name.c_str(), useDhcp, doDelay, startupDelaySeconds, doLatched, doInterlocked, doPulsed);

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
    for (uint8_t relayNum = 1; relayNum <= NUM_RELAYS; relayNum++)
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
      for (i = 0; i < NUM_RELAYS; i++)
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
#if NUM_RELAYS == 16
    writeRelaysToShiftRegister();
#endif
    notifyClients();
  }

  ws.cleanupClients();

  processSerialCommands();

  // Check for over the air update request and (if present) flash it
  ArduinoOTA.handle();
}
