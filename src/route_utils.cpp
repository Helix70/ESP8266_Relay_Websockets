#include "route_utils.h"

String routeGetBodyParam(AsyncWebServerRequest *request, const char *name)
{
  if (request->hasParam(name, true))
  {
    return request->getParam(name, true)->value();
  }
  return "";
}

bool routeParseBool(String value, bool defaultValue)
{
  value.toLowerCase();
  value.trim();
  if (value == "1" || value == "true" || value == "on" || value == "yes") return true;
  if (value == "0" || value == "false" || value == "off" || value == "no") return false;
  return defaultValue;
}
