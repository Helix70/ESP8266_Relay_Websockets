#include "serial_commands.h"

#include "app_state.h"
#include "config_store.h"
#include "serial_provision.h"

static void handleSerialCommand(const String &rawCommand)
{
  String command = rawCommand;
  command.trim();
  command.toLowerCase();

  if (command.length() == 0)
  {
    return;
  }

  if (command == "reset_wifi")
  {
    Serial.println("reset_wifi received.");
    Serial.println("Confirm clear Wi-Fi credentials and start serial Wi-Fi setup? [y/N]");
    String confirmation = readSerialLineBlocking();
    confirmation.trim();
    confirmation.toLowerCase();

    if (!(confirmation == "y" || confirmation == "yes"))
    {
      Serial.println("Cancelled. Wi-Fi credentials were not changed.");
      return;
    }

    Serial.println("Clearing Wi-Fi credentials.");
    wifiSsid = "";
    wifiPassword = "";

    if (saveBoardConfig())
    {
      Serial.println("Wi-Fi credentials cleared.");
      Serial.println("Starting serial Wi-Fi setup...");

      if (runSerialWiFiProvisioningWizard())
      {
        Serial.println("Wi-Fi credentials saved. Restarting...");
        pendingRestart = true;
        pendingRestartAt = millis() + 1000;
      }
      else
      {
        Serial.println("Serial Wi-Fi setup failed or cancelled. Credentials remain blank.");
      }
    }
    else
    {
      Serial.println("Failed to clear Wi-Fi credentials.");
    }
    return;
  }

  if (command == "help")
  {
    Serial.println("Available commands: reset_wifi, help");
    return;
  }

  Serial.printf("Unknown command: %s\n", command.c_str());
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
