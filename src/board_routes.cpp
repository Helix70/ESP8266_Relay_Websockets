#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "board_hardware.h"
#include "config_routes_internal.h"
#include "route_data.h"
#include "route_utils.h"
#include "config_store.h"
#include "relay_runtime.h"
#include "storage_utils.h"
#include "storage_lock.h"
#include "web_runtime.h"

namespace
{
// LFS_NAME_MAX = 32 on ESP8266; temp is slug+".jt" (3), final is slug+".json" (5).
// Final file is binding: slug <= 26 keeps component at 31 < 32.
constexpr size_t kMaxBoardFilenameLength = 26;
constexpr size_t kBoardDocMinCapacity = 2048;
constexpr size_t kBoardDocMaxCapacity = 4096;
constexpr size_t kBoardListDocCapacity = 512;

String sanitizeBoardSlug(const String &title)
{
  String out;
  for (size_t i = 0; i < title.length() && out.length() < kMaxBoardFilenameLength; i++)
  {
    char c = title[i];
    if (isAlphaNumeric(c))
    {
      out += (char)tolower(c);
    }
    else if ((c == ' ' || c == '-' || c == '_') && out.length() > 0 && out[out.length() - 1] != '-')
    {
      out += '-';
    }
  }

  while (out.length() > 0 && out[out.length() - 1] == '-')
  {
    out = out.substring(0, out.length() - 1);
  }

  if (out.length() == 0)
  {
    out = "board";
  }

  return out;
}

bool normalizeBoardFilename(String raw, String &normalized)
{
  raw.trim();
  if (raw.startsWith("/boards/"))
  {
    raw = raw.substring(String("/boards/").length());
  }
  else if (raw.startsWith("boards/"))
  {
    raw = raw.substring(String("boards/").length());
  }

  if (raw.length() == 0 || raw.length() > (kMaxBoardFilenameLength + 5))
  {
    return false;
  }

  if (raw.indexOf('/') >= 0 || raw.indexOf('\\') >= 0 || raw.indexOf("..") >= 0)
  {
    return false;
  }

  if (!raw.endsWith(".json"))
  {
    return false;
  }

  normalized = raw;
  return true;
}

String boardPathFromFilename(const String &filename)
{
  return String("/boards/") + filename;
}

size_t boardDocCapacityForFile(File &f)
{
  size_t fileSize = (size_t)f.size();
  size_t cap = fileSize + 1024;
  if (cap < kBoardDocMinCapacity) cap = kBoardDocMinCapacity;
  if (cap > kBoardDocMaxCapacity) cap = kBoardDocMaxCapacity;
  return cap;
}

bool parseBoardFile(const String &path, JsonDocument &doc)
{
  File f = LittleFS.open(path, "r");
  if (!f)
  {
    return false;
  }

  size_t cap = boardDocCapacityForFile(f);
  doc.clear();

  JsonDocument localDoc;
  DeserializationError err = deserializeJson(localDoc, f);
  f.close();

  if (!err)
  {
    doc = localDoc;
    return true;
  }

  if (err == DeserializationError::NoMemory && cap < kBoardDocMaxCapacity)
  {
    File fRetry = LittleFS.open(path, "r");
    if (!fRetry)
    {
      return false;
    }

    JsonDocument retryDoc;
    err = deserializeJson(retryDoc, fRetry);
    fRetry.close();
    if (!err)
    {
      doc = retryDoc;
      return true;
    }
  }

  return false;
}

bool parseBoardMetadataFile(const String &path, JsonDocument &doc)
{
  File f = LittleFS.open(path, "r");
  if (!f)
  {
    return false;
  }

  doc.clear();

  JsonDocument filter;
  filter["name"] = true;
  filter["cpu"] = true;
  filter["relayCount"] = true;
  filter["outputType"] = true;

  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();
  if (!err)
  {
    return true;
  }

  return false;
}

bool writeBoardJson(const String &filename, const JsonDocument &doc)
{
  String finalPath = boardPathFromFilename(filename);
  String tempPath = finalPath.substring(0, finalPath.length() - 5) + ".jt";

  File f = LittleFS.open(tempPath, "w");
  if (!f)
  {
    return false;
  }

  size_t written = serializeJson(doc, f);
  if (!f || written == 0)
  {
    f.close();
    LittleFS.remove(tempPath);
    return false;
  }
  f.close();

  if (LittleFS.exists(finalPath) && !LittleFS.remove(finalPath))
  {
    LittleFS.remove(tempPath);
    return false;
  }

  if (!LittleFS.rename(tempPath, finalPath))
  {
    LittleFS.remove(tempPath);
    return false;
  }

  return true;
}

String detectCpuFromFilename(const String &filename)
{
  String lower = filename;
  lower.toLowerCase();
  if (lower.startsWith("esp8266-")) return "ESP8266";
  if (lower.startsWith("esp32-")) return "ESP32";
  return String(BOARD_CPU_TYPE);
}

bool cpuMatchesCurrentHardware(String cpu)
{
  cpu.trim();
  cpu.toUpperCase();

  String currentCpu = String(BOARD_CPU_TYPE);
  currentCpu.toUpperCase();
  return cpu == currentCpu;
}

uint8_t normalizeRelayCount(uint8_t rc)
{
  return (rc == kVariantRelayCount16) ? kVariantRelayCount16 : kVariantRelayCount8;
}

void fillBoardDefaults(JsonDocument &doc, const String &filename)
{
  String cpu = String(doc["cpu"] | "");
  cpu.trim();
  if (cpu.length() < 3)
  {
    cpu = detectCpuFromFilename(filename);
  }

  uint8_t relayCount = normalizeRelayCount((uint8_t)(doc["relayCount"] | kVariantRelayCount8));

  String outputType = String(doc["outputType"] | "");
  outputType.toLowerCase();
  if (outputType != "gpio" && outputType != "shiftregister")
  {
    outputType = (relayCount == kVariantRelayCount16) ? "shiftregister" : "gpio";
  }

  String name = String(doc["name"] | "");
  name.trim();
  if (name.length() == 0)
  {
    name = cpu + " " + String(relayCount) + "-Relay";
  }

  uint8_t ledPin = (uint8_t)(doc["ledPin"] | kDefaultBoardLedPin);

  doc["name"] = name;
  doc["cpu"] = cpu;
  doc["relayCount"] = relayCount;
  doc["ledPin"] = ledPin;
  doc["outputType"] = outputType;

  if (outputType == "gpio")
  {
    JsonArray relays = doc["relays"].as<JsonArray>();
    if (relays.isNull())
    {
      relays = doc["relays"].template to<JsonArray>();
    }

    while (relays.size() > relayCount)
    {
      relays.remove(relays.size() - 1);
    }

    for (uint8_t i = 0; i < relayCount; i++)
    {
      if (i >= relays.size())
      {
        JsonObject r = relays.add<JsonObject>();
        r["relay"] = i + 1;
        r["pin"] = (uint8_t)255;
      }
      else
      {
        JsonObject r = relays[i];
        r["relay"] = i + 1;
        r["pin"] = (uint8_t)(r["pin"] | (uint8_t)255);
      }
    }

    if (doc["shiftRegister"].is<JsonVariantConst>())
    {
      doc.remove("shiftRegister");
    }
  }
  else
  {
    JsonObject sr = doc["shiftRegister"].as<JsonObject>();
    if (sr.isNull()) {
      sr  = doc["shiftRegister"].to<JsonObject>();    
    }

    sr["latchPin"] = (uint8_t)(sr["latchPin"] | kDefaultShiftRegisterLatchPin);
    sr["clockPin"] = (uint8_t)(sr["clockPin"] | kDefaultShiftRegisterClockPin);
    sr["dataPin"] = (uint8_t)(sr["dataPin"] | kDefaultShiftRegisterDataPin);
    sr["oePin"] = (uint8_t)(sr["oePin"] | kDefaultShiftRegisterOePin);

    if (doc["relays"].is<JsonVariantConst>())
    {
      doc.remove("relays");
    }
  }
}

void addBoardListEntry(JsonArray &arr, const String &filename)
{
  String path = boardPathFromFilename(filename);

  JsonDocument entry;
  parseBoardMetadataFile(path, entry);

  fillBoardDefaults(entry, filename);

  String entryCpu = String(entry["cpu"] | detectCpuFromFilename(filename));
  if (!cpuMatchesCurrentHardware(entryCpu))
  {
    return;
  }

  JsonObject board = arr.add<JsonObject>();
  board["filename"] = String("boards/") + filename;
  board["name"] = String(entry["name"] | filename);
  board["cpu"] = entryCpu;
  board["relayCount"] = (uint8_t)(entry["relayCount"] | kVariantRelayCount8);
  board["outputType"] = String(entry["outputType"] | "gpio");
}

bool collectBoardDocumentFromRequest(AsyncWebServerRequest *request, JsonDocument &doc, String &outFilename)
{
  String filename = routeGetBodyParam(request, "filename");
  String title = routeGetBodyParam(request, "title");
  String normalizedFilename;

  if (filename.length() > 0)
  {
    if (!normalizeBoardFilename(filename, normalizedFilename))
    {
      return false;
    }
  }
  else
  {
    title.trim();
    if (title.length() == 0)
    {
      title = routeGetBodyParam(request, "name");
      title.trim();
    }
    if (title.length() == 0)
    {
      title = "board";
    }
    normalizedFilename = sanitizeBoardSlug(title) + ".json";
  }

  JsonDocument loaded;
  bool hasExisting = LittleFS.exists(boardPathFromFilename(normalizedFilename));
  if (hasExisting && !parseBoardFile(boardPathFromFilename(normalizedFilename), loaded))
  {
    return false;
  }

  doc.clear();
  if (hasExisting)
  {
    doc = loaded;
  }

  uint8_t relayCount = normalizeRelayCount((uint8_t)routeGetBodyParam(request, "relayCount").toInt());
  if (relayCount == 0)
  {
    relayCount = normalizeRelayCount((uint8_t)(doc["relayCount"] | kVariantRelayCount8));
  }

  String outputType = routeGetBodyParam(request, "outputType");
  outputType.toLowerCase();
  if (outputType != "gpio" && outputType != "shiftregister")
  {
    outputType = String(doc["outputType"] | "");
    outputType.toLowerCase();
  }
  if (outputType != "gpio" && outputType != "shiftregister")
  {
    outputType = (relayCount == kVariantRelayCount16) ? "shiftregister" : "gpio";
  }

  String boardName = routeGetBodyParam(request, "name");
  boardName.trim();
  if (boardName.length() == 0)
  {
    boardName = String(doc["name"] | "");
    boardName.trim();
  }

  String cpu = String(BOARD_CPU_TYPE);

  doc["name"] = boardName.length() ? boardName : (cpu + " " + String(relayCount) + "-Relay");
  doc["cpu"] = cpu;
  doc["relayCount"] = relayCount;
  doc["ledPin"] = (uint8_t)(routeGetBodyParam(request, "ledPin").length()
    ? routeGetBodyParam(request, "ledPin").toInt()
    : (uint8_t)(doc["ledPin"] | kDefaultBoardLedPin));
  doc["outputType"] = outputType;

  if (outputType == "gpio")
  {
    if (doc["shiftRegister"].is<JsonVariantConst>()) doc.remove("shiftRegister");
    JsonArray relays = doc["relays"].as<JsonArray>();
    if (relays.isNull()) relays = doc["relays"].template to<JsonArray>();

    while (relays.size() > relayCount) relays.remove(relays.size() - 1);

    for (uint8_t i = 1; i <= relayCount; i++)
    {
      String key = "relay" + String(i) + "_pin";
      uint8_t pin = (uint8_t)255;
      if (routeGetBodyParam(request, key.c_str()).length())
      {
        pin = (uint8_t)routeGetBodyParam(request, key.c_str()).toInt();
      }
      else if (i <= relays.size())
      {
        JsonObject existing = relays[i - 1];
        pin = (uint8_t)(existing["pin"] | (uint8_t)255);
      }

      if (i > relays.size())
      {
        JsonObject relay = relays.add<JsonObject>();
        relay["relay"] = i;
        relay["pin"] = pin;
      }
      else
      {
        JsonObject relay = relays[i - 1];
        relay["relay"] = i;
        relay["pin"] = pin;
      }
    }
  }
  else
  {
    if (doc["relays"].is<JsonVariantConst>()) doc.remove("relays");
    JsonObject sr = doc["shiftRegister"].as<JsonObject>();
    if (sr.isNull()) sr  = doc["shiftRegister"].to<JsonObject>();
    sr["latchPin"] = (uint8_t)(routeGetBodyParam(request, "sr_latchPin").length()
      ? routeGetBodyParam(request, "sr_latchPin").toInt()
      : (uint8_t)(sr["latchPin"] | kDefaultShiftRegisterLatchPin));
    sr["clockPin"] = (uint8_t)(routeGetBodyParam(request, "sr_clockPin").length()
      ? routeGetBodyParam(request, "sr_clockPin").toInt()
      : (uint8_t)(sr["clockPin"] | kDefaultShiftRegisterClockPin));
    sr["dataPin"] = (uint8_t)(routeGetBodyParam(request, "sr_dataPin").length()
      ? routeGetBodyParam(request, "sr_dataPin").toInt()
      : (uint8_t)(sr["dataPin"] | kDefaultShiftRegisterDataPin));
    sr["oePin"] = (uint8_t)(routeGetBodyParam(request, "sr_oePin").length()
      ? routeGetBodyParam(request, "sr_oePin").toInt()
      : (uint8_t)(sr["oePin"] | kDefaultShiftRegisterOePin));
  }

  fillBoardDefaults(doc, normalizedFilename);
  outFilename = normalizedFilename;
  return true;
}
}

