#include "storage_utils.h"

#include "LittleFS.h"
#include <ArduinoJson.h>
#include <string.h>

#include "route_data.h"

#if defined(ESP8266)
#include <EEPROM.h>
#endif

#if defined(ESP32)
#include <Preferences.h>
#endif

extern RelayLabel relayLabels[16];

namespace
{
constexpr uint8_t kMaxRelays = 16;
constexpr size_t kMaxRelayLabelLength = 32;

#if defined(ESP8266)
constexpr size_t kEepromTotalSize = 4096;
constexpr int kRelayLabelsEepromBase = 2048;
constexpr size_t kRelayLabelsEepromRegionSize = 2048;
constexpr uint32_t kRelayLabelsMagic = 0x31534C52u; // "RLS1"
constexpr uint16_t kRelayLabelsVersion = 1;
constexpr int kRelayLabelsHeaderOffsetMagic = kRelayLabelsEepromBase + 0;
constexpr int kRelayLabelsHeaderOffsetVersion = kRelayLabelsEepromBase + 4;
constexpr int kRelayLabelsHeaderOffsetCount = kRelayLabelsEepromBase + 6;
constexpr int kRelayLabelsHeaderOffsetCrc = kRelayLabelsEepromBase + 7;
constexpr int kRelayLabelsPayloadOffset = kRelayLabelsEepromBase + 9;
constexpr size_t kRelayLabelNameBytes = kMaxRelayLabelLength + 1;
constexpr size_t kRelayLabelRecordSize = 3 + (kRelayLabelNameBytes * 2);
constexpr size_t kRelayLabelsMaxPayload = kRelayLabelsEepromRegionSize - 9;

uint16_t labelsCrc16Ccitt(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++)
    {
      if (crc & 0x8000u)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void labelsEepromWriteU16(int offset, uint16_t value)
{
  EEPROM.write(offset, (uint8_t)(value & 0xFFu));
  EEPROM.write(offset + 1, (uint8_t)((value >> 8) & 0xFFu));
}

void labelsEepromWriteU32(int offset, uint32_t value)
{
  EEPROM.write(offset, (uint8_t)(value & 0xFFu));
  EEPROM.write(offset + 1, (uint8_t)((value >> 8) & 0xFFu));
  EEPROM.write(offset + 2, (uint8_t)((value >> 16) & 0xFFu));
  EEPROM.write(offset + 3, (uint8_t)((value >> 24) & 0xFFu));
}

uint16_t labelsEepromReadU16(int offset)
{
  return (uint16_t)EEPROM.read(offset) |
         (uint16_t)((uint16_t)EEPROM.read(offset + 1) << 8);
}

uint32_t labelsEepromReadU32(int offset)
{
  return (uint32_t)EEPROM.read(offset) |
         ((uint32_t)EEPROM.read(offset + 1) << 8) |
         ((uint32_t)EEPROM.read(offset + 2) << 16) |
         ((uint32_t)EEPROM.read(offset + 3) << 24);
}

String labelStringFromFixed(const uint8_t *src)
{
  size_t len = 0;
  while (len < kRelayLabelNameBytes && src[len] != 0)
  {
    len++;
  }

  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++)
  {
    out += (char)src[i];
  }
  out.trim();
  return out;
}

void labelStringToFixed(const String &src, uint8_t *dst)
{
  String cleaned = src;
  cleaned.trim();
  if (cleaned.length() > kMaxRelayLabelLength)
  {
    cleaned = cleaned.substring(0, kMaxRelayLabelLength);
  }

  memset(dst, 0, kRelayLabelNameBytes);
  for (size_t i = 0; i < cleaned.length() && i < kMaxRelayLabelLength; i++)
  {
    dst[i] = (uint8_t)cleaned[i];
  }
}

bool saveRelayLabelsToEeprom()
{
  const uint8_t count = kMaxRelays;
  const size_t payloadLen = (size_t)count * kRelayLabelRecordSize;
  if (payloadLen > kRelayLabelsMaxPayload)
  {
    return false;
  }

  uint8_t payload[kMaxRelays * kRelayLabelRecordSize];
  memset(payload, 0, sizeof(payload));

  for (uint8_t i = 0; i < count; i++)
  {
    size_t base = (size_t)i * kRelayLabelRecordSize;
    payload[base + 0] = relayLabels[i].mode;
    payload[base + 1] = relayLabels[i].group;
    payload[base + 2] = relayLabels[i].pulseTimeout;
    labelStringToFixed(relayLabels[i].on, payload + base + 3);
    labelStringToFixed(relayLabels[i].off, payload + base + 3 + kRelayLabelNameBytes);
  }

  uint16_t crc = labelsCrc16Ccitt(payload, payloadLen);

  EEPROM.begin((int)kEepromTotalSize);
  labelsEepromWriteU32(kRelayLabelsHeaderOffsetMagic, kRelayLabelsMagic);
  labelsEepromWriteU16(kRelayLabelsHeaderOffsetVersion, kRelayLabelsVersion);
  EEPROM.write(kRelayLabelsHeaderOffsetCount, count);
  labelsEepromWriteU16(kRelayLabelsHeaderOffsetCrc, crc);

  for (size_t i = 0; i < payloadLen; i++)
  {
    EEPROM.write(kRelayLabelsPayloadOffset + (int)i, payload[i]);
  }

  bool ok = EEPROM.commit();
  EEPROM.end();
  return ok;
}

bool loadRelayLabelsFromEeprom()
{
  EEPROM.begin((int)kEepromTotalSize);

  uint32_t magic = labelsEepromReadU32(kRelayLabelsHeaderOffsetMagic);
  uint16_t version = labelsEepromReadU16(kRelayLabelsHeaderOffsetVersion);
  uint8_t count = EEPROM.read(kRelayLabelsHeaderOffsetCount);
  uint16_t expectedCrc = labelsEepromReadU16(kRelayLabelsHeaderOffsetCrc);

  if (magic != kRelayLabelsMagic || version != kRelayLabelsVersion || count != kMaxRelays)
  {
    EEPROM.end();
    return false;
  }

  const size_t payloadLen = (size_t)count * kRelayLabelRecordSize;
  if (payloadLen > kRelayLabelsMaxPayload)
  {
    EEPROM.end();
    return false;
  }

  uint8_t payload[kMaxRelays * kRelayLabelRecordSize];
  for (size_t i = 0; i < payloadLen; i++)
  {
    payload[i] = EEPROM.read(kRelayLabelsPayloadOffset + (int)i);
  }
  EEPROM.end();

  uint16_t actualCrc = labelsCrc16Ccitt(payload, payloadLen);
  if (actualCrc != expectedCrc)
  {
    return false;
  }

  bool anyFound = false;
  for (uint8_t i = 0; i < count; i++)
  {
    size_t base = (size_t)i * kRelayLabelRecordSize;
    uint8_t mode = payload[base + 0];
    uint8_t group = payload[base + 1];
    uint8_t pulseTimeout = payload[base + 2];

    String on = labelStringFromFixed(payload + base + 3);
    String off = labelStringFromFixed(payload + base + 3 + kRelayLabelNameBytes);

    if (on.length() > 0 || off.length() > 0)
    {
      anyFound = true;
    }

    assignRelayLabels(i + 1, on, off);
    assignRelayMode(i + 1, mode, group, pulseTimeout);
  }

  return anyFound;
}
#endif

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
}

