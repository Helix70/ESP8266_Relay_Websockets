#include "network_manager.h"

#include <Arduino.h>

#include "app_state.h"

#if defined(ESP32)
#include <esp_wifi.h>
#endif

static const bool LOCK_TO_STRONGEST_BSSID = false;

#if defined(ESP32) || defined(ESP8266)
static bool beginWiFiOnStrongestBssid(const char *targetSsid, const char *targetPassword)
{
  int scanCount = WiFi.scanNetworks(false, true);
  if (scanCount <= 0)
  {
    Serial.println("WiFi scan found no APs, falling back to default connect");
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
    return false;
  }

  uint8_t bestBssid[6];
  memcpy(bestBssid, WiFi.BSSID(bestIndex), sizeof(bestBssid));
  int bestChannel = WiFi.channel(bestIndex);
  String bestBssidStr = WiFi.BSSIDstr(bestIndex);

  Serial.printf("Connecting to strongest AP for '%s': BSSID=%s ch=%d RSSI=%d dBm\n",
                targetSsid, bestBssidStr.c_str(), bestChannel, bestRssi);

  WiFi.scanDelete();
  WiFi.begin(targetSsid, targetPassword, bestChannel, bestBssid, true);
  return true;
}
#endif

void initWiFi()
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
    if (LOCK_TO_STRONGEST_BSSID && !beginWiFiOnStrongestBssid(wifiSsid.c_str(), wifiPassword.c_str()))
    {
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
    else if (!LOCK_TO_STRONGEST_BSSID)
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
  }
  else
  {
    WiFi.disconnect();
    Serial.println("Status= NOT WL_CONNECTED");
    Serial.println("Rebooting....");
    delay(500);
    ESP.restart();
  }
}
