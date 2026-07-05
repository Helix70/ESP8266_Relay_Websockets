#include "serial_commands.h"

#include "app_state.h"
#include "config_store.h"
#include "serial_provision.h"

void printSerialHelp()
{
  Serial.println("Available commands:");
  Serial.println("  help   - Show this help");
  Serial.println("  reboot - Reboot the device");
  Serial.println("  reset  - Erase all stored settings and reboot");
  Serial.println("  wifi   - Clear Wi-Fi credentials, run Wi-Fi setup, then reboot");
}

static void handleSerialCommand(const String &rawCommand)
{
  String command = rawCommand;
  command.trim();
  command.toLowerCase();

  if (command.length() == 0)
  {
    return;
  }

  if (command == "help")
  {
    printSerialHelp();
    return;
  }

  if (command == "reboot")
  {
    Serial.println("Rebooting...");
    pendingRestart = true;
    pendingRestartAt = millis() + 1000;
    return;
  }

  if (command == "reset")
  {
    Serial.println("This will erase all stored settings. The filesystem is not affected.");
    Serial.println("Confirm? [y/N]");
    String confirmation = readSerialLineBlocking();
    confirmation.trim();
    confirmation.toLowerCase();

    if (!(confirmation == "y" || confirmation == "yes"))
    {
      Serial.println("Cancelled. Settings were not changed.");
      return;
    }

    if (clearBoardConfig())
    {
      Serial.println("Settings erased. Rebooting...");
    }
    else
    {
      Serial.println("Failed to erase settings. Rebooting anyway...");
    }
    pendingRestart = true;
    pendingRestartAt = millis() + 1000;
    return;
  }

  if (command == "wifi")
  {
    Serial.println("This will clear Wi-Fi credentials and run the Wi-Fi setup wizard.");
    Serial.println("Confirm? [y/N]");
    String confirmation = readSerialLineBlocking();
    confirmation.trim();
    confirmation.toLowerCase();

    if (!(confirmation == "y" || confirmation == "yes"))
    {
      Serial.println("Cancelled. Wi-Fi credentials were not changed.");
      return;
    }

    wifiSsid = "";
    wifiPassword = "";
    saveBoardConfig();
    Serial.println("Wi-Fi credentials cleared. Starting setup...");

    runSerialWiFiProvisioningWizard();

    Serial.println("Rebooting...");
    pendingRestart = true;
    pendingRestartAt = millis() + 1000;
    return;
  }

  Serial.printf("Unknown command: %s\n", command.c_str());
  Serial.println("Type 'help' for available commands.");
}

void processSerialCommands()
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
      handleSerialCommand(serialCommandBuffer);
      serialCommandBuffer = "";
      continue;
    }

    if (serialCommandBuffer.length() < 127)
    {
      serialCommandBuffer += c;
    }
  }
}
