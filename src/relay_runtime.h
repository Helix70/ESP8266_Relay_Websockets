#pragma once

#include <Arduino.h>

void applyHardwareVariantPinsAndModes();
void initRelayOutputs();
void writeRelaysToShiftRegister();

void applyLegacyRelayModes(uint8_t relayNum);
bool handlePerRelayModeToggle(uint8_t relayNum);

bool processRelayTimers(uint32_t now);
