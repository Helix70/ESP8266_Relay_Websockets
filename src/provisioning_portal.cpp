#include "provisioning_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

#include "config_store.h"

namespace
{
constexpr size_t kMaxSsidLength = 32;
constexpr size_t kMaxPasswordLength = 64;

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
  html += "function scan(force){try{const u='/scan?t='+Date.now()+(force?'&rescan=1':'');const xhr=new XMLHttpRequest();xhr.open('GET',u,true);xhr.onreadystatechange=function(){if(xhr.readyState!==4)return;s.innerHTML='';if(xhr.status<200||xhr.status>=300){s.innerHTML='<option value=\"\">Scan failed</option>';st.textContent='Scan request failed: HTTP '+xhr.status;return;}let d={};try{d=JSON.parse(xhr.responseText||'{}');}catch(e){s.innerHTML='<option value=\"\">Scan failed</option>';st.textContent='Scan parse/error: '+e;return;}if(d.scanning){s.innerHTML='<option value=\"\">Scanning...</option>';st.textContent='Scanning...';setTimeout(function(){scan(false);},900);return;}const list=Array.isArray(d.ssids)?d.ssids:[];if(!list.length){s.innerHTML='<option value=\"\">No SSIDs found</option>';st.textContent='No SSIDs found. Enter one manually.';return;}s.innerHTML='<option value=\"\">Choose SSID...</option>';let visible=0;let hidden=0;list.forEach(function(x){const name=(x&&typeof x==='object')?String(x.ssid||''):String(x||'');const rssi=(x&&typeof x==='object'&&x.rssi!==undefined)?x.rssi:'?';const display=name.length?name:'(hidden network)';if(name.length)visible++;else hidden++;const o=document.createElement('option');o.value=name;o.textContent=display+' ('+rssi+' dBm)';s.appendChild(o);});st.textContent='Scan complete: '+visible+' visible, '+hidden+' hidden';};xhr.onerror=function(){s.innerHTML='<option value=\"\">Scan failed</option>';st.textContent='Scan request failed.';};xhr.send();}catch(e){s.innerHTML='<option value=\"\">Scan failed</option>';st.textContent='Scan error: '+e;}}";
  html += "async function saveCfg(){const ssid=i.value.trim();if(!ssid){st.textContent='SSID is required.';return;}st.textContent='Saving...';const body=`ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(p.value)}`;const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();if(d.ok){st.textContent='Saved. Rebooting...';}else{st.textContent='Save failed: '+(d.error||'unknown');}}scan(true);</script></body></html>";
  return html;
}

#if defined(ESP32)
portMUX_TYPE provisioningScanMux = portMUX_INITIALIZER_UNLOCKED;
#define PROVISIONING_LOCK() portENTER_CRITICAL(&provisioningScanMux)
#define PROVISIONING_UNLOCK() portEXIT_CRITICAL(&provisioningScanMux)
#else
#define PROVISIONING_LOCK() noInterrupts()
#define PROVISIONING_UNLOCK() interrupts()
#endif
} // namespace

extern AsyncWebServer server;
extern bool reportSignalStrength;
extern bool provisioningMode;
extern bool provisioningScanRunning;
extern bool provisioningScanRequested;
extern bool provisioningScanInitialized;
extern uint32_t provisioningScanStartedAt;
extern String provisioningScanPayload;
extern String wifiSsid;
extern String wifiPassword;
extern bool pendingRestart;
extern uint32_t pendingRestartAt;

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
