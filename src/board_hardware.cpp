#include "board_hardware.h"

#include "LittleFS.h"
#include <ArduinoJson.h>

BoardHardware activeBoardHardware;

// ─── helpers ─────────────────────────────────────────────────────────────────

static void setDefaults(const String &variant)
{
  activeBoardHardware = BoardHardware{};
  activeBoardHardware.loaded     = false;
  activeBoardHardware.cpu        = BOARD_CPU_TYPE;
  activeBoardHardware.ledPin     = 2;

  bool is16 = (variant == "16relay");
  activeBoardHardware.relayCount = is16 ? 16 : 8;
  activeBoardHardware.outputType = is16 ? BOARD_OUTPUT_SHIFTREGISTER : BOARD_OUTPUT_GPIO;

  for (uint8_t i = 0; i < 16; i++) activeBoardHardware.relayPins[i] = 255;

#if defined(ESP8266)
  activeBoardHardware.ledPin = 2;
  if (!is16) {
    uint8_t pins[8] = {16, 14, 12, 13, 15, 0, 4, 5};
    for (uint8_t i = 0; i < 8; i++) activeBoardHardware.relayPins[i] = pins[i];
  }
#elif defined(ESP32)
  activeBoardHardware.ledPin = 23;
  if (!is16) {
    uint8_t pins[8] = {32, 33, 25, 26, 27, 14, 12, 13};
    for (uint8_t i = 0; i < 8; i++) activeBoardHardware.relayPins[i] = pins[i];
  }
#endif

  activeBoardHardware.srLatchPin = 12;
  activeBoardHardware.srClockPin = 13;
  activeBoardHardware.srDataPin  = 14;
  activeBoardHardware.srOePin    = 5;
}

// ─── public ──────────────────────────────────────────────────────────────────

String boardHardwarePath(const String &variant)
{
  String cpu = String(BOARD_CPU_TYPE);
  cpu.toLowerCase();
  return "/boards/" + cpu + "-" + variant + ".json";
}

bool loadBoardHardware(const String &variant)
{
  String path = boardHardwarePath(variant);
  return loadBoardHardwareFromPath(path);
}

bool loadBoardHardwareFromPath(const String &path)
{
  String variant = (path.indexOf("16relay") >= 0) ? "16relay" : "8relay";
  setDefaults(variant);

  if (!LittleFS.exists(path))
  {
    Serial.printf("Board hardware config not found (%s), using defaults\n", path.c_str());
    return false;
  }

  File f = LittleFS.open(path, "r");
  if (!f)
  {
    Serial.printf("Failed to open board hardware config: %s\n", path.c_str());
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err)
  {
    Serial.printf("Board hardware config parse error (%s): %s\n", path.c_str(), err.c_str());
    return false;
  }

  activeBoardHardware.name       = doc["name"]       | activeBoardHardware.name;
  activeBoardHardware.cpu        = doc["cpu"]        | activeBoardHardware.cpu;
  activeBoardHardware.relayCount = doc["relayCount"] | activeBoardHardware.relayCount;
  activeBoardHardware.ledPin     = doc["ledPin"]     | activeBoardHardware.ledPin;

  String outputType = String(doc["outputType"] | "gpio");
  activeBoardHardware.outputType = (outputType == "shiftregister")
                                   ? BOARD_OUTPUT_SHIFTREGISTER
                                   : BOARD_OUTPUT_GPIO;

  if (activeBoardHardware.outputType == BOARD_OUTPUT_GPIO)
  {
    JsonArray relays = doc["relays"].as<JsonArray>();
    if (!relays.isNull())
    {
      for (JsonObject r : relays)
      {
        uint8_t relayNum = r["relay"] | 0;
        if (relayNum >= 1 && relayNum <= 16)
          activeBoardHardware.relayPins[relayNum - 1] = r["pin"] | (uint8_t)255;
      }
    }
  }
  else
  {
    JsonObject sr = doc["shiftRegister"];
    if (!sr.isNull())
    {
      activeBoardHardware.srLatchPin = sr["latchPin"] | activeBoardHardware.srLatchPin;
      activeBoardHardware.srClockPin = sr["clockPin"] | activeBoardHardware.srClockPin;
      activeBoardHardware.srDataPin  = sr["dataPin"]  | activeBoardHardware.srDataPin;
      activeBoardHardware.srOePin    = sr["oePin"]    | activeBoardHardware.srOePin;
    }
  }

  activeBoardHardware.loaded = true;
  Serial.printf("Loaded board hardware: %s\n", activeBoardHardware.name.c_str());
  return true;
}
