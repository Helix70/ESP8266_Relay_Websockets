#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_state.h"
#include "config_routes_internal.h"
#include "config_store.h"
#include "route_data.h"
#include "route_utils.h"
#include "storage_lock.h"
#include "storage_utils.h"
#include "web_runtime.h"

namespace
{
constexpr size_t kTemplateWriteSafetyBytes = 512;
String gLastTemplateWriteErrorReason;
uint32_t gLastTemplateWriteErrorAtMs = 0;

struct FsUsage
{
  size_t total = 0;
  size_t used = 0;
  size_t free = 0;
};

bool readLittleFsUsage(FsUsage &usage)
{
#if defined(ESP8266)
  FSInfo info;
  if (!LittleFS.info(info))
  {
    return false;
  }
  usage.total = (size_t)info.totalBytes;
  usage.used = (size_t)info.usedBytes;
#elif defined(ESP32)
  usage.total = (size_t)LittleFS.totalBytes();
  usage.used = (size_t)LittleFS.usedBytes();
#else
  usage.total = 0;
  usage.used = 0;
#endif

  usage.free = (usage.total > usage.used) ? (usage.total - usage.used) : 0;
  return usage.total > 0;
}

void addFsUsageToResponse(JsonDocument &response)
{
  FsUsage usage;
  if (!readLittleFsUsage(usage))
  {
    return;
  }

  response["fsTotalBytes"] = usage.total;
  response["fsUsedBytes"] = usage.used;
  response["fsFreeBytes"] = usage.free;
}

String buildTemplateWriteFailurePayload(const String &reason, size_t requiredBytes = 0)
{
  DynamicJsonDocument response(512);
  response["ok"] = false;
  response["error"] = (reason == "insufficient_space") ? "insufficient storage" : "save failed";
  response["reason"] = reason;
  if (requiredBytes > 0)
  {
    response["requiredBytes"] = requiredBytes;
  }
  addFsUsageToResponse(response);

  String payload;
  serializeJson(response, payload);
  return payload;
}

void sendTemplateWriteFailure(AsyncWebServerRequest *request, const String &reason, size_t requiredBytes = 0)
{
  gLastTemplateWriteErrorReason = reason;
  gLastTemplateWriteErrorAtMs = millis();
  int status = (reason == "insufficient_space") ? 507 : 500;
  request->send(status, "application/json", buildTemplateWriteFailurePayload(reason, requiredBytes));
}

bool ensureTemplateWriteHeadroom(size_t requiredBytes, String &failureReason)
{
  FsUsage usage;
  if (!readLittleFsUsage(usage))
  {
    return true;
  }

  size_t needed = requiredBytes + kTemplateWriteSafetyBytes;
  if (usage.free < needed)
  {
    failureReason = "insufficient_space";
    return false;
  }

  return true;
}

size_t estimateTemplateWriteBytesFromLabels(const String &title, uint8_t rc, const JsonArray &labels)
{
  size_t estimate = 192 + (size_t)title.length() * 2 + (size_t)rc * 48;
  for (uint8_t i = 0; i < rc; i++)
  {
    JsonObject src = labels[i];
    estimate += (size_t)String(src["on"] | "").length() * 2;
    estimate += (size_t)String(src["off"] | "").length() * 2;
    estimate += (size_t)String(src["mode"] | "latched").length() * 2;
  }
  return estimate;
}

size_t estimateTemplateWriteBytesFromRequest(AsyncWebServerRequest *request, const String &title, uint8_t rc)
{
  size_t estimate = 192 + (size_t)title.length() * 2 + (size_t)rc * 48;
  for (uint8_t i = 1; i <= rc; i++)
  {
    String prefix = "relay" + String(i);
    estimate += (size_t)routeGetBodyParam(request, (prefix + "_on").c_str()).length() * 2;
    estimate += (size_t)routeGetBodyParam(request, (prefix + "_off").c_str()).length() * 2;
    estimate += (size_t)routeGetBodyParam(request, (prefix + "_mode").c_str()).length() * 2;
  }
  return estimate;
}

bool openTemplateTempForWrite(const String &tempPath, File &outFile)
{
  for (uint8_t attempt = 0; attempt < 4; attempt++)
  {
    outFile = LittleFS.open(tempPath, "w");
    if (outFile)
    {
      return true;
    }
    delay(20);
    yield();
  }

  return false;
}

void computeTemplateStorageStats(size_t &templateCount, size_t &largestTemplateBytes)
{
  templateCount = 0;
  largestTemplateBytes = 0;

  if (!LittleFS.exists("/templates"))
  {
    return;
  }

#if defined(ESP8266)
  Dir dir = LittleFS.openDir("/templates");
  while (dir.next())
  {
    if (!dir.isFile()) continue;
    String fname = dir.fileName();
    if (!fname.endsWith(".json")) continue;

    File f = dir.openFile("r");
    if (!f) continue;
    size_t sz = (size_t)f.size();
    f.close();

    templateCount++;
    if (sz > largestTemplateBytes) largestTemplateBytes = sz;
  }
#elif defined(ESP32)
  File root = LittleFS.open("/templates");
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
          size_t sz = (size_t)f.size();
          templateCount++;
          if (sz > largestTemplateBytes) largestTemplateBytes = sz;
        }
      }
      f = root.openNextFile();
    }
  }
