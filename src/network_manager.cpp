#include "network_manager.h"

#include <Arduino.h>

#include "app_state.h"
#include "web_runtime.h"

#if defined(ESP32)
#include <esp_wifi.h>
#endif

#if defined(ESP32) || defined(ESP8266)
static bool performStrongestBssidConnect(const char *targetSsid, const char *targetPassword, bool onlyIfDifferent)
{
  int scanCount = WiFi.scanNetworks(false, true);
  if (scanCount <= 0)
  {
    Serial.println("WiFi scan found no APs, falling back to default connect");
    wifiRescanStatus = "Scan complete: no APs found";
    return false;
  }

  int bestIndex = -1;
  int bestRssi = -1000;
  String desiredSsid = String(targetSsid);

  for (int i = 0; i < scanCount; i++)
  {
    if (WiFi.SSID(i) == desiredSsid)
    {
      int rssi = WiFi.RSSI(i);
      if (bestIndex < 0 || rssi > bestRssi)
      {
        bestIndex = i;
        bestRssi = rssi;
      }
    }
  }

  if (bestIndex < 0)
  {
    Serial.printf("SSID '%s' not found in scan, falling back to default connect\n", targetSsid);
    WiFi.scanDelete();
    wifiRescanStatus = "Scan complete: configured SSID not found";
    return false;
  }

  uint8_t bestBssid[6];
  memcpy(bestBssid, WiFi.BSSID(bestIndex), sizeof(bestBssid));
  int bestChannel = WiFi.channel(bestIndex);
  String bestBssidStr = WiFi.BSSIDstr(bestIndex);

  bool connectedNow = (WiFi.status() == WL_CONNECTED);
  String currentSsid = connectedNow ? WiFi.SSID() : String();
  String currentBssid = connectedNow ? WiFi.BSSIDstr() : String();

  if (onlyIfDifferent && connectedNow && currentSsid == desiredSsid && currentBssid == bestBssidStr)
  {
    WiFi.scanDelete();
    wifiRescanStatus = "Already on strongest matching AP";
    return false;
  }

  Serial.printf("Connecting to strongest AP for '%s': BSSID=%s ch=%d RSSI=%d dBm\n",
                targetSsid, bestBssidStr.c_str(), bestChannel, bestRssi);

  WiFi.scanDelete();
  WiFi.begin(targetSsid, targetPassword, bestChannel, bestBssid, true);
  wifiRescanStatus = "Rescan complete, refreshing Wi-Fi link...";
  return true;
}

static bool beginWiFiOnStrongestBssid(const char *targetSsid, const char *targetPassword)
{
  return performStrongestBssidConnect(targetSsid, targetPassword, false);
}
#endif

void requestStrongestSsidRescan()
{
  if (wifiSsid.length() == 0)
  {
    wifiRescanStatus = "Cannot rescan: no configured SSID";
    return;
  }

  if (wifiRescanInProgress)
  {
    return;
  }

  wifiRescanRequested = true;
  wifiRescanStatus = "Rescan requested";
}

void processStrongestSsidRescan()
{
  static bool awaitingReconnect = false;
  static uint32_t reconnectDeadline = 0;

  if (!wifiRescanInProgress && wifiRescanRequested)
  {
    wifiRescanRequested = false;
    wifiRescanInProgress = true;
    wifiRescanStatus = "Scanning for strongest matching AP...";
    notifyClients();

#if defined(ESP32) || defined(ESP8266)
    bool changedAp = performStrongestBssidConnect(wifiSsid.c_str(), wifiPassword.c_str(), true);
    if (changedAp)
    {
      awaitingReconnect = true;
      reconnectDeadline = millis() + 12000;
      notifyClients();
      return;
    }
#else
    wifiRescanStatus = "Rescan not supported on this target";
#endif

    wifiRescanInProgress = false;
    notifyClients();
    return;
  }

  if (!wifiRescanInProgress || !awaitingReconnect)
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiRescanInProgress = false;
    awaitingReconnect = false;
    wifiRescanStatus = "Connected: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)";
    notifyClients();
    return;
  }

  if ((int32_t)(millis() - reconnectDeadline) >= 0)
  {
    wifiRescanInProgress = false;
    awaitingReconnect = false;
    wifiRescanStatus = "Reconnect timed out after rescan";
    notifyClients();
  }
}

bool initWiFi()
{
  int blinkCounter = 0;
  int attempts = 1;
  Serial.printf("\n\n");

  if (doDelay)
  {
    uint32_t delayIterations = (uint32_t)startupDelaySeconds * 2;
    Serial.printf("Delaying start %u seconds ", startupDelaySeconds);
    while (blinkCounter < (int)delayIterations)
    {
      onboard_led.on = !onboard_led.on;
      onboard_led.update();
      if ((blinkCounter % 2) == 0)
      {
        Serial.print(".");
      }
      delay(500);
      blinkCounter++;
    }
    onboard_led.on = !onboard_led.on_state;
    onboard_led.update();
  }
  Serial.println("*");

  if (useStaticIp)
  {
    Serial.printf("Configuring static IP: %s\n", boardIp.toString().c_str());
    WiFi.config(boardIp, boardDns, boardGateway, boardSubnet);
  }
  else
  {
    Serial.println("Using DHCP");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

#if defined(ESP32)
  esp_wifi_set_ps(WIFI_PS_NONE);
#elif defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

  while ((WiFi.status() != WL_CONNECTED) && (attempts <= 5))
  {
    WiFi.disconnect();
#if defined(ESP32) || defined(ESP8266)
    if (connectStrongestOnStartup && !beginWiFiOnStrongestBssid(wifiSsid.c_str(), wifiPassword.c_str()))
    {
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
    else if (!connectStrongestOnStartup)
    {
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
#else
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
#endif

    Serial.printf("Trying to connect [%s], attempt %d.", WiFi.macAddress().c_str(), attempts);
    attempts++;

    blinkCounter = 0;
    while ((WiFi.status() != WL_CONNECTED) && (blinkCounter < 60))
    {
      Serial.print(".");
      delay(500);
      blinkCounter++;
    }
    Serial.println("*");
  }

  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Status=WL_CONNECTED");
    Serial.print("CONNECTED To: ");
    Serial.println(wifiSsid);
    Serial.print("IP Address: http://");
    Serial.println(WiFi.localIP().toString().c_str());
    WiFi.softAPdisconnect(true);
#if defined(ESP32)
    // IDF 5.x resets power-save to WIFI_PS_MIN_MODEM when the STA associates.
    // Re-apply WIFI_PS_NONE after the connection is fully established to ensure
    // the DTIM beacon wakeup latency (100-300ms) is eliminated.
    esp_wifi_set_ps(WIFI_PS_NONE);
#endif
    return true;
  }

  WiFi.disconnect();
  Serial.println("Status= NOT WL_CONNECTED");
  Serial.println("Wi-Fi connect failed; entering provisioning mode instead of reboot loop.");
  return false;
}
