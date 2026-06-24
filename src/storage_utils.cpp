#include "storage_utils.h"

#include "LittleFS.h"
#include <ArduinoJson.h>

#if defined(ESP32)
#include <Preferences.h>
#endif

namespace
{
constexpr uint8_t kMaxRelays = 16;
constexpr size_t kMaxRelayLabelLength = 32;

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

void initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("Cannot mount LittleFS volume...");
    while (1)
    {
      delay(50);
    }
  }
  Serial.println("LittleFS volume mounted...");
}

static const char *modeToStr(uint8_t mode)
{
  if (mode == RELAY_MODE_INTERLOCKED) return "interlocked";
  if (mode == RELAY_MODE_PULSED)      return "pulsed";
  return "onoff";
}

static uint8_t strToMode(const String &s)
{
  if (s == "interlocked") return RELAY_MODE_INTERLOCKED;
  if (s == "pulsed")      return RELAY_MODE_PULSED;
  return RELAY_MODE_ONOFF;
}

void assignRelayMode(uint8_t relayNum, uint8_t mode, uint8_t group, uint8_t pulseTimeout)
{
  if (relayNum < 1 || relayNum > kMaxRelays) return;
  uint8_t idx = relayNum - 1;
  relayLabels[idx].mode         = (mode <= RELAY_MODE_PULSED) ? mode : RELAY_MODE_ONOFF;
  relayLabels[idx].group        = group;
  relayLabels[idx].pulseTimeout = (pulseTimeout >= 1 && pulseTimeout <= 30) ? pulseTimeout : 1;
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
#else
  StaticJsonDocument<4096> doc;
  JsonArray labels = doc.createNestedArray("labels");

  for (uint8_t i = 0; i < kMaxRelays; i++)
  {
    JsonObject label = labels.createNestedObject();
    label["on"]           = relayLabels[i].on;
    label["off"]          = relayLabels[i].off;
    label["mode"]         = modeToStr(relayLabels[i].mode);
    label["group"]        = relayLabels[i].group;
    label["pulseTimeout"] = relayLabels[i].pulseTimeout;
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

  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse relay labels file (%s), using defaults\n", error.c_str());
    return false;
  }

  JsonArray labels = doc["labels"].as<JsonArray>();
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

    assignRelayLabels(i + 1, String(label["on"] | ""), String(label["off"] | ""));
    assignRelayMode(i + 1,
                    strToMode(String(label["mode"] | "onoff")),
                    (uint8_t)(label["group"] | (uint8_t)0),
                    (uint8_t)(label["pulseTimeout"] | (uint8_t)1));
  }

  Serial.println("Loaded relay labels");
  return true;
#endif
}

bool loadLabelsFromTemplate(uint8_t count)
{
  String path = "/templates/template-" + String(count) + "relay.json";
  if (!LittleFS.exists(path))
  {
    Serial.printf("No template found for %u relays (%s)\n", count, path.c_str());
    return false;
  }

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonArray labels = doc["labels"].as<JsonArray>();
  if (labels.isNull()) return false;

  uint8_t n = (uint8_t)labels.size() < count ? (uint8_t)labels.size() : count;
  for (uint8_t i = 0; i < n; i++)
  {
    JsonObject label = labels[i];
    if (label.isNull()) continue;
    assignRelayLabels(i + 1, String(label["on"] | ""), String(label["off"] | ""));
    assignRelayMode(i + 1,
                    strToMode(String(label["mode"] | "onoff")),
                    (uint8_t)(label["group"] | (uint8_t)0),
                    (uint8_t)(label["pulseTimeout"] | (uint8_t)1));
  }

  Serial.printf("Loaded relay labels from template: %s\n", path.c_str());
  return true;
}
