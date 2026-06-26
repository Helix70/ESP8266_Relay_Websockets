#pragma once

#include <Arduino.h>

void applyHardwareVariantPinsAndModes();
void initRelayOutputs();
void writeRelaysToShiftRegister();

bool handlePerRelayModeToggle(uint8_t relayNum);

bool processRelayTimers(uint32_t now);
