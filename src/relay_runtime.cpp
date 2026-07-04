#include "relay_runtime.h"

#include "app_state.h"
#include "board_hardware.h"

void applyHardwareVariantPinsAndModes()
{
  if (hardwareVariant.length() == 0)
  {
    relayCount = 0;
    Serial.println("Hardware variant not configured - relay outputs disabled");
    return;
  }

  if (activeBoardHardwareFilename.length() == 0)
  {
    activeBoardHardwareFilename = boardHardwarePath(hardwareVariant);
  }

  if (!loadBoardHardwareFromPath(activeBoardHardwareFilename))
  {
    String fallbackPath = boardHardwarePath(hardwareVariant);
    activeBoardHardwareFilename = fallbackPath;
    loadBoardHardwareFromPath(activeBoardHardwareFilename);
  }

  relayCount       = activeBoardHardware.relayCount;
  useShiftRegister = (activeBoardHardware.outputType == BOARD_OUTPUT_SHIFTREGISTER);
  onboard_led.pin  = activeBoardHardware.ledPin;

  for (uint8_t i = 0; i < MAX_RELAYS; i++)
  {
    relays[i].pin = 255;
  }

  if (!useShiftRegister)
  {
    for (uint8_t i = 0; i < relayCount && i < MAX_RELAYS; i++)
    {
      relays[i].pin = activeBoardHardware.relayPins[i];
    }
  }

  hardwareVariant = (relayCount == 16) ? String(kVariant16Relay) : String(kVariant8Relay);

  for (uint8_t i = 0; i < MAX_RELAYS; i++)
  {
    latched_relays[i].relay_num = i + 1;
    latched_relays[i].latched_num = 0;
    latched_relays[i].timeout = 0;
    latched_relays[i].counter = 0;
    pulsed_relays[i].relay_num = i + 1;
    pulsed_relays[i].timeout = 0;
    pulsed_relays[i].counter = 0;
  }
}

void initRelayOutputs()
{
  for (uint8_t i = 0; i < MAX_RELAYS; i++)
  {
    relays[i].low();
    relays[i].disabled = 0;
    relays[i].last = 0;
    if (!useShiftRegister && i < relayCount && relays[i].pin != 255)
    {
      pinMode(relays[i].pin, OUTPUT);
    }
    relays[i].update();
  }

  if (useShiftRegister)
  {
    digitalWrite(activeBoardHardware.srOePin, HIGH);
    pinMode(activeBoardHardware.srOePin, OUTPUT);

    digitalWrite(activeBoardHardware.srLatchPin, HIGH);
    pinMode(activeBoardHardware.srLatchPin, OUTPUT);

    digitalWrite(activeBoardHardware.srClockPin, LOW);
    pinMode(activeBoardHardware.srClockPin, OUTPUT);

    digitalWrite(activeBoardHardware.srDataPin, LOW);
    pinMode(activeBoardHardware.srDataPin, OUTPUT);

    writeRelaysToShiftRegister();

    digitalWrite(activeBoardHardware.srOePin, LOW);
  }
}

void writeRelaysToShiftRegister()
{
  for (int i = 0; i < relayCount; i++)
  {
    int regNum = i / 8;
    int bitNum = i % 8;
    if (relays[i].state())
    {
      bitSet(outputData[regNum], bitNum);
    }
    else
    {
      bitClear(outputData[regNum], bitNum);
    }
  }

  digitalWrite(activeBoardHardware.srLatchPin, LOW);
  for (int i = numRegisters; i > 0; i--)
  {
    shiftOut(activeBoardHardware.srDataPin, activeBoardHardware.srClockPin, MSBFIRST, outputData[i - 1]);
  }
  digitalWrite(activeBoardHardware.srDataPin, LOW);
  digitalWrite(activeBoardHardware.srClockPin, LOW);
  digitalWrite(activeBoardHardware.srLatchPin, HIGH);
}

