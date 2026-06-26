#pragma once

#include <stdint.h>

void initWebSocket();
void notifyClients();
void notifyRelayStates();
void notifyRelayState(uint8_t relayNum);
void registerRuntimeHttpRoutes();
void startRuntimeServer();