#endif
}

bool templateFileExists(const String &path)
{
  if (LittleFS.exists(path))
  {
    return true;
  }

  File probe = LittleFS.open(path, "r");
  if (!probe)
  {
    return false;
  }

  probe.close();
  return true;
}

String buildTemplateNotFoundPayload(const String &action, const String &rawFilename, const String &normalizedFilename, const String &path)
{
  DynamicJsonDocument response(640);
  response["ok"] = false;
  response["error"] = "template not found";
  response["reason"] = "missing_template_file";
  response["action"] = action;
  response["rawFilename"] = rawFilename;
  response["normalizedFilename"] = normalizedFilename;
  response["path"] = path;
  response["pathExists"] = LittleFS.exists(path);
  response["selectedTemplate"] = selectedRelayTemplateFilename;
  response["selectedMatches"] = (selectedRelayTemplateFilename == normalizedFilename);

  size_t templateCount = 0;
  size_t largestTemplateBytes = 0;
  computeTemplateStorageStats(templateCount, largestTemplateBytes);
  response["templateCount"] = templateCount;
  response["largestTemplateBytes"] = largestTemplateBytes;

  addFsUsageToResponse(response);

  String payload;
  serializeJson(response, payload);
  return payload;
}

String sanitizeTemplateSlug(const String &title)
{
  String out;
  for (size_t i = 0; i < title.length() && out.length() < kMaxTemplateFilenameLength; i++)
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
    out = "template";
  }

  return out;
}