void registerBoardRoutes()
{
  server.on("/api/boards", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["activeBoardFile"] = activeBoardHardwareFilename;

    JsonArray arr = doc["boards"].template to<JsonArray>();

    if (LittleFS.exists("/boards"))
    {
#if defined(ESP8266)
      Dir dir = LittleFS.openDir("/boards");
      while (dir.next())
      {
        if (!dir.isFile()) continue;
        String fname = dir.fileName();
        if (!fname.endsWith(".json")) continue;
        addBoardListEntry(arr, fname);
      }
#elif defined(ESP32)
      File root = LittleFS.open("/boards");
      if (root && root.isDirectory())
      {
        File f = root.openNextFile();
        while (f)
        {
          if (!f.isDirectory())
          {
            String fname = String(f.name());
            int slash = fname.lastIndexOf('/');
            if (slash >= 0) fname = fname.substring(slash + 1);
            if (fname.endsWith(".json"))
            {
              addBoardListEntry(arr, fname);
            }
          }
          f = root.openNextFile();
        }
      }
#endif
    }

    AsyncResponseStream *stream = request->beginResponseStream("application/json");
    serializeJson(doc, *stream);
    request->send(stream);
  });

  server.on("/api/boards", HTTP_POST, [](AsyncWebServerRequest *request) {
    StorageWriteLockGuard fsGuard;
    if (!fsGuard.acquired())
    {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"storage busy\",\"reason\":\"storage_write_lock\",\"retryAfterMs\":250}");
      return;
    }

    String action = routeGetBodyParam(request, "action");
    action.trim();
    action.toLowerCase();

    if (action.length() == 0)
    {
      action = "save";
    }

    if (!LittleFS.exists("/boards"))
    {
      LittleFS.mkdir("/boards");
    }

    if (action == "upload")
    {
      String rawContent = routeGetBodyParam(request, "content");
      if (rawContent.length() == 0)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"content required\"}");
        return;
      }

      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (filename.length() > 0)
      {
        if (!normalizeBoardFilename(filename, normalizedFilename))
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
          return;
        }
      }
      else
      {
        JsonDocument probe;
        if (deserializeJson(probe, rawContent) != DeserializationError::Ok)
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid board json\"}");
          return;
        }
        String title = String(probe["name"] | "board");
        title.trim();
        if (title.length() == 0) title = "board";
        normalizedFilename = sanitizeBoardSlug(title) + ".json";
      }

      JsonDocument inDoc;
      if (deserializeJson(inDoc, rawContent) != DeserializationError::Ok)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid board json\"}");
        return;
      }

      fillBoardDefaults(inDoc, normalizedFilename);

      String boardCpu = String(inDoc["cpu"] | "");
      if (!cpuMatchesCurrentHardware(boardCpu))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"board cpu incompatible with running hardware\",\"reason\":\"cpu_mismatch\"}");
        return;
      }

      if (!writeBoardJson(normalizedFilename, inDoc))
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
      }

      JsonDocument response;
      response["ok"] = true;
      response["filename"] = String("boards/") + normalizedFilename;
      char payload[128];
      serializeJson(response, payload, sizeof(payload));
      request->send(200, "application/json", payload);
      return;
    }

    if (action == "rename")
    {
      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (!normalizeBoardFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }

      String title = routeGetBodyParam(request, "title");
      title.trim();
      if (title.length() == 0)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"title required\"}");
        return;
      }

      String newFilename = sanitizeBoardSlug(title) + ".json";
      if (newFilename == normalizedFilename)
      {
        JsonDocument response;
        response["ok"] = true;
        response["filename"] = String("boards/") + newFilename;
        char payload[128];
        serializeJson(response, payload, sizeof(payload));
        request->send(200, "application/json", payload);
        return;
      }

      String oldPath = boardPathFromFilename(normalizedFilename);
      String newPath = boardPathFromFilename(newFilename);

      if (!LittleFS.exists(oldPath))
      {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"board not found\"}");
        return;
      }

      if (LittleFS.exists(newPath))
      {
        request->send(409, "application/json", "{\"ok\":false,\"error\":\"board already exists\"}");
        return;
      }

      JsonDocument doc;
      if (!parseBoardFile(oldPath, doc))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid board json\"}");
        return;
      }

      doc["name"] = title;
      fillBoardDefaults(doc, newFilename);

      if (!writeBoardJson(newFilename, doc))
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
      }

      LittleFS.remove(oldPath);

      JsonDocument response;
      response["ok"] = true;
      response["filename"] = String("boards/") + newFilename;
      char payload[128];
      serializeJson(response, payload, sizeof(payload));
      request->send(200, "application/json", payload);
      return;
    }

    if (action == "delete")
    {
      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (!normalizeBoardFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }

      String path = boardPathFromFilename(normalizedFilename);
      if (!LittleFS.exists(path))
      {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"board not found\"}");
        return;
      }

      if (!LittleFS.remove(path))
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove failed\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }

    if (action == "setactive")
    {
      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (!normalizeBoardFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }

      String path = boardPathFromFilename(normalizedFilename);
      if (!LittleFS.exists(path))
      {
        request->send(404, "application/json", "{\"ok\":false,\"error\":\"board not found\"}");
        return;
      }

      JsonDocument boardDoc;
      if (!parseBoardFile(path, boardDoc))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid board json\"}");
        return;
      }

      fillBoardDefaults(boardDoc, normalizedFilename);

      String boardCpu = String(boardDoc["cpu"] | "");
      if (!cpuMatchesCurrentHardware(boardCpu))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"board cpu incompatible with running hardware\",\"reason\":\"cpu_mismatch\"}");
        return;
      }

      uint8_t rc = normalizeRelayCount((uint8_t)(boardDoc["relayCount"] | kVariantRelayCount8));

      hardwareVariant = (rc == kVariantRelayCount16) ? String(kVariant16Relay) : String(kVariant8Relay);
      activeBoardHardwareFilename = path;
      applyHardwareVariantPinsAndModes();
      reconcileSelectedTemplateForActiveHardware(true);

      if (!saveBoardConfig())
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save active board\"}");
        return;
      }

      notifyClients();

      JsonDocument response;
      response["ok"] = true;
      response["activeBoardFile"] = activeBoardHardwareFilename;
      char payload[128];
      serializeJson(response, payload, sizeof(payload));
      request->send(200, "application/json", payload);
      return;
    }

    JsonDocument doc;
    String normalizedFilename;
    if (!collectBoardDocumentFromRequest(request, doc, normalizedFilename))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid board payload\"}");
      return;
    }

    if (!writeBoardJson(normalizedFilename, doc))
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    String activeFilename = boardHardwarePath(hardwareVariant);
    if (activeFilename.startsWith("/"))
    {
      activeFilename = activeFilename.substring(1);
    }
    if (activeFilename == (String("boards/") + normalizedFilename))
    {
      loadBoardHardware(hardwareVariant);
    }

    JsonDocument response;
    response["ok"] = true;
    response["filename"] = String("boards/") + normalizedFilename;
    char payload[128];
    serializeJson(response, payload, sizeof(payload));
    request->send(200, "application/json", payload);
  });
}