bool handlePerRelayModeToggle(uint8_t relayNum)
{
  if (!(relayNum > 0 && relayNum <= relayCount))
  {
    return false;
  }

  uint8_t idx = relayNum - 1;
  uint8_t mode = relayLabels[idx].mode;
  uint8_t group = relayLabels[idx].group;

  // A relay disabled by a group peer (or by its own running pulse) ignores
  // presses. The client also disables the button, so this is a safety net.
  if (relays[idx].disabled)
  {
    return true;
  }

  if (mode == RELAY_MODE_INTERLOCKED)
  {
    bool turningOn = !relays[idx].on;
    relays[idx].toggle();
    relays[idx].update();
    // Switch off (but do not disable) the other interlocked relays in the group.
    if (turningOn && group > 0)
    {
      for (uint8_t j = 0; j < relayCount; j++)
      {
        if (j != idx && relayLabels[j].mode == RELAY_MODE_INTERLOCKED && relayLabels[j].group == group)
        {
          relays[j].low();
          relays[j].update();
        }
      }
    }
    return true;
  }

  if (mode == RELAY_MODE_PULSED)
  {
    uint8_t timeout = relayLabels[idx].pulseTimeout;
    if (timeout == 0 || timeout > 30)
    {
      timeout = 1;
    }
    relays[idx].high();
    relays[idx].update();
    relays[idx].disabled = 1;
    pulsed_relays[idx].counter = (uint32_t)timeout * DELAY_COUNTER;
    // Disable the other pulsed relays in the group until this pulse expires.
    if (group > 0)
    {
      for (uint8_t j = 0; j < relayCount; j++)
      {
        if (j != idx && relayLabels[j].mode == RELAY_MODE_PULSED && relayLabels[j].group == group)
        {
          relays[j].disabled = 1;
        }
      }
    }
    return true;
  }

  if (mode == RELAY_MODE_INTERLOCKED_PULSED)
  {
    uint8_t timeout = relayLabels[idx].pulseTimeout;
    if (timeout == 0 || timeout > 30)
    {
      timeout = 1;
    }
    // Switch off and disable the other interlocked-and-pulsed relays in the
    // group; they are re-enabled when this pulse expires.
    if (group > 0)
    {
      for (uint8_t j = 0; j < relayCount; j++)
      {
        if (j != idx && relayLabels[j].mode == RELAY_MODE_INTERLOCKED_PULSED && relayLabels[j].group == group)
        {
          relays[j].low();
          relays[j].update();
          relays[j].disabled = 1;
          pulsed_relays[j].counter = 0;
        }
      }
    }
    relays[idx].high();
    relays[idx].update();
    relays[idx].disabled = 1;
    pulsed_relays[idx].counter = (uint32_t)timeout * DELAY_COUNTER;
    return true;
  }

  // RELAY_MODE_ONOFF (Latched), and any fallback mode.
  bool turningOn = !relays[idx].on;
  relays[idx].toggle();
  relays[idx].update();
  // Optional group: while this latched relay is on, the other latched relays in
  // the group are switched off and disabled; switching it off re-enables them.
  if (group > 0)
  {
    for (uint8_t j = 0; j < relayCount; j++)
    {
      if (j != idx && relayLabels[j].mode == RELAY_MODE_ONOFF && relayLabels[j].group == group)
      {
        if (turningOn)
        {
          relays[j].low();
          relays[j].update();
          relays[j].disabled = 1;
        }
        else
        {
          relays[j].disabled = 0;
        }
      }
    }
  }
  return true;
}

static bool isPulseMode(uint8_t mode)
{
  return mode == RELAY_MODE_PULSED || mode == RELAY_MODE_INTERLOCKED_PULSED;
}

bool processRelayTimers(uint32_t now)
{
  bool notify = false;

  bool needsTimerLoop = false;
  for (uint8_t j = 0; j < relayCount; j++)
  {
    if (isPulseMode(relayLabels[j].mode) && pulsed_relays[j].counter > 0)
    {
      needsTimerLoop = true;
      break;
    }
  }

  if (!needsTimerLoop)
  {
    return false;
  }

  if ((now - latched_timer) <= DELAY_INTERVAL_MS)
  {
    return false;
  }

  latched_timer = now;

  for (uint8_t i = 0; i < relayCount; i++)
  {
    if (isPulseMode(relayLabels[i].mode) && pulsed_relays[i].counter)
    {
      pulsed_relays[i].counter--;
      if (pulsed_relays[i].counter == 0)
      {
        relays[i].low();
        relays[i].update();
        relays[i].disabled = 0;
        // Re-enable the other relays of the same pulse mode in this group.
        uint8_t group = relayLabels[i].group;
        if (group > 0)
        {
          for (uint8_t j = 0; j < relayCount; j++)
          {
            if (j != i && relayLabels[j].mode == relayLabels[i].mode && relayLabels[j].group == group)
            {
              relays[j].disabled = 0;
            }
          }
        }
        notify = true;
      }
    }
  }

  return notify;
}
