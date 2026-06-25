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

  loadBoardHardware(hardwareVariant);

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

  if (hardwareVariant == kVariant16Relay)
  {
    for (uint8_t i = 0; i < MAX_RELAYS; i++)
    {
      latched_relays[i].relay_num = i + 1;
      latched_relays[i].latched_num = ((i % 2) == 0) ? (i + 2) : i;
      latched_relays[i].timeout = 0;
      latched_relays[i].counter = 0;
      pulsed_relays[i].relay_num = i + 1;
      pulsed_relays[i].timeout = 1;
      pulsed_relays[i].counter = 0;
    }
  }
  else
  {
    hardwareVariant = kVariant8Relay;

    uint8_t latchedPair[8] = {0, 0, 0, 0, 6, 5, 8, 7};
    uint8_t latchedTimeout[8] = {0, 2, 0, 0, 0, 0, 0, 0};
    uint8_t pulseTimeout[8] = {1, 1, 0, 0, 0, 0, 0, 0};
    for (uint8_t i = 0; i < 8; i++)
    {
      latched_relays[i].relay_num = i + 1;
      latched_relays[i].latched_num = latchedPair[i];
      latched_relays[i].timeout = latchedTimeout[i];
      latched_relays[i].counter = 0;
      pulsed_relays[i].relay_num = i + 1;
      pulsed_relays[i].timeout = pulseTimeout[i];
      pulsed_relays[i].counter = 0;
    }
    for (uint8_t i = 8; i < MAX_RELAYS; i++)
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

static void handleLatch(uint8_t relayNum)
{
  if (!(relayNum > 0 && relayNum <= relayCount))
  {
    return;
  }

  uint8_t index = relayNum - 1;
  if (latched_relays[index].timeout == 0)
  {
    return;
  }

  if (latched_relays[index].relay_num > 0 && latched_relays[index].relay_num <= relayCount)
  {
    relays[index].disabled = 1;
    latched_relays[index].counter = (latched_relays[index].timeout * DELAY_COUNTER);
  }
  if (latched_relays[index].latched_num > 0 && latched_relays[index].latched_num <= relayCount)
  {
    relays[latched_relays[index].latched_num - 1].disabled = 1;
    latched_relays[latched_relays[index].latched_num - 1].counter = (latched_relays[index].timeout * DELAY_COUNTER);
  }
}

static void handleInterlock(uint8_t relayNum)
{
  if (!(relayNum > 0 && relayNum <= relayCount))
  {
    return;
  }

  uint8_t index = relayNum - 1;
  bool interlockedMember = false;

  for (uint8_t i = 0; i < sizeof(interlocked_buttons); i++)
  {
    if (interlocked_buttons[i] == relayNum)
    {
      interlockedMember = true;
    }
  }

  if (!interlockedMember || relays[index].on != 0)
  {
    return;
  }

  for (uint8_t i = 0; i < sizeof(interlocked_buttons); i++)
  {
    if (interlocked_buttons[i] != relayNum)
    {
      relays[interlocked_buttons[i] - 1].low();
      relays[interlocked_buttons[i] - 1].update();
    }
  }
}

static void handlePulsed(uint8_t relayNum)
{
  if (!(relayNum > 0 && relayNum <= relayCount))
  {
    return;
  }

  if (pulsed_relays[relayNum - 1].timeout == 0)
  {
    return;
  }

  for (uint8_t i = 1; i <= relayCount; i++)
  {
    uint8_t index = i - 1;
    if (pulsed_relays[index].timeout == 0)
    {
      continue;
    }

    if (i == relayNum)
    {
      if (pulsed_relays[index].relay_num > 0 && pulsed_relays[index].relay_num <= relayCount)
      {
        relays[index].last = 1;
        pulsed_relays[index].counter = (pulsed_relays[index].timeout * DELAY_COUNTER);
      }
    }
    else
    {
      relays[index].last = 0;
    }
  }
}

void applyLegacyRelayModes(uint8_t relayNum)
{
  if (doLatched)
  {
    handleLatch(relayNum);
  }
  if (doInterlocked)
  {
    handleInterlock(relayNum);
  }
  if (doPulsed)
  {
    handlePulsed(relayNum);
  }
}

bool handlePerRelayModeToggle(uint8_t relayNum)
{
  if (!(relayNum > 0 && relayNum <= relayCount))
  {
    return false;
  }

  uint8_t idx = relayNum - 1;
  uint8_t mode = relayLabels[idx].mode;

  if (mode == RELAY_MODE_PULSED && relays[idx].disabled)
  {
    return true;
  }

  if (mode == RELAY_MODE_INTERLOCKED)
  {
    uint8_t group = relayLabels[idx].group;
    bool turningOn = !relays[idx].on;
    relays[idx].toggle();
    relays[idx].update();
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
    return true;
  }

  applyLegacyRelayModes(relayNum);
  relays[idx].toggle();
  relays[idx].update();
  return true;
}

bool processRelayTimers(uint32_t now)
{
  bool notify = false;

  bool needsTimerLoop = doLatched || doPulsed;
  if (!needsTimerLoop)
  {
    for (uint8_t j = 0; j < relayCount; j++)
    {
      if (relayLabels[j].mode == RELAY_MODE_PULSED && pulsed_relays[j].counter > 0)
      {
        needsTimerLoop = true;
        break;
      }
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
    if (doLatched && latched_relays[i].counter)
    {
      latched_relays[i].counter--;
      if (latched_relays[i].counter == 0)
      {
        relays[i].low();
        relays[i].update();
        relays[i].disabled = false;
        notify = true;
      }
    }

    if (doPulsed && relayLabels[i].mode != RELAY_MODE_PULSED && pulsed_relays[i].counter)
    {
      pulsed_relays[i].counter--;
      if (pulsed_relays[i].counter == 0)
      {
        relays[i].low();
        relays[i].update();
        notify = true;
      }
    }

    if (relayLabels[i].mode == RELAY_MODE_PULSED && pulsed_relays[i].counter)
    {
      pulsed_relays[i].counter--;
      if (pulsed_relays[i].counter == 0)
      {
        relays[i].low();
        relays[i].update();
        relays[i].disabled = 0;
        notify = true;
      }
    }
  }

  return notify;
}