extern const char *kRelayLabelsPath;
extern uint8_t relayCount;
extern RelayLabel relayLabels[kMaxRelays];

bool initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("Cannot mount LittleFS volume; filesystem-backed routes disabled");
    return false;
  }
  Serial.println("LittleFS volume mounted...");
  return true;
}

#if !defined(ESP8266) && !defined(ESP32)
static const char *modeToStr(uint8_t mode)
{
  if (mode == RELAY_MODE_INTERLOCKED) return "I";
  if (mode == RELAY_MODE_PULSED)      return "P";
  return "L";
}
#endif

static uint8_t strToMode(const String &s)
{
  if (s == "I") return RELAY_MODE_INTERLOCKED;
  if (s == "P") return RELAY_MODE_PULSED;
  return RELAY_MODE_ONOFF;
}

void assignRelayMode(uint8_t relayNum, uint8_t mode, uint8_t group, uint8_t pulseTimeout)
{
  if (relayNum < 1 || relayNum > kMaxRelays) return;
  uint8_t idx = relayNum - 1;
  relayLabels[idx].mode         = (mode <= RELAY_MODE_PULSED) ? mode : RELAY_MODE_ONOFF;
  relayLabels[idx].group        = group;
  if (relayLabels[idx].mode == RELAY_MODE_PULSED)
  {
    relayLabels[idx].pulseTimeout = (pulseTimeout >= 1 && pulseTimeout <= kMaxPulseTimeoutSeconds)
                                      ? pulseTimeout
                                      : kDefaultPulseTimeoutSeconds;
  }
  else
  {
    relayLabels[idx].pulseTimeout = 0;
  }
}

