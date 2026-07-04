#pragma once

#include <Arduino.h>

// Persisted as a per-relay byte (ESP32 NVS) and a letter code in templates.
// Values 0/1/2 are unchanged from earlier firmware so existing configs and
// templates keep working. RELAY_MODE_MOMENTARY (3) is reserved for a future
// "on while held" mode that is not implemented yet.
constexpr uint8_t RELAY_MODE_ONOFF = 0;        // "Latched": manual on/off
constexpr uint8_t RELAY_MODE_INTERLOCKED = 1;  // manual on/off, group required
constexpr uint8_t RELAY_MODE_PULSED = 2;       // on, auto-off after timeout
constexpr uint8_t RELAY_MODE_MOMENTARY = 3;    // reserved, not implemented
constexpr uint8_t RELAY_MODE_INTERLOCKED_PULSED = 4; // interlocked + pulsed
constexpr uint8_t RELAY_MODE_MAX = RELAY_MODE_INTERLOCKED_PULSED;

struct RelayLabel {
  String on;
  String off;
  uint8_t mode = RELAY_MODE_ONOFF;
  uint8_t group = 0;
  uint8_t pulseTimeout = 1;
};

bool initLittleFS();

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
bool loadLabelsFromTemplateFile(const String &filename, uint8_t count,
                                String *failureReason = nullptr);
