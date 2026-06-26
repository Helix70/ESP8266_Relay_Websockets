#pragma once

#include <Arduino.h>

constexpr uint8_t RELAY_MODE_ONOFF = 0;
constexpr uint8_t RELAY_MODE_INTERLOCKED = 1;
constexpr uint8_t RELAY_MODE_PULSED = 2;

struct RelayLabel {
  String on;
  String off;
  uint8_t mode = RELAY_MODE_ONOFF;
  uint8_t group = 0;
  uint8_t pulseTimeout = 1;
};

void initLittleFS();

void assignRelayLabels(uint8_t relayNum, const String &requestedOnLabel,
                       const String &requestedOffLabel);
void assignRelayMode(uint8_t relayNum, uint8_t mode, uint8_t group,
                     uint8_t pulseTimeout);
bool saveRelayLabels();
// Returns true if labels were found in storage; false means defaults are
// active.
bool loadRelayLabels();
// Loads labels from the generic template for the given relay count (8 or 16).
bool loadLabelsFromTemplate(uint8_t count);
// Loads labels from a template filename under /templates/ for the given relay
// count.
bool loadLabelsFromTemplateFile(const String &filename, uint8_t count);