void assignRelayLabels(uint8_t relayNum, const String &requestedOnLabel, const String &requestedOffLabel)
{
  String normalizedOff = clampRelayLabel(requestedOffLabel, relayNum, false);
  String normalizedOn = requestedOnLabel;
  normalizedOn.trim();

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
  for (uint8_t i = 0; i < kMaxRelays; i++)
  {
    snprintf(key, sizeof(key), "on_%d", i);
    prefs.putString(key, relayLabels[i].on);
    snprintf(key, sizeof(key), "off_%d", i);
    prefs.putString(key, relayLabels[i].off);
    snprintf(key, sizeof(key), "md_%d", i);
    prefs.putUChar(key, relayLabels[i].mode);
    snprintf(key, sizeof(key), "gr_%d", i);
    prefs.putUChar(key, relayLabels[i].group);
    snprintf(key, sizeof(key), "pt_%d", i);
    prefs.putUChar(key, relayLabels[i].pulseTimeout);
  }
  prefs.end();
  Serial.println("Saved relay labels to NVS");
  return true;
#elif defined(ESP8266)
  if (!saveRelayLabelsToEeprom())
  {
    Serial.println("Failed to save relay labels to EEPROM");
    return false;
  }
  Serial.println("Saved relay labels to EEPROM");
  return true;
#else
  JsonDocument doc;
  JsonArray labels = doc["labels"].template to<JsonArray>();

  for (uint8_t i = 0; i < kMaxRelays; i++)
  {
    JsonObject label = labels.add<JsonObject>();
    label["o"]            = relayLabels[i].on;
    label["f"]            = relayLabels[i].off;
    label["m"]            = modeToStr(relayLabels[i].mode);
    label["g"]        = relayLabels[i].group;
    label["p"] = relayLabels[i].pulseTimeout;
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

bool loadRelayLabels()
{
  for (uint8_t i = 0; i < kMaxRelays; i++)
  {
    relayLabels[i].on           = defaultRelayOnLabel(i + 1);
    relayLabels[i].off          = defaultRelayOffLabel(i + 1);
    relayLabels[i].mode         = RELAY_MODE_ONOFF;
    relayLabels[i].group        = 0;
    relayLabels[i].pulseTimeout = 1;
  }

#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("labels", true))
  {
    Serial.println("NVS labels namespace empty, using defaults");
    return false;
  }
  char key[8];
  bool anyFound = false;
  for (uint8_t i = 0; i < kMaxRelays; i++)
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
    snprintf(key, sizeof(key), "md_%d", i);
    if (prefs.isKey(key)) relayLabels[i].mode = prefs.getUChar(key, RELAY_MODE_ONOFF);
    snprintf(key, sizeof(key), "gr_%d", i);
    if (prefs.isKey(key)) relayLabels[i].group = prefs.getUChar(key, 0);
    snprintf(key, sizeof(key), "pt_%d", i);
    if (prefs.isKey(key)) relayLabels[i].pulseTimeout = prefs.getUChar(key, 1);
  }
  prefs.end();
  if (anyFound)
    Serial.println("Loaded relay labels from NVS");
  else
    Serial.println("NVS labels not found, using defaults");
  return anyFound;
#elif defined(ESP8266)
  bool loadedFromEeprom = loadRelayLabelsFromEeprom();
  if (loadedFromEeprom)
  {
    Serial.println("Loaded relay labels from EEPROM");
    return true;
  }

  if (!LittleFS.exists(kRelayLabelsPath))
  {
    Serial.println("Relay labels not found in EEPROM/file, using defaults");
    return false;
  }

  File file = LittleFS.open(kRelayLabelsPath, "r");
  if (!file)
  {
    Serial.println("Failed to open relay labels file for reading, using defaults");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse relay labels file (%s), using defaults\n", error.c_str());
    return false;
  }

  JsonArray labels = doc["l"].as<JsonArray>();
  if (labels.isNull())
  {
    Serial.println("Relay labels file missing labels array, using defaults");
    return false;
  }

  uint8_t count = labels.size() < relayCount ? labels.size() : relayCount;
  for (uint8_t i = 0; i < count; i++)
  {
    JsonObject label = labels[i];
    if (label.isNull()) continue;

    assignRelayLabels(i + 1, String(label["o"] | ""), String(label["f"] | ""));
    assignRelayMode(i + 1,
                    strToMode(String(label["m"] | "L")),
                    (uint8_t)(label["g"] | (uint8_t)0),
                    (uint8_t)(label["p"] | (uint8_t)1));
  }

  // Migrate legacy LittleFS labels into EEPROM for consistent persistence.
  if (saveRelayLabelsToEeprom())
  {
    Serial.println("Migrated relay labels from LittleFS to EEPROM");
  }

  Serial.println("Loaded relay labels");
  return true;
#else
  if (!LittleFS.exists(kRelayLabelsPath))
  {
    Serial.println("Relay labels file not found, using defaults");
    return false;
  }

  File file = LittleFS.open(kRelayLabelsPath, "r");
  if (!file)
  {
    Serial.println("Failed to open relay labels file for reading, using defaults");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse relay labels file (%s), using defaults\n", error.c_str());
    return false;
  }

  JsonArray labels = doc["l"].as<JsonArray>();
  if (labels.isNull())
  {
    Serial.println("Relay labels file missing labels array, using defaults");
    return false;
  }

  uint8_t count = labels.size() < relayCount ? labels.size() : relayCount;
  for (uint8_t i = 0; i < count; i++)
  {
    JsonObject label = labels[i];
    if (label.isNull()) continue;

    assignRelayLabels(i + 1, String(label["o"] | ""), String(label["f"] | ""));
    assignRelayMode(i + 1,
            strToMode(String(label["m"] | "L")),
                    (uint8_t)(label["g"] | (uint8_t)0),
                    (uint8_t)(label["p"] | (uint8_t)1));
  }

  Serial.println("Loaded relay labels");
  return true;
#endif
}

bool loadLabelsFromTemplate(uint8_t count)
{
  String path = "/templates/template-" + String(count) + "relay.json";
  return loadLabelsFromTemplateFile(path.substring(String("/templates/").length()), count);
}

bool loadLabelsFromTemplateFile(const String &filename, uint8_t count, String *failureReason)
{
  auto fail = [&](const String &reason, const String &path = String(), const DeserializationError *error = nullptr) -> bool
  {
    if (failureReason)
    {
      *failureReason = reason;
    }

    Serial.printf("Template load failed: reason=%s file=%s count=%u heap=%lu",
                  reason.c_str(),
                  path.c_str(),
                  (unsigned)count,
                  (unsigned long)ESP.getFreeHeap());
    if (error)
    {
      Serial.printf(" error=%s", error->c_str());
    }
    Serial.println();
    return false;
  };

  auto applyTemplateLabels = [&](JsonVariantConst root) -> bool
  {
    JsonArrayConst labels = root["l"].as<JsonArrayConst>();
    if (labels.isNull())
    {
      labels = root.as<JsonArrayConst>();
    }
    if (labels.isNull()) return false;

    uint8_t n = (uint8_t)labels.size() < count ? (uint8_t)labels.size() : count;
    for (uint8_t i = 0; i < n; i++)
    {
      JsonObjectConst label = labels[i];
      if (label.isNull()) continue;
      assignRelayLabels(i + 1, String(label["o"] | ""), String(label["f"] | ""));
      assignRelayMode(i + 1,
              strToMode(String(label["m"] | "L")),
                      (uint8_t)(label["g"] | (uint8_t)0),
                      (uint8_t)(label["p"] | (uint8_t)1));
    }

    return true;
  };

  String cleanName = filename;
  cleanName.trim();
  if (cleanName.length() == 0)
  {
    return fail("empty_filename");
  }

  if (cleanName.startsWith("/templates/"))
  {
    cleanName = cleanName.substring(String("/templates/").length());
  }

  if (cleanName.indexOf('/') >= 0 || cleanName.indexOf('\\') >= 0 || cleanName.indexOf("..") >= 0)
  {
    return fail("invalid_filename", cleanName);
  }

  String path = "/templates/" + cleanName;
  if (!LittleFS.exists(path))
  {
    for (uint8_t i = 0; i < count && i < kMaxRelays; i++)
    {
      assignRelayLabels(i + 1, "", "");
      assignRelayMode(i + 1, RELAY_MODE_ONOFF, 0, 1);
    }
    Serial.printf("Template not found for %u relays (%s), generated defaults\n", count, path.c_str());
    if (cleanName == ("template-" + String(count) + "relay.json"))
    {
      return true;
    }
    return fail("template_not_found", path);
  }

  File f = LittleFS.open(path, "r");
  if (!f) return fail("open_failed", path);

  size_t docCapacity = 2560;
#ifndef ESP8266
  size_t fileSize = (size_t)f.size();
  docCapacity = fileSize + 2560;
  if (docCapacity < 2560) docCapacity = 2560;
  if (docCapacity > 32768) docCapacity = 32768;
#endif

#ifdef ESP8266
  // Guard against fragmented heap before committing to a large allocation.
  if (ESP.getMaxFreeBlockSize() < (docCapacity + 2048))
  {
    f.close();
    Serial.printf("[Template] Heap too fragmented: free=%lu maxBlock=%lu need=%u\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxFreeBlockSize(),
                  (unsigned)(docCapacity + 2048));
    return fail("low_heap", path);
  }
#endif

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

#ifndef ESP8266
  // On ESP32 only: retry with a larger allocation if the file was bigger than expected.
  // Not safe on ESP8266 — 32 KB is likely to OOM on a fragmented heap.
  if (err == DeserializationError::NoMemory && docCapacity < 32768)
  {
    f = LittleFS.open(path, "r");
    if (!f) return fail("reopen_failed", path);
    JsonDocument retryDoc;
    err = deserializeJson(retryDoc, f);
    f.close();
    if (err) return fail(err == DeserializationError::NoMemory ? "parse_no_memory" : "parse_failed", path, &err);

    if (!applyTemplateLabels(retryDoc)) return fail("labels_missing", path);

    Serial.printf("Loaded relay labels from template: %s\n", path.c_str());
    if (failureReason)
    {
      *failureReason = "";
    }
    return true;
  }
#endif

  if (err) return fail(err == DeserializationError::NoMemory ? "parse_no_memory" : "parse_failed", path, &err);

  if (!applyTemplateLabels(doc)) return fail("labels_missing", path);

  Serial.printf("Loaded relay labels from template: %s\n", path.c_str());
  if (failureReason)
  {
    *failureReason = "";
  }
  return true;
}
