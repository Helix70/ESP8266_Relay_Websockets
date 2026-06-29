#pragma once

#include <Arduino.h>

String readSerialLineBlocking();
bool runSerialHardwareSetupWizard();
bool runSerialWiFiProvisioningWizard();
