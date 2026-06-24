#pragma once

#include <Arduino.h>

String encryptConfigSecret(const String &plain);
String decryptConfigSecret(const String &encoded);

bool saveBoardConfig();
void loadBoardConfig();
