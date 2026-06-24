#pragma once

#include <Arduino.h>

void initLittleFS();

void assignRelayLabels(uint8_t relayNum, const String &requestedOnLabel,
                       const String &requestedOffLabel);
bool saveRelayLabels();
void loadRelayLabels();
