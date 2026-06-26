#pragma once

#include <Arduino.h>

#if defined(ESP8266)
static constexpr const char *BOARD_CPU_TYPE = "ESP8266";
#elif defined(ESP32)
static constexpr const char *BOARD_CPU_TYPE = "ESP32";
#else
static constexpr const char *BOARD_CPU_TYPE = "Unknown";
#endif

constexpr uint8_t BOARD_OUTPUT_GPIO = 0;
constexpr uint8_t BOARD_OUTPUT_SHIFTREGISTER = 1;

struct BoardHardware {
  String name;
  String cpu;
  uint8_t relayCount;
  uint8_t ledPin;
  uint8_t outputType;
  uint8_t relayPins[16];
  uint8_t srLatchPin;
  uint8_t srClockPin;
  uint8_t srDataPin;
  uint8_t srOePin;
  bool loaded;
};

extern BoardHardware activeBoardHardware;

// Returns the LittleFS path for the board hardware config matching the
// current compile-time CPU and the given relay variant ("8relay"/"16relay").
String boardHardwarePath(const String &variant);

// Reads the board hardware config from LittleFS.
// Returns true on success. Falls back to hardcoded defaults on failure.
bool loadBoardHardware(const String &variant);

// Reads board hardware config directly from a LittleFS path such as
// "/boards/custom.json" and falls back to defaults derived from relay count.
bool loadBoardHardwareFromPath(const String &path);
