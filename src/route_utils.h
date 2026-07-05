#pragma once

#include <ESPAsyncWebServer.h>

#include <Arduino.h>

String routeGetBodyParam(AsyncWebServerRequest *request, const char *name);
bool routeParseBool(String value, bool defaultValue);
