#include "app_state.h"

uint32_t elapsed = 0;
uint32_t timer = 0;
uint32_t latched_timer = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char *kRelayLabelsPath = "/relay_labels.json";
const size_t kMaxRelayLabelLength = 32;
const char *kBoardConfigPath = "/board_config.json";
const size_t kMaxBoardNameLength = 64;
const size_t kMaxSsidLength = 32;
const size_t kMaxPasswordLength = 64;

const uint8_t MAX_RELAYS = APP_MAX_RELAYS;
const char *kVariant8Relay = "8relay";
const char *kVariant16Relay = "16relay";

const uint32_t DELAY_INTERVAL_MS = 50;
const uint32_t DELAY_COUNTER = (1000 / DELAY_INTERVAL_MS);

String boardName = "Relay Board";
String wifiSsid;
String wifiPassword;
String serialCommandBuffer;

bool reportSignalStrength = true;
bool provisioningMode = false;
bool provisioningScanRunning = false;
bool provisioningScanRequested = false;
bool provisioningScanInitialized = false;
uint32_t provisioningScanStartedAt = 0;
String provisioningScanPayload = "{\"ssids\":[],\"scanning\":false}";

IPAddress boardIp;
IPAddress boardDns;
IPAddress boardGateway;
IPAddress boardSubnet;
bool useStaticIp = false;

bool doDelay = false;
uint16_t startupDelaySeconds = 60;
bool doLatched = false;
bool doInterlocked = false;
bool doPulsed = false;

bool pendingRestart = false;
uint32_t pendingRestartAt = 0;

String hardwareVariant = "";
uint8_t relayCount = 0;
bool useShiftRegister = false;

RelayLabel relayLabels[APP_MAX_RELAYS];

OutputPin onboard_led = {255, false, HIGH, false};

OutputPin relays[APP_MAX_RELAYS] = {
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false},
  {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}, {255, false, HIGH, false}};

const int numRegisters = APP_SHIFT_REGISTER_COUNT;
byte outputData[APP_SHIFT_REGISTER_COUNT];

Latch latched_relays[APP_MAX_RELAYS] = {
    {1, 2, 0, 0}, {2, 1, 0, 0}, {3, 4, 0, 0}, {4, 3, 0, 0},
    {5, 6, 0, 0}, {6, 5, 0, 0}, {7, 8, 0, 0}, {8, 7, 0, 0},
    {9, 10, 0, 0}, {10, 9, 0, 0}, {11, 12, 0, 0}, {12, 11, 0, 0},
    {13, 14, 0, 0}, {14, 13, 0, 0}, {15, 16, 0, 0}, {16, 15, 0, 0}};

uint8_t interlocked_buttons[APP_INTERLOCKED_BUTTON_COUNT] = {1, 2, 3, 4, 5, 6, 8};

Pulse pulsed_relays[APP_MAX_RELAYS] = {
    {1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {4, 1, 0},
    {5, 1, 0}, {6, 1, 0}, {7, 1, 0}, {8, 1, 0},
    {9, 1, 0}, {10, 1, 0}, {11, 1, 0}, {12, 1, 0},
    {13, 1, 0}, {14, 1, 0}, {15, 1, 0}, {16, 1, 0}};
