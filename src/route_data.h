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
constexpr uint8_t kMaxPulseTimeoutSeconds = 30;
constexpr uint8_t kDefaultPulseTimeoutSeconds = 1;
constexpr size_t kMaxTemplateTitleLength = 64;
constexpr size_t kMaxTemplateFilenameLength = 48;

inline uint8_t relayCountForVariant(const String &variant) {
  return (variant == "16relay") ? kVariantRelayCount16 : kVariantRelayCount8;
}
