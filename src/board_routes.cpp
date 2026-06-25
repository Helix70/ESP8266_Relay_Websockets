#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "board_hardware.h"
#include "config_routes_internal.h"
#include "route_data.h"
#include "route_utils.h"

void registerBoardRoutes()
{
  server.on("/api/boards", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    doc["activeBoardFile"] = boardHardwarePath(hardwareVariant);

    JsonArray arr = doc.createNestedArray("boards");
    for (uint8_t vi = 0; vi < kSupportedVariantCount; vi++) {
      String path = boardHardwarePath(String(kSupportedVariants[vi]));
      if (!LittleFS.exists(path)) continue;

      File f = LittleFS.open(path, "r");
      if (!f) continue;

      DynamicJsonDocument entry(1024);
      if (deserializeJson(entry, f) != DeserializationError::Ok) { f.close(); continue; }
      f.close();

      JsonObject board = arr.createNestedObject();
      board["filename"] = path.substring(1);
      board["name"] = entry["name"] | "";
      board["cpu"] = entry["cpu"] | "";
      board["relayCount"] = entry["relayCount"] | 0;
      board["outputType"] = entry["outputType"] | "gpio";
    }

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/boards", HTTP_POST, [](AsyncWebServerRequest *request) {
    String variant = routeGetBodyParam(request, "variant");
    String outputType = routeGetBodyParam(request, "outputType");
    if (variant != "8relay" && variant != "16relay") {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid variant\"}");
      return;
    }

    BoardHardware tmp;
    tmp.loaded = false;
    {
      String path = boardHardwarePath(variant);
      if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        DynamicJsonDocument d(2048);
        if (f && deserializeJson(d, f) == DeserializationError::Ok) {
          tmp.name = d["name"] | "";
          tmp.cpu = d["cpu"] | BOARD_CPU_TYPE;
          tmp.relayCount = d["relayCount"] | relayCountForVariant(variant);
          tmp.ledPin = d["ledPin"] | kDefaultBoardLedPin;
          String ot = String(d["outputType"] | "gpio");
          tmp.outputType = (ot == "shiftregister") ? BOARD_OUTPUT_SHIFTREGISTER : BOARD_OUTPUT_GPIO;
          for (uint8_t i = 0; i < 16; i++) tmp.relayPins[i] = 255;
          JsonArray relaysJson = d["relays"].as<JsonArray>();
          if (!relaysJson.isNull()) {
            for (JsonObject r : relaysJson) {
              uint8_t n = r["relay"] | 0;
              if (n >= 1 && n <= 16) tmp.relayPins[n - 1] = r["pin"] | (uint8_t)255;
            }
          }
          JsonObject sr = d["shiftRegister"];
          tmp.srLatchPin = sr.isNull() ? kDefaultShiftRegisterLatchPin : (uint8_t)(sr["latchPin"] | kDefaultShiftRegisterLatchPin);
          tmp.srClockPin = sr.isNull() ? kDefaultShiftRegisterClockPin : (uint8_t)(sr["clockPin"] | kDefaultShiftRegisterClockPin);
          tmp.srDataPin = sr.isNull() ? kDefaultShiftRegisterDataPin : (uint8_t)(sr["dataPin"] | kDefaultShiftRegisterDataPin);
          tmp.srOePin = sr.isNull() ? kDefaultShiftRegisterOePin : (uint8_t)(sr["oePin"] | kDefaultShiftRegisterOePin);
          tmp.loaded = true;
        }
        if (f) f.close();
      }
    }

    if (routeGetBodyParam(request, "name").length()) tmp.name = routeGetBodyParam(request, "name");
    if (routeGetBodyParam(request, "ledPin").length()) tmp.ledPin = (uint8_t)routeGetBodyParam(request, "ledPin").toInt();
    if (outputType == "gpio" || outputType == "shiftregister")
      tmp.outputType = (outputType == "shiftregister") ? BOARD_OUTPUT_SHIFTREGISTER : BOARD_OUTPUT_GPIO;

    if (tmp.outputType == BOARD_OUTPUT_GPIO) {
      uint8_t rc = relayCountForVariant(variant);
      for (uint8_t i = 1; i <= rc; i++) {
        String key = "relay" + String(i) + "_pin";
        if (routeGetBodyParam(request, key.c_str()).length())
          tmp.relayPins[i - 1] = (uint8_t)routeGetBodyParam(request, key.c_str()).toInt();
      }
    } else {
      if (routeGetBodyParam(request, "sr_latchPin").length()) tmp.srLatchPin = (uint8_t)routeGetBodyParam(request, "sr_latchPin").toInt();
      if (routeGetBodyParam(request, "sr_clockPin").length()) tmp.srClockPin = (uint8_t)routeGetBodyParam(request, "sr_clockPin").toInt();
      if (routeGetBodyParam(request, "sr_dataPin").length()) tmp.srDataPin = (uint8_t)routeGetBodyParam(request, "sr_dataPin").toInt();
      if (routeGetBodyParam(request, "sr_oePin").length()) tmp.srOePin = (uint8_t)routeGetBodyParam(request, "sr_oePin").toInt();
    }

    DynamicJsonDocument doc(2048);
    doc["name"] = tmp.name;
    doc["cpu"] = tmp.cpu.length() ? tmp.cpu : String(BOARD_CPU_TYPE);
    doc["relayCount"] = relayCountForVariant(variant);
    doc["ledPin"] = tmp.ledPin;
    doc["outputType"] = (tmp.outputType == BOARD_OUTPUT_SHIFTREGISTER) ? "shiftregister" : "gpio";

    if (tmp.outputType == BOARD_OUTPUT_GPIO) {
      uint8_t rc = doc["relayCount"].as<uint8_t>();
      JsonArray relaysJson = doc.createNestedArray("relays");
      for (uint8_t i = 0; i < rc; i++) {
        JsonObject r = relaysJson.createNestedObject();
        r["relay"] = i + 1;
        r["pin"] = tmp.relayPins[i];
      }
    } else {
      JsonObject sr = doc.createNestedObject("shiftRegister");
      sr["latchPin"] = tmp.srLatchPin;
      sr["clockPin"] = tmp.srClockPin;
      sr["dataPin"] = tmp.srDataPin;
      sr["oePin"] = tmp.srOePin;
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

    if (variant == hardwareVariant) {
      loadBoardHardware(hardwareVariant);
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });
}
