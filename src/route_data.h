#pragma once

#include <Arduino.h>

constexpr uint8_t kVariantRelayCount8 = 8;
constexpr uint8_t kVariantRelayCount16 = 16;
constexpr uint8_t kSupportedVariantCount = 2;
constexpr const char *kSupportedVariants[kSupportedVariantCount] = {"8relay",
                                                                    "16relay"};

constexpr uint8_t kDefaultBoardLedPin = 2;
constexpr uint8_t kDefaultShiftRegisterLatchPin = 12;
constexpr uint8_t kDefaultShiftRegisterClockPin = 13;
constexpr uint8_t kDefaultShiftRegisterDataPin = 14;
constexpr uint8_t kDefaultShiftRegisterOePin = 5;

constexpr uint16_t kMaxStartupDelaySeconds = 300;
constexpr uint8_t kMaxPulseTimeoutSeconds = 60;
constexpr uint8_t kDefaultPulseTimeoutSeconds = 1;
constexpr size_t kMaxTemplateTitleLength = 40;
// LFS_NAME_MAX = 32 on ESP8266; temp file is slug+".jt" (3 bytes), final is
// slug+".json" (5 bytes) — final is the binding constraint: slug <= 26.
constexpr size_t kMaxTemplateFilenameLength = 26;

inline uint8_t relayCountForVariant(const String &variant) {
  return (variant == "16relay") ? kVariantRelayCount16 : kVariantRelayCount8;
}
