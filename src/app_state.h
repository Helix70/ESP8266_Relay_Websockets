#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "storage_utils.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

constexpr uint8_t APP_MAX_RELAYS = 16;
constexpr uint8_t APP_INTERLOCKED_BUTTON_COUNT = 7;
constexpr uint8_t APP_SHIFT_REGISTER_COUNT = 2;

struct OutputPin {
  uint8_t pin;
  uint8_t on;
  const uint8_t on_state;
  uint8_t disabled;
  uint8_t last;

  void update() {
    if (pin != 255) {
      digitalWrite(pin, on ? HIGH : LOW);
    }
  }

  void low() { on = !on_state; }
  void high() { on = on_state; }
  void toggle() { on = !on; }
  uint8_t state() { return on; }
};

struct Latch {
  uint8_t relay_num;
  uint8_t latched_num;
  uint8_t timeout;
  uint16_t counter;
};

struct Pulse {
  uint8_t relay_num;
  uint8_t timeout;
  uint16_t counter;
};

extern const char *kRelayLabelsPath;
extern const char *kBoardConfigPath;
extern const size_t kMaxRelayLabelLength;
extern const size_t kMaxBoardNameLength;
extern const size_t kMaxSsidLength;
extern const size_t kMaxPasswordLength;

extern const uint8_t MAX_RELAYS;
extern const char *kVariant8Relay;
extern const char *kVariant16Relay;

extern const uint32_t DELAY_INTERVAL_MS;
extern const uint32_t DELAY_COUNTER;

extern AsyncWebServer server;
extern AsyncWebSocket ws;

extern uint32_t elapsed;
extern uint32_t timer;
extern uint32_t latched_timer;

extern String boardName;
extern String wifiSsid;
extern String wifiPassword;
extern String serialCommandBuffer;

extern bool reportSignalStrength;
extern bool provisioningMode;
extern bool provisioningScanRunning;
extern bool provisioningScanRequested;
extern bool provisioningScanInitialized;
extern uint32_t provisioningScanStartedAt;
extern String provisioningScanPayload;

extern IPAddress boardIp;
extern IPAddress boardDns;
extern IPAddress boardGateway;
extern IPAddress boardSubnet;
extern bool useStaticIp;

extern bool doDelay;
extern uint16_t startupDelaySeconds;
extern bool doLatched;
extern bool doInterlocked;
extern bool doPulsed;

extern bool pendingRestart;
extern uint32_t pendingRestartAt;

extern String hardwareVariant;
extern uint8_t relayCount;
extern bool useShiftRegister;

extern RelayLabel relayLabels[APP_MAX_RELAYS];
extern OutputPin onboard_led;
extern OutputPin relays[APP_MAX_RELAYS];

extern const int numRegisters;
extern byte outputData[APP_SHIFT_REGISTER_COUNT];

extern Latch latched_relays[APP_MAX_RELAYS];
extern uint8_t interlocked_buttons[APP_INTERLOCKED_BUTTON_COUNT];
extern Pulse pulsed_relays[APP_MAX_RELAYS];