bool normalizeTemplateFilename(String raw, String &normalized)
{
  raw.trim();
  if (raw.startsWith("/templates/"))
  {
    raw = raw.substring(String("/templates/").length());
  }

  if (raw.length() == 0 || raw.length() > (kMaxTemplateFilenameLength + 5))
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

String buildTemplatePath(const String &filename)
{
  return String("/templates/") + filename;
}

void writeEscapedJsonString(Print &out, const String &value)
{
  out.print('"');
  for (size_t i = 0; i < value.length(); i++)
  {
    uint8_t c = (uint8_t)value[i];
    if (c == '"')
    {
      out.print("\\\"");
    }
    else if (c == '\\')
    {
      out.print("\\\\");
    }
    else if (c == '\b')
    {
      out.print("\\b");
    }
    else if (c == '\f')
    {
      out.print("\\f");
    }
    else if (c == '\n')
    {
      out.print("\\n");
    }
    else if (c == '\r')
    {
      out.print("\\r");
    }
    else if (c == '\t')
    {
      out.print("\\t");
    }
    else if (c < 0x20)
    {
      char hex[7];
      snprintf(hex, sizeof(hex), "\\u%04X", c);
      out.print(hex);
    }
    else
    {
      out.write(c);
    }
  }
  out.print('"');
}

void addTemplateListEntry(JsonArray &arr, const String &filename, File &file)
{
  String title = filename;
  uint8_t relayCount = (filename.indexOf("16") >= 0) ? kVariantRelayCount16 : kVariantRelayCount8;

  // Fast-path metadata extraction: only inspect a small prefix of each file.
  // This avoids parsing large label arrays/help blocks when listing templates.
  String probe;
  probe.reserve(640);
  while (file.available() && probe.length() < 640)
  {
    probe += (char)file.read();
  }

  int titleKey = probe.indexOf("\"title\"");
  if (titleKey >= 0)
  {
    int colon = probe.indexOf(':', titleKey);
    if (colon > 0)
    {
      int firstQuote = probe.indexOf('"', colon + 1);
      int secondQuote = (firstQuote >= 0) ? probe.indexOf('"', firstQuote + 1) : -1;
      if (firstQuote >= 0 && secondQuote > firstQuote)
      {
        String parsedTitle = probe.substring(firstQuote + 1, secondQuote);
        parsedTitle.trim();
        if (parsedTitle.length() > 0)
        {
          title = parsedTitle;
        }
      }
    }
  }

  int relayKey = probe.indexOf("\"relayCount\"");
  if (relayKey >= 0)
  {
    int colon = probe.indexOf(':', relayKey);
    if (colon > 0)
    {
      String parsedNumber;
      for (int i = colon + 1; i < (int)probe.length(); i++)
      {
        char c = probe[i];
        if (c >= '0' && c <= '9')
        {
          parsedNumber += c;
        }
        else if (parsedNumber.length() > 0)
        {
          break;
        }
      }

      int parsedRelayCount = parsedNumber.toInt();
      if (parsedRelayCount == (int)kVariantRelayCount16)
      {
        relayCount = kVariantRelayCount16;
      }
      else if (parsedRelayCount == (int)kVariantRelayCount8)
      {
        relayCount = kVariantRelayCount8;
      }
    }
  }

  JsonObject entry = arr.createNestedObject();
  entry["filename"] = filename;
  entry["title"] = title;
  entry["relayCount"] = relayCount;
}

bool writeTemplateJson(const String &filename, const String &title, uint8_t rc, const JsonArray &labels, String *failureReason = nullptr)
{
  String finalPath = buildTemplatePath(filename);
  String tempPath = finalPath + ".tmp";

  File f;
  if (!openTemplateTempForWrite(tempPath, f))
  {
    if (failureReason) *failureReason = "temp_open_failed";
    return false;
  }

  bool ok = true;

  f.println("{");
  f.print("  \"title\": ");
  writeEscapedJsonString(f, title);
  f.println(",");
  f.print("  \"relayCount\": ");
  f.print(rc);
  f.println(",");
  f.println("  \"labels\": [");

  for (uint8_t i = 0; i < rc; i++)
  {
    JsonObject src = labels[i];

    String on = String(src["on"] | "");
    String off = String(src["off"] | "");
    on.trim();
    off.trim();

    String mode = String(src["mode"] | "latched");
    if (mode != "interlocked" && mode != "pulsed") mode = "latched";

    uint8_t group = (uint8_t)(src["group"] | (uint8_t)0);
    uint8_t pulseTimeout = (uint8_t)(src["pulseTimeout"] | (uint8_t)0);
    if (mode == "pulsed")
    {
      if (pulseTimeout == 0 || pulseTimeout > kMaxPulseTimeoutSeconds)
      {
        pulseTimeout = kDefaultPulseTimeoutSeconds;
      }
    }
    else
    {
      pulseTimeout = 0;
    }

    f.print("    {\"on\": ");
    writeEscapedJsonString(f, on);
    f.print(", \"off\": ");
    writeEscapedJsonString(f, off);
    f.print(", \"mode\": ");
    writeEscapedJsonString(f, mode);
    f.print(", \"group\": ");
    f.print(group);
    f.print(", \"pulseTimeout\": ");
    f.print(pulseTimeout);
    f.print("}");
    if (i + 1 < rc)
    {
      f.print(",");
    }
    f.println();
  }

  f.println("  ]");
  f.println("}");

  if (!f)
  {
    ok = false;
  }
  f.close();

  if (!ok)
  {
    if (failureReason) *failureReason = "temp_write_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  if (LittleFS.exists(finalPath) && !LittleFS.remove(finalPath))
  {
    if (failureReason) *failureReason = "remove_existing_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  if (!LittleFS.rename(tempPath, finalPath))
  {
    if (failureReason) *failureReason = "rename_tmp_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  return true;
}

bool writeTemplateJsonFromRequest(AsyncWebServerRequest *request, const String &filename, const String &title, uint8_t rc, String *failureReason = nullptr)
{
  String finalPath = buildTemplatePath(filename);
  String tempPath = finalPath + ".tmp";

  File f;
  if (!openTemplateTempForWrite(tempPath, f))
  {
    if (failureReason) *failureReason = "temp_open_failed";
    return false;
  }

  bool ok = true;

  f.println("{");
  f.print("  \"title\": ");
  writeEscapedJsonString(f, title);
  f.println(",");
  f.print("  \"relayCount\": ");
  f.print(rc);
  f.println(",");
  f.println("  \"labels\": [");

  for (uint8_t i = 1; i <= rc; i++)
  {
    String prefix = "relay" + String(i);
    String onLabel = routeGetBodyParam(request, (prefix + "_on").c_str());
    String offLabel = routeGetBodyParam(request, (prefix + "_off").c_str());
    onLabel.trim();
    offLabel.trim();

    String modeStr = routeGetBodyParam(request, (prefix + "_mode").c_str());
    if (modeStr != "interlocked" && modeStr != "pulsed") modeStr = "latched";

    uint8_t group = (uint8_t)routeGetBodyParam(request, (prefix + "_group").c_str()).toInt();
    uint8_t pt = (uint8_t)routeGetBodyParam(request, (prefix + "_pulseTimeout").c_str()).toInt();
    if (modeStr == "pulsed")
    {
      if (pt == 0 || pt > kMaxPulseTimeoutSeconds) pt = kDefaultPulseTimeoutSeconds;
    }
    else
    {
      pt = 0;
    }

    f.print("    {\"on\": ");
    writeEscapedJsonString(f, onLabel);
    f.print(", \"off\": ");
    writeEscapedJsonString(f, offLabel);
    f.print(", \"mode\": ");
    writeEscapedJsonString(f, modeStr);
    f.print(", \"group\": ");
    f.print(group);
    f.print(", \"pulseTimeout\": ");
    f.print(pt);
    f.print("}");
    if (i < rc)
    {
      f.print(",");
    }
    f.println();
  }

  f.println("  ]");
  f.println("}");

  if (!f)
  {
    ok = false;
  }
  f.close();

  if (!ok)
  {
    if (failureReason) *failureReason = "temp_write_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  if (LittleFS.exists(finalPath) && !LittleFS.remove(finalPath))
  {
    if (failureReason) *failureReason = "remove_existing_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  if (!LittleFS.rename(tempPath, finalPath))
  {
    if (failureReason) *failureReason = "rename_tmp_failed";
    LittleFS.remove(tempPath);
    return false;
  }

  return true;
}
}

void registerTemplateRoutes()
{
  server.on("/api/templates/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(768);
    doc["selectedTemplate"] = selectedRelayTemplateFilename;

    size_t templateCount = 0;
    size_t largestTemplateBytes = 0;
    computeTemplateStorageStats(templateCount, largestTemplateBytes);
    doc["templateCount"] = templateCount;
    doc["largestTemplateBytes"] = largestTemplateBytes;

    addFsUsageToResponse(doc);
    doc["lastWriteErrorReason"] = gLastTemplateWriteErrorReason;
    doc["lastWriteErrorAtMs"] = gLastTemplateWriteErrorAtMs;

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/templates", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    doc["selectedTemplate"] = selectedRelayTemplateFilename;
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

    if (action == "upload")
    {
      String rawContent = routeGetBodyParam(request, "content");
      if (rawContent.length() == 0)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"content required\"}");
        return;
      }

      DynamicJsonDocument inDoc(8192);
      if (deserializeJson(inDoc, rawContent) != DeserializationError::Ok)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid template json\"}");
        return;
      }

      JsonArray labels = inDoc["labels"].as<JsonArray>();
      if (labels.isNull() || labels.size() == 0)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"labels missing\"}");
        return;
      }

      uint8_t rc = (uint8_t)(inDoc["relayCount"] | (uint8_t)labels.size());
      if (rc == 0 || rc > APP_MAX_RELAYS)
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid relay count\"}");
        return;
      }

      String title = routeGetBodyParam(request, "title");
      title.trim();
      if (title.length() == 0)
      {
        title = String(inDoc["title"] | "");
        title.trim();
      }
      if (title.length() == 0)
      {
        title = "Imported Template";
      }
      if (title.length() > kMaxTemplateTitleLength)
      {
        title = title.substring(0, kMaxTemplateTitleLength);
      }

      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (filename.length() > 0)
      {
        if (!normalizeTemplateFilename(filename, normalizedFilename))
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
          return;
        }
      }
      else
      {
        normalizedFilename = sanitizeTemplateSlug(title) + ".json";
      }

      if (!LittleFS.exists("/templates"))
      {
        LittleFS.mkdir("/templates");
      }

      size_t estimatedBytes = estimateTemplateWriteBytesFromLabels(title, rc, labels);
      String failureReason;
      if (!ensureTemplateWriteHeadroom(estimatedBytes, failureReason))
      {
        sendTemplateWriteFailure(request, failureReason, estimatedBytes);
        return;
      }

      if (!writeTemplateJson(normalizedFilename, title, rc, labels, &failureReason))
      {
        sendTemplateWriteFailure(request, failureReason.length() ? failureReason : String("save_failed"), estimatedBytes);
        return;
      }

      DynamicJsonDocument response(256);
      response["ok"] = true;
      response["filename"] = normalizedFilename;
      String payload;
      serializeJson(response, payload);
      request->send(200, "application/json", payload);
      return;
    }

    if (action == "select" || action == "setactive")
    {
      String filename = routeGetBodyParam(request, "filename");
      filename.trim();

      if (filename.length() == 0)
      {
        selectedRelayTemplateFilename = "";
        if (!saveBoardConfig())
        {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
          return;
        }
        notifyClients();
        request->send(200, "application/json", "{\"ok\":true,\"selectedTemplate\":\"\"}");
        return;
      }

      String normalizedFilename;
      if (!normalizeTemplateFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }

      String path = buildTemplatePath(normalizedFilename);
      if (!templateFileExists(path))
      {
        request->send(404, "application/json", buildTemplateNotFoundPayload("setactive", filename, normalizedFilename, path));
        return;
      }

      if (!loadLabelsFromTemplateFile(normalizedFilename, relayCount))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"template incompatible or invalid\"}");
        return;
      }

      if (!saveRelayLabels())
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save relay labels\"}");
        return;
      }

      selectedRelayTemplateFilename = normalizedFilename;
      if (!saveBoardConfig())
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
        return;
      }

      notifyClients();

      DynamicJsonDocument response(256);
      response["ok"] = true;
      response["selectedTemplate"] = selectedRelayTemplateFilename;
      String payload;
      serializeJson(response, payload);
      request->send(200, "application/json", payload);
      return;
    }

    if (action == "delete")
    {
      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (!normalizeTemplateFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }

      String path = buildTemplatePath(normalizedFilename);
      if (!templateFileExists(path))
      {
        request->send(404, "application/json", buildTemplateNotFoundPayload("delete", filename, normalizedFilename, path));
        return;
      }

      if (!LittleFS.remove(path))
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove failed\"}");
        return;
      }

      if (selectedRelayTemplateFilename == normalizedFilename)
      {
        selectedRelayTemplateFilename = "";
        if (!saveBoardConfig())
        {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
          return;
        }
        notifyClients();
      }

      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }

    if (action == "rename")
    {
      String filename = routeGetBodyParam(request, "filename");
      String normalizedFilename;
      if (!normalizeTemplateFilename(filename, normalizedFilename))
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
      if (title.length() > kMaxTemplateTitleLength)
      {
        title = title.substring(0, kMaxTemplateTitleLength);
      }

      String oldPath = buildTemplatePath(normalizedFilename);
      if (!templateFileExists(oldPath))
      {
        request->send(404, "application/json", buildTemplateNotFoundPayload("rename", filename, normalizedFilename, oldPath));
        return;
      }

      String newFilename = sanitizeTemplateSlug(title) + ".json";
      String newPath = buildTemplatePath(newFilename);
      if (newFilename != normalizedFilename && LittleFS.exists(newPath))
      {
        request->send(409, "application/json", "{\"ok\":false,\"error\":\"template already exists\"}");
        return;
      }

      bool removeOldFile = false;
      {
        File inFile = LittleFS.open(oldPath, "r");
        if (!inFile)
        {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"open failed\"}");
          return;
        }

        DynamicJsonDocument inDoc(4096);
        DeserializationError inErr = deserializeJson(inDoc, inFile);
        inFile.close();
        if (inErr != DeserializationError::Ok)
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid template json\"}");
          return;
        }

        JsonArray labels = inDoc["labels"].as<JsonArray>();
        if (labels.isNull() || labels.size() == 0)
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"labels missing\"}");
          return;
        }

        uint8_t rc = (uint8_t)(inDoc["relayCount"] | (uint8_t)labels.size());
        if (rc == 0 || rc > APP_MAX_RELAYS)
        {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid relay count\"}");
          return;
        }

        String existingTitle = String(inDoc["title"] | "");
        existingTitle.trim();

        if (newFilename == normalizedFilename && existingTitle == title)
        {
          DynamicJsonDocument response(256);
          response["ok"] = true;
          response["filename"] = newFilename;
          String payload;
          serializeJson(response, payload);
          request->send(200, "application/json", payload);
          return;
        }

        if (newFilename != normalizedFilename && existingTitle == title)
        {
          if (!LittleFS.rename(oldPath, newPath))
          {
            sendTemplateWriteFailure(request, "direct_rename_failed");
            return;
          }

          if (selectedRelayTemplateFilename == normalizedFilename)
          {
            selectedRelayTemplateFilename = newFilename;
            if (!saveBoardConfig())
            {
              request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
              return;
            }
            notifyClients();
          }

          DynamicJsonDocument response(256);
          response["ok"] = true;
          response["filename"] = newFilename;
          String payload;
          serializeJson(response, payload);
          request->send(200, "application/json", payload);
          return;
        }

        size_t estimatedBytes = estimateTemplateWriteBytesFromLabels(title, rc, labels);
        String failureReason;
        if (!ensureTemplateWriteHeadroom(estimatedBytes, failureReason))
        {
          sendTemplateWriteFailure(request, failureReason, estimatedBytes);
          return;
        }

        if (!writeTemplateJson(newFilename, title, rc, labels, &failureReason))
        {
          sendTemplateWriteFailure(request, failureReason.length() ? failureReason : String("save_failed"), estimatedBytes);
          return;
        }

        removeOldFile = (newFilename != normalizedFilename);
      }

      if (removeOldFile)
      {
        if (!LittleFS.remove(oldPath))
        {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove old file failed\"}");
          return;
        }
      }

      if (selectedRelayTemplateFilename == normalizedFilename)
      {
        selectedRelayTemplateFilename = newFilename;
        if (!saveBoardConfig())
        {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
          return;
        }
        notifyClients();
      }

      DynamicJsonDocument response(256);
      response["ok"] = true;
      response["filename"] = newFilename;
      String payload;
      serializeJson(response, payload);
      request->send(200, "application/json", payload);
      return;
    }

    String title = routeGetBodyParam(request, "title");
    title.trim();
    if (title.length() == 0) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"title required\"}");
      return;
    }
    if (title.length() > kMaxTemplateTitleLength) title = title.substring(0, kMaxTemplateTitleLength);

    int rc = routeGetBodyParam(request, "relayCount").toInt();
    if (rc <= 0 || rc > APP_MAX_RELAYS) rc = (int)relayCount;

    String filename = sanitizeTemplateSlug(title) + ".json";

    if (!LittleFS.exists("/templates")) {
      LittleFS.mkdir("/templates");
    }

    size_t estimatedBytes = estimateTemplateWriteBytesFromRequest(request, title, (uint8_t)rc);
    String failureReason;
    if (!ensureTemplateWriteHeadroom(estimatedBytes, failureReason))
    {
      sendTemplateWriteFailure(request, failureReason, estimatedBytes);
      return;
    }

    if (!writeTemplateJsonFromRequest(request, filename, title, (uint8_t)rc, &failureReason)) {
      sendTemplateWriteFailure(request, failureReason.length() ? failureReason : String("save_failed"), estimatedBytes);
      return;
    }

    DynamicJsonDocument response(256);
    response["ok"] = true;
    response["filename"] = filename;
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/templates/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    StorageWriteLockGuard fsGuard;
    if (!fsGuard.acquired())
    {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"storage busy\",\"reason\":\"storage_write_lock\",\"retryAfterMs\":250}");
      return;
    }

    String rawContent = routeGetBodyParam(request, "content");
    if (rawContent.length() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"content required\"}");
      return;
    }

    DynamicJsonDocument inDoc(8192);
    if (deserializeJson(inDoc, rawContent) != DeserializationError::Ok)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid template json\"}");
      return;
    }

    JsonArray labels = inDoc["labels"].as<JsonArray>();
    if (labels.isNull() || labels.size() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"labels missing\"}");
      return;
    }

    uint8_t rc = (uint8_t)(inDoc["relayCount"] | (uint8_t)labels.size());
    if (rc == 0 || rc > APP_MAX_RELAYS)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid relay count\"}");
      return;
    }

    String title = routeGetBodyParam(request, "title");
    title.trim();
    if (title.length() == 0)
    {
      title = String(inDoc["title"] | "");
      title.trim();
    }
    if (title.length() == 0)
    {
      title = "Imported Template";
    }
    if (title.length() > kMaxTemplateTitleLength)
    {
      title = title.substring(0, kMaxTemplateTitleLength);
    }

    String filename = routeGetBodyParam(request, "filename");
    String normalizedFilename;
    if (filename.length() > 0)
    {
      if (!normalizeTemplateFilename(filename, normalizedFilename))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
        return;
      }
    }
    else
    {
      normalizedFilename = sanitizeTemplateSlug(title) + ".json";
    }

    if (!LittleFS.exists("/templates"))
    {
      LittleFS.mkdir("/templates");
    }

    size_t estimatedBytes = estimateTemplateWriteBytesFromLabels(title, rc, labels);
    String failureReason;
    if (!ensureTemplateWriteHeadroom(estimatedBytes, failureReason))
    {
      sendTemplateWriteFailure(request, failureReason, estimatedBytes);
      return;
    }

    if (!writeTemplateJson(normalizedFilename, title, rc, labels, &failureReason))
    {
      sendTemplateWriteFailure(request, failureReason.length() ? failureReason : String("save_failed"), estimatedBytes);
      return;
    }

    DynamicJsonDocument response(256);
    response["ok"] = true;
    response["filename"] = normalizedFilename;
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/templates/select", HTTP_POST, [](AsyncWebServerRequest *request) {
    StorageWriteLockGuard fsGuard;
    if (!fsGuard.acquired())
    {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"storage busy\",\"reason\":\"storage_write_lock\",\"retryAfterMs\":250}");
      return;
    }

    String filename = routeGetBodyParam(request, "filename");
    filename.trim();

    if (filename.length() == 0)
    {
      selectedRelayTemplateFilename = "";
      if (!saveBoardConfig())
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
      }
      notifyClients();
      request->send(200, "application/json", "{\"ok\":true,\"selectedTemplate\":\"\"}");
      return;
    }

    String normalizedFilename;
    if (!normalizeTemplateFilename(filename, normalizedFilename))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
      return;
    }

    if (!LittleFS.exists(buildTemplatePath(normalizedFilename)))
    {
      request->send(404, "application/json", "{\"ok\":false,\"error\":\"template not found\"}");
      return;
    }

    if (!loadLabelsFromTemplateFile(normalizedFilename, relayCount))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"template incompatible or invalid\"}");
      return;
    }

    if (!saveRelayLabels())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save relay labels\"}");
      return;
    }

    selectedRelayTemplateFilename = normalizedFilename;
    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
      return;
    }

    notifyClients();

    DynamicJsonDocument response(256);
    response["ok"] = true;
    response["selectedTemplate"] = selectedRelayTemplateFilename;
    String payload;
    serializeJson(response, payload);
    request->send(200, "application/json", payload);
  });

  server.on("/api/templates", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    StorageWriteLockGuard fsGuard;
    if (!fsGuard.acquired())
    {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"storage busy\",\"reason\":\"storage_write_lock\",\"retryAfterMs\":250}");
      return;
    }

    if (!request->hasParam("filename"))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"filename required\"}");
      return;
    }

    String filename = request->getParam("filename")->value();
    String normalizedFilename;
    if (!normalizeTemplateFilename(filename, normalizedFilename))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
      return;
    }

    String path = buildTemplatePath(normalizedFilename);
    if (!LittleFS.exists(path))
    {
      request->send(404, "application/json", "{\"ok\":false,\"error\":\"template not found\"}");
      return;
    }

    if (!LittleFS.remove(path))
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove failed\"}");
      return;
    }

    if (selectedRelayTemplateFilename == normalizedFilename)
    {
      selectedRelayTemplateFilename = "";
      if (!saveBoardConfig())
      {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to save selection\"}");
        return;
      }
      notifyClients();
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });
}
