#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "config_routes_internal.h"
#include "route_data.h"
#include "route_utils.h"

namespace
{
void addTemplateListEntry(JsonArray &arr, const String &filename, File &file)
{
  String title = filename;
  uint8_t relayCount = (filename.indexOf("16") >= 0) ? kVariantRelayCount16 : kVariantRelayCount8;

  DynamicJsonDocument filter(128);
  filter["title"] = true;
  filter["relayCount"] = true;

  DynamicJsonDocument meta(192);
  if (deserializeJson(meta, file, DeserializationOption::Filter(filter)) == DeserializationError::Ok)
  {
    String parsedTitle = String(meta["title"] | "");
    parsedTitle.trim();
    if (parsedTitle.length() > 0)
    {
      title = parsedTitle;
    }
    relayCount = (uint8_t)(meta["relayCount"] | relayCount);
  }

  JsonObject entry = arr.createNestedObject();
  entry["filename"] = filename;
  entry["title"] = title;
  entry["relayCount"] = relayCount;
}
}

void registerTemplateRoutes()
{
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
        if (!f) continue;
        addTemplateListEntry(arr, fname, f);
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
              addTemplateListEntry(arr, fname, f);
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
    String title = routeGetBodyParam(request, "title");
    title.trim();
    if (title.length() == 0) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"title required\"}");
      return;
    }
    if (title.length() > kMaxTemplateTitleLength) title = title.substring(0, kMaxTemplateTitleLength);

    int rc = routeGetBodyParam(request, "relayCount").toInt();
    if (rc <= 0 || rc > APP_MAX_RELAYS) rc = (int)relayCount;

    String filename = "";
    for (size_t i = 0; i < title.length() && filename.length() < kMaxTemplateFilenameLength; i++) {
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
      String prefix = "relay" + String(i);
      String onLabel = routeGetBodyParam(request, (prefix + "_on").c_str());
      String offLabel = routeGetBodyParam(request, (prefix + "_off").c_str());
      onLabel.trim();
      offLabel.trim();

      String modeStr = routeGetBodyParam(request, (prefix + "_mode").c_str());
      if (modeStr != "interlocked" && modeStr != "pulsed") modeStr = "onoff";
      uint8_t group = (uint8_t)routeGetBodyParam(request, (prefix + "_group").c_str()).toInt();
      uint8_t pt = (uint8_t)routeGetBodyParam(request, (prefix + "_pulseTimeout").c_str()).toInt();
      if (pt == 0 || pt > kMaxPulseTimeoutSeconds) pt = kDefaultPulseTimeoutSeconds;

      JsonObject label = labels.createNestedObject();
      label["on"] = onLabel;
      label["off"] = offLabel;
      label["mode"] = modeStr;
      label["group"] = group;
      label["pulseTimeout"] = pt;
    }

    File f = LittleFS.open("/templates/" + filename, "w");
    if (!f) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }
    serializeJson(doc, f);
    f.close();

    DynamicJsonDocument response(256);
    response["ok"] = true;
    response["filename"] = filename;
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);
  });
}
