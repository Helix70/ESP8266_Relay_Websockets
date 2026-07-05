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
extern bool useStaticIp;
extern IPAddress boardIp;
extern IPAddress boardDns;
extern IPAddress boardGateway;
extern IPAddress boardSubnet;

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

static bool promptForIpAddress(const char *label, IPAddress &out)
{
  for (int attempt = 0; attempt < 3; attempt++)
  {
    Serial.printf("%s: ", label);
    String input = readSerialLineBlocking();
    input.trim();
    if (input.length() == 0)
    {
      Serial.println("Required. Try again.");
      continue;
    }
    if (out.fromString(input))
    {
      return true;
    }
    Serial.println("Invalid format. Use dotted-decimal (e.g. 192.168.1.1).");
  }
  return false;
}

bool runSerialWiFiProvisioningWizard()
{
  Serial.println("\n=== Wi-Fi Setup ===");
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
    Serial.println("No SSIDs found. You can still enter one manually.");
  }

  Serial.println("Enter SSID number from the list, or type SSID text directly:");
  String ssidInput = readSerialLineBlocking();
  ssidInput.trim();
  if (ssidInput.length() == 0)
  {
    Serial.println("No SSID provided. Aborting.");
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

  // Network configuration
  Serial.println("\n=== Network Setup ===");
  Serial.println("Use DHCP? [Y/n]:");
  String dhcpInput = readSerialLineBlocking();
  dhcpInput.trim();
  dhcpInput.toLowerCase();

  bool newUseDhcp = !(dhcpInput == "n" || dhcpInput == "no");

  IPAddress newIp, newDns, newGateway, newSubnet;
  if (!newUseDhcp)
  {
    bool ipOk = promptForIpAddress("IP Address", newIp) &&
                promptForIpAddress("DNS", newDns) &&
                promptForIpAddress("Gateway", newGateway) &&
                promptForIpAddress("Subnet Mask", newSubnet);
    if (!ipOk)
    {
      Serial.println("Invalid IP configuration. Defaulting to DHCP.");
      newUseDhcp = true;
    }
  }

  wifiSsid = selectedSsid;
  wifiPassword = enteredPassword;
  useStaticIp = !newUseDhcp;
  if (!newUseDhcp)
  {
    boardIp = newIp;
    boardDns = newDns;
    boardGateway = newGateway;
    boardSubnet = newSubnet;
  }

  if (!saveBoardConfig())
  {
    Serial.println("Failed to save configuration.");
    return false;
  }

  if (newUseDhcp)
  {
    Serial.printf("Saved: SSID=%s, DHCP\n", selectedSsid.c_str());
  }
  else
  {
    Serial.printf("Saved: SSID=%s, IP=%s\n", selectedSsid.c_str(), boardIp.toString().c_str());
  }
  return true;
}
