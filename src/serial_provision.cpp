#include "serial_provision.h"
#include "config_store.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

namespace
{
constexpr size_t kMaxSsidLength = 32;
constexpr size_t kMaxPasswordLength = 64;
}

extern bool reportSignalStrength;
extern String wifiSsid;
extern String wifiPassword;

String readSerialLineBlocking()
{
  String line;
  while (true)
  {
    while (Serial.available() > 0)
    {
      char c = (char)Serial.read();
      if (c == '\r')
      {
        continue;
      }
      if (c == '\n')
      {
        return line;
      }
      line += c;
    }
    delay(10);
    yield();
  }
}

bool runSerialWiFiProvisioningWizard()
{
  Serial.println("\n=== Wi-Fi provisioning ===");
  Serial.println("Scanning for SSIDs...");
  reportSignalStrength = false;

  WiFi.mode(WIFI_STA);
  int scanCount = WiFi.scanNetworks();

  if (scanCount > 0)
  {
    for (int i = 0; i < scanCount; i++)
    {
      Serial.printf("%d) %s (RSSI %d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }
  else
  {
    Serial.println("No SSIDs found by scan. You can still enter one manually.");
  }

  Serial.println("Enter SSID number from the list, or type SSID text directly:");
  String ssidInput = readSerialLineBlocking();
  ssidInput.trim();
  if (ssidInput.length() == 0)
  {
    Serial.println("No SSID provided. Aborting Wi-Fi provisioning.");
    WiFi.scanDelete();
    return false;
  }

  String selectedSsid;
  bool numericChoice = true;
  for (size_t i = 0; i < ssidInput.length(); i++)
  {
    if (!isDigit((unsigned char)ssidInput[i]))
    {
      numericChoice = false;
      break;
    }
  }

  if (numericChoice && scanCount > 0)
  {
    int choice = ssidInput.toInt();
    if (choice >= 1 && choice <= scanCount)
    {
      selectedSsid = WiFi.SSID(choice - 1);
    }
  }

  if (selectedSsid.length() == 0)
  {
    selectedSsid = ssidInput;
  }

  if (selectedSsid.length() > kMaxSsidLength)
  {
    selectedSsid = selectedSsid.substring(0, kMaxSsidLength);
  }

  WiFi.scanDelete();

  Serial.printf("Selected SSID: %s\n", selectedSsid.c_str());
  Serial.println("Enter Wi-Fi password (empty for open network):");
  String enteredPassword = readSerialLineBlocking();

  if (enteredPassword.length() > kMaxPasswordLength)
  {
    enteredPassword = enteredPassword.substring(0, kMaxPasswordLength);
  }

  wifiSsid = selectedSsid;
  wifiPassword = enteredPassword;

  if (!saveBoardConfig())
  {
    Serial.println("Failed to save Wi-Fi credentials to configuration");
    return false;
  }

  Serial.println("Wi-Fi credentials saved to configuration.");
  return true;
}
