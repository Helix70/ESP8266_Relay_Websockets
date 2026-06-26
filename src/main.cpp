#include <Arduino.h>
#include <ArduinoOTA.h>
#include "LittleFS.h"

#include "app_state.h"
#include "config_routes.h"
#include "config_store.h"
#include "network_manager.h"
#include "provisioning_portal.h"
#include "relay_runtime.h"
#include "serial_commands.h"
#include "storage_utils.h"
#include "web_runtime.h"

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "relay-board"
#endif

static bool initializeRuntime()
{
  bool littleFsReady = initLittleFS();

  bool labelsFound = loadRelayLabels();
  loadBoardConfig();
  applyHardwareVariantPinsAndModes();
  if (littleFsReady)
  {
    bool templateReconciled = reconcileSelectedTemplateForActiveHardware(false);

    if (relayCount > 0 && selectedRelayTemplateFilename.length() > 0)
    {
      if (templateReconciled)
      {
        saveRelayLabels();
        labelsFound = true;
        Serial.printf("Applied selected relay template on boot: %s\n", selectedRelayTemplateFilename.c_str());
      }
      else if (loadLabelsFromTemplateFile(selectedRelayTemplateFilename, relayCount))
      {
        saveRelayLabels();
        labelsFound = true;
        Serial.printf("Applied selected relay template on boot: %s\n", selectedRelayTemplateFilename.c_str());
      }
      else
      {
        Serial.printf("Selected relay template unavailable: %s\n", selectedRelayTemplateFilename.c_str());
      }
    }

    if (!labelsFound && relayCount > 0)
    {
      loadLabelsFromTemplate(relayCount);
      saveRelayLabels();
    }
  }
  else
  {
    Serial.println("Skipping filesystem-backed template hydration because LittleFS is unavailable");
  }

  if (onboard_led.pin != 255)
  {
    pinMode(onboard_led.pin, OUTPUT);
  }

  onboard_led.on = !onboard_led.on_state;
  onboard_led.update();

  initRelayOutputs();
  return littleFsReady;
}

static void setupOta()
{
  ArduinoOTA.onStart([]()
                     {
    LittleFS.end();

    ws.enable(false);
    ws.closeAll(); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();
}

void setup()
{
  Serial.begin(115200);

  bool littleFsReady = initializeRuntime();

  if (!littleFsReady)
  {
    Serial.println("LittleFS unavailable; starting provisioning portal for recovery.");
    startProvisioningPortal();
    return;
  }

  if (wifiSsid.length() == 0)
  {
    startProvisioningPortal();
    Serial.println("You can also provision via serial command: reset_wifi");
    return;
  }

  if (!initWiFi())
  {
    Serial.println("Wi-Fi initialization failed; starting provisioning portal to avoid reboot loop.");
    startProvisioningPortal();
    return;
  }
  initWebSocket();
  registerRuntimeHttpRoutes();
  registerConfigRoutes();
  startRuntimeServer();
  setupOta();
}

void loop()
{
  if (pendingRestart)
  {
    uint32_t now = millis();
    if ((int32_t)(now - pendingRestartAt) >= 0)
    {
      pendingRestart = false;
      Serial.println("Restarting to apply network configuration");
      ESP.restart();
    }
  }

  if (provisioningMode)
  {
    pollProvisioningScan();
    processSerialCommands();
    delay(2);
    return;
  }

  elapsed = millis();
  if ((elapsed - timer) > 2000)
  {
    onboard_led.on = !onboard_led.on;
    onboard_led.update();
    timer = elapsed;
    if (reportSignalStrength)
    {
      Serial.printf("WiFi Signal Strength: %d\n", WiFi.RSSI());
    }
  }

  if (processRelayTimers(elapsed))
  {
    if (useShiftRegister)
    {
      writeRelaysToShiftRegister();
    }
    notifyRelayStates();
  }

  processStrongestSsidRescan();

  ws.cleanupClients();
  processSerialCommands();
  ArduinoOTA.handle();
}
