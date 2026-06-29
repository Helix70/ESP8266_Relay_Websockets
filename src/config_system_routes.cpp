#include <ArduinoJson.h>

#include "app_state.h"
#include "config_routes_internal.h"
#include "config_store.h"
#include "network_manager.h"
#include "relay_runtime.h"
#include "route_data.h"
#include "route_utils.h"
#include "storage_utils.h"
#include "web_runtime.h"

void registerSystemConfigRoutes()
{
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    String name = routeGetBodyParam(request, "name");
    name.trim();
    if (name.length() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"name required\"}");
      return;
    }
    if (name.length() > kMaxBoardNameLength)
    {
      name = name.substring(0, kMaxBoardNameLength);
    }

    bool oldDoDelay = doDelay;
    uint16_t oldStartupDelaySeconds = startupDelaySeconds;
    bool oldConnectStrongestOnStartup = connectStrongestOnStartup;

    boardName = name;
    doDelay = routeParseBool(routeGetBodyParam(request, "doDelay"), doDelay);
    String strongestParam = routeGetBodyParam(request, "connectStrongestOnStartup");
    if (strongestParam.length() == 0)
    {
      strongestParam = routeGetBodyParam(request, "connectStrongest");
    }
    if (strongestParam.length() == 0)
    {
      strongestParam = routeGetBodyParam(request, "strongestSsid");
    }
    connectStrongestOnStartup = routeParseBool(strongestParam, connectStrongestOnStartup);

    String delaySecondsStr = routeGetBodyParam(request, "delaySeconds");
    if (delaySecondsStr.length() > 0)
    {
      int parsedDelaySeconds = delaySecondsStr.toInt();
      if (parsedDelaySeconds < 0) parsedDelaySeconds = 0;
      if (parsedDelaySeconds > kMaxStartupDelaySeconds) parsedDelaySeconds = kMaxStartupDelaySeconds;
      startupDelaySeconds = (uint16_t)parsedDelaySeconds;
    }

    bool restartNeeded = false;
    bool useDhcp = routeParseBool(routeGetBodyParam(request, "useDhcp"), !useStaticIp);

    if (useDhcp)
    {
      if (useStaticIp)
      {
        restartNeeded = true;
      }
      useStaticIp = false;
    }
    else
    {
      String ipStr = routeGetBodyParam(request, "ip");
      String dnsStr = routeGetBodyParam(request, "dns");
      String gatewayStr = routeGetBodyParam(request, "gateway");
      String subnetStr = routeGetBodyParam(request, "subnet");

      IPAddress newIp, newDns, newGateway, newSubnet;
      if (ipStr.length() == 0 || dnsStr.length() == 0 || gatewayStr.length() == 0 || subnetStr.length() == 0 ||
          !newIp.fromString(ipStr) || !newDns.fromString(dnsStr) || !newGateway.fromString(gatewayStr) || !newSubnet.fromString(subnetStr))
      {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid ip config\"}");
        return;
      }

      if (!useStaticIp || boardIp != newIp || boardDns != newDns || boardGateway != newGateway || boardSubnet != newSubnet)
      {
        restartNeeded = true;
      }

      boardIp = newIp;
      boardDns = newDns;
      boardGateway = newGateway;
      boardSubnet = newSubnet;
      useStaticIp = true;
    }

    if (doDelay != oldDoDelay || startupDelaySeconds != oldStartupDelaySeconds ||
        connectStrongestOnStartup != oldConnectStrongestOnStartup)
    {
      restartNeeded = true;
    }

    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    notifyClients();

    JsonDocument response;
    response["ok"] = true;
    response["restart"] = restartNeeded;
    response["useDhcp"] = !useStaticIp;
    response["appliedIp"] = useStaticIp ? boardIp.toString() : WiFi.localIP().toString();
    char payload[128];
    serializeJson(response, payload, sizeof(payload));
    request->send(200, "application/json", payload);

    if (restartNeeded)
    {
      pendingRestart = true;
      pendingRestartAt = millis() + 1200;
    }
  });

  server.on("/api/clearwifi", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    bool confirmed = false;
    if (request->hasParam("confirm", true))
    {
      String value = request->getParam("confirm", true)->value();
      value.toLowerCase();
      value.trim();
      confirmed = (value == "1" || value == "true" || value == "yes" || value == "y");
    }

    if (!confirmed)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"confirmation required\"}");
      return;
    }

    wifiSsid = "";
    wifiPassword = "";

    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    pendingRestart = true;
    pendingRestartAt = millis() + 1200;
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    request->send(200, "application/json", "{\"ok\":true}");
    pendingRestart = true;
    pendingRestartAt = millis() + 1200;
  });

  server.on("/api/wifi/rescan", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (wifiSsid.length() == 0)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid not configured\"}");
      return;
    }

    if (wifiRescanInProgress || wifiRescanRequested)
    {
      request->send(200, "application/json", "{\"ok\":true,\"queued\":false,\"message\":\"rescan already in progress\"}");
      return;
    }

    requestStrongestSsidRescan();
    notifyClients();
    request->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  server.on("/api/theme", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    char buf[100];
    snprintf(buf, sizeof(buf), "{\"h\":\"%s\"}", themeHex);
    request->send(200, "application/json", buf);
  });

  server.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    String h = routeGetBodyParam(request, "h");
    h.trim();

    if (h.length() == 0 || h.length() >= sizeof(themeHex))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid\"}");
      return;
    }

    bool valid = true;
    int commas = 0;
    for (size_t i = 0; i < h.length() && valid; i++)
    {
      char c = h[i];
      if (c == ',') { commas++; continue; }
      if (c == '#') continue;
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) continue;
      valid = false;
    }
    if (!valid || (commas != 6 && commas != 8))
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid hex\"}");
      return;
    }

    strlcpy(themeHex, h.c_str(), sizeof(themeHex));

    if (!saveBoardConfig())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/labels", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    bool changed = false;
    for (uint8_t relayNum = 1; relayNum <= relayCount; relayNum++)
    {
      String prefix = "relay" + String(relayNum);
      String onLabel = routeGetBodyParam(request, (prefix + "_on").c_str());
      String offLabel = routeGetBodyParam(request, (prefix + "_off").c_str());
      assignRelayLabels(relayNum, onLabel, offLabel);

      String modeStr = routeGetBodyParam(request, (prefix + "_mode").c_str());
      uint8_t mode = RELAY_MODE_ONOFF;
      if (modeStr == "I") mode = RELAY_MODE_INTERLOCKED;
      else if (modeStr == "P") mode = RELAY_MODE_PULSED;

      uint8_t group = (uint8_t)routeGetBodyParam(request, (prefix + "_group").c_str()).toInt();
      uint8_t timeout = (uint8_t)routeGetBodyParam(request, (prefix + "_pulse").c_str()).toInt();
      if (mode == RELAY_MODE_PULSED)
      {
        if (timeout == 0 || timeout > kMaxPulseTimeoutSeconds) timeout = kDefaultPulseTimeoutSeconds;
      }
      else
      {
        timeout = 0;
      }
      assignRelayMode(relayNum, mode, group, timeout);
      changed = true;
    }

    if (!changed)
    {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"no labels provided\"}");
      return;
    }

    if (!saveRelayLabels())
    {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    notifyClients();
    request->send(200, "application/json", "{\"ok\":true}");
  });
}
