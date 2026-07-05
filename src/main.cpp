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

// Injected by scripts/pio_custom_targets.py from `git rev-parse --short HEAD`
// + the commit's checkin date (not the compile date, so rebuilding without
// new commits doesn't produce a misleadingly "newer" version string).
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
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
        recordBootWarning("Selected relay template '" + selectedRelayTemplateFilename +
                           "' is missing or unreadable; using default labels");
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
    // Set before LittleFS.end() so any in-flight request that reaches a
    // route/filter check after this point sees the flag and refuses to
    // touch the filesystem, instead of racing the unmount below.
    otaInProgress = true;
    LittleFS.end();

    // Quiesce the async server before the blocking flash write starves loop().
    // The WebSocket close handshake and disconnect callbacks run in ctx: sys;
    // if they overlap the update they can dereference freed state (Exception 28,
    // null read in ctx: sys). Stop accepting frames, close all clients, then
    // yield so lwIP can flush the teardown before we hand control to the updater.
    ws.enable(false);
    ws.closeAll();

    uint32_t flushDeadline = millis() + 300;
    while (millis() < flushDeadline)
    {
      ws.cleanupClients();
      if (ws.count() == 0)
      {
        break;
      }
      delay(10);
    }
    delay(50); });

  ArduinoOTA.onEnd([]()
                   {
    Serial.println("\nEnd");
    // Device reboots immediately on success anyway, but reset defensively.
    otaInProgress = false; });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { uint32_t progressPercent = (progress / (total / 100));
                          static uint32_t lastProgressPercent = 0;
                          if (progressPercent != lastProgressPercent)
                          {
                            lastProgressPercent = progressPercent;
                            if (progressPercent % 5 == 0) // Print progress every 5%
                            {
                              Serial.printf("Progress: %u%%\n", progressPercent);
                            }
                          }
                        });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");

    // Unlike onEnd (success), the device keeps running after a failed/aborted
    // OTA — re-mount LittleFS and re-enable WS or the device would be stuck
    // "running" with a torn-down filesystem despite otaInProgress clearing.
    initLittleFS();
    ws.enable(true);
    otaInProgress = false; });

  // mDNS is disabled: OTA uploads target a fixed IP (platformio.ini upload_port),
  // never a .local hostname, and the OTA transfer itself runs over its own UDP
  // port independent of mDNS. Leaving mDNS on means parsing every multicast
  // packet from every device on the LAN; on ESP8266 that parser has no
  // allocation ceiling and a single busy packet can exhaust the ~16KB heap
  // margin in one pass (observed OOM crash in MDNSResponder::_readRRAnswer).
  // setHostname() served no purpose beyond mDNS registration (verified in the
  // ArduinoOTA library: _hostname is only used for MDNS.begin() and a debug
  // log gated behind #ifdef OTA_DEBUG, which this project never defines) and
  // has been removed along with mDNS.
#if defined(ESP32)
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.begin();
#else
  ArduinoOTA.begin(false);
#endif
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\nFirmware version: %s\n", FIRMWARE_VERSION);
  printSerialHelp();

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
    Serial.printf("Provisioning\n");
    
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

      // Heap logging is throttled separately from the 2s tick above: only log
      // when free heap moves by a meaningful amount (catches real leaks/drops
      // like the mDNS OOM precursor) or at least once a minute (keeps a
      // baseline in the log even when heap is perfectly stable).
      const uint32_t kHeapLogIntervalMs = 60000;
      const uint32_t kHeapChangeThresholdBytes = 1024;
      static uint32_t lastHeapLogAt = 0;
      static uint32_t lastLoggedFreeHeap = 0;
      static bool heapLogged = false;

      uint32_t freeHeap = ESP.getFreeHeap();
      uint32_t heapDelta = (freeHeap > lastLoggedFreeHeap)
                               ? (freeHeap - lastLoggedFreeHeap)
                               : (lastLoggedFreeHeap - freeHeap);
      bool changedSignificantly = !heapLogged || heapDelta >= kHeapChangeThresholdBytes;
      bool intervalElapsed = (elapsed - lastHeapLogAt) >= kHeapLogIntervalMs;

      if (changedSignificantly || intervalElapsed)
      {
#if defined(ESP8266)
        Serial.printf("Heap free: %u, largest block: %u\n", freeHeap, ESP.getMaxFreeBlockSize());
#else
        Serial.printf("Heap free: %u, largest block: %u\n", freeHeap, ESP.getMaxAllocHeap());
#endif
        lastHeapLogAt = elapsed;
        lastLoggedFreeHeap = freeHeap;
        heapLogged = true;
      }
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

  dispatchPendingNotifications();

  // Rate-limited: cleanupClients() takes the same lock as WS_EVT_DATA dispatch
  // and textAll()/text() broadcasts. Calling it unconditionally every loop()
  // pass (previously throttled only by a delayMicroseconds(100) busy-wait)
  // starved the ESP32 AsyncTCP task of that lock, causing intermittent
  // relay-toggle latency. Stale-client cleanup doesn't need per-tick freshness.
  //
  // maxClients is platform-specific, not a shared blanket value:
  //   ESP8266: 2. Heap-constrained (~80KB total; this session's soak testing
  //   found a comfortable operating floor around 7-8KB free under heavy
  //   combined load), and this UI is only ever used by 1-2 browsers at once
  //   here, so capping tightly means a stuck/zombie connection (e.g. a
  //   backgrounded mobile browser that never sent a clean close) can only
  //   ever hold onto at most 2 client slots' worth of buffers, instead of
  //   the library's own default of 4.
  //   ESP32: 8 (the library's own default, restored rather than reduced).
  //   This session's soak testing measured a ~197KB heap floor on ESP32
  //   under the same heavy combined load where ESP8266 sat at 7-8KB, so the
  //   tight ESP8266-motivated cap was needlessly restrictive here. 8 slots
  //   comfortably covers real concurrent use (1-2 UI browsers) plus the
  //   automated soak test's own connection plus a live monitoring/dashboard
  //   view open at the same time, without being unbounded.
#ifdef ESP32
  static const uint16_t kWsMaxClients = 8;
#else
  static const uint16_t kWsMaxClients = 2;
#endif
  static uint32_t lastWsCleanup = 0;
  if ((elapsed - lastWsCleanup) > 250)
  {
    ws.cleanupClients(kWsMaxClients);
    lastWsCleanup = elapsed;
  }

  processSerialCommands();
  ArduinoOTA.handle();

#ifdef ESP8266
  // ESP32 needed cleanupClients() rate-limited above to stop it starving the
  // AsyncTCP task of _ws_clients_lock; ESP8266 never needed that (WS sends are
  // already deferred out of interrupt context, see dispatchPendingNotifications()).
  // Keep this platform's loop cadence unchanged from before that ESP32 fix,
  // since altering it here is untested and unrelated to the problem it solved.
  delayMicroseconds(100);
#endif
}
