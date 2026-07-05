#pragma once

#include <Arduino.h>
#include <stdint.h>

String getBootSessionToken();
void initWebSocket();
void notifyClients();
void notifyRelayStates();
void notifyRelayState(uint8_t relayNum);
void dispatchPendingNotifications();
void registerRuntimeHttpRoutes();
void startRuntimeServer();
