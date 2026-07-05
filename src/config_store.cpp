#include "config_store.h"

#include "LittleFS.h"
#include <ArduinoJson.h>

#include "board_hardware.h"
#include "storage_utils.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

#if defined(ESP32)
#include <Preferences.h>
#endif

namespace
{
constexpr const char *kDefaultBoardName = "Relay Board";
constexpr const char *kDefaultVariant = ""; // empty = not yet configured
constexpr const char *kDefaultThemeHex = "#F8F7F9,#143642,#0f8b8d,#cf2700,#143642,#0f8b8d,#ffffff,#ffffff,#ffffff";
constexpr const char *kDefaultThemeStyle = "classic";
constexpr size_t kMaxBoardNameLength = 64;
constexpr size_t kMaxSsidLength = 32;
constexpr size_t kMaxPasswordLength = 64;

#if defined(ESP8266)
constexpr size_t kEepromTotalSize = 4096;
constexpr int kBoardConfigEepromBase = 0;
constexpr size_t kBoardConfigEepromRegionSize = 2048;
constexpr uint32_t kBoardConfigEepromMagic = 0x31434252u; // "RBC1"
constexpr uint16_t kBoardConfigEepromVersion = 1;
constexpr int kBoardConfigHeaderOffsetMagic = kBoardConfigEepromBase + 0;
constexpr int kBoardConfigHeaderOffsetVersion = kBoardConfigEepromBase + 4;
constexpr int kBoardConfigHeaderOffsetLength = kBoardConfigEepromBase + 6;
constexpr int kBoardConfigHeaderOffsetCrc = kBoardConfigEepromBase + 8;
constexpr int kBoardConfigPayloadOffset = kBoardConfigEepromBase + 10;
constexpr size_t kBoardConfigMaxPayload = kBoardConfigEepromRegionSize - 10;

uint16_t crc16Ccitt(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++)
    {
      if (crc & 0x8000u)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void eepromWriteU16(int offset, uint16_t value)
{
  EEPROM.write(offset, (uint8_t)(value & 0xFFu));
  EEPROM.write(offset + 1, (uint8_t)((value >> 8) & 0xFFu));
}

void eepromWriteU32(int offset, uint32_t value)
{
  EEPROM.write(offset, (uint8_t)(value & 0xFFu));
  EEPROM.write(offset + 1, (uint8_t)((value >> 8) & 0xFFu));
  EEPROM.write(offset + 2, (uint8_t)((value >> 16) & 0xFFu));
  EEPROM.write(offset + 3, (uint8_t)((value >> 24) & 0xFFu));
}

uint16_t eepromReadU16(int offset)
{
  return (uint16_t)EEPROM.read(offset) |
         (uint16_t)((uint16_t)EEPROM.read(offset + 1) << 8);
}

uint32_t eepromReadU32(int offset)
{
  return (uint32_t)EEPROM.read(offset) |
         ((uint32_t)EEPROM.read(offset + 1) << 8) |
         ((uint32_t)EEPROM.read(offset + 2) << 16) |
         ((uint32_t)EEPROM.read(offset + 3) << 24);
}

bool saveBoardConfigToEepromJson(const char *payload, size_t payloadLen)
{
  if (payloadLen == 0 || payloadLen > kBoardConfigMaxPayload)
  {
    return false;
  }

  EEPROM.begin((int)kEepromTotalSize);

  uint16_t len16 = (uint16_t)payloadLen;
  uint16_t crc = crc16Ccitt((const uint8_t *)payload, len16);

  eepromWriteU32(kBoardConfigHeaderOffsetMagic, kBoardConfigEepromMagic);
  eepromWriteU16(kBoardConfigHeaderOffsetVersion, kBoardConfigEepromVersion);
  eepromWriteU16(kBoardConfigHeaderOffsetLength, len16);
  eepromWriteU16(kBoardConfigHeaderOffsetCrc, crc);

  for (uint16_t i = 0; i < len16; i++)
  {
    EEPROM.write(kBoardConfigPayloadOffset + i, (uint8_t)payload[i]);
  }

  bool ok = EEPROM.commit();
  EEPROM.end();
  return ok;
}

bool loadBoardConfigFromEepromJson(char *payloadOut, size_t maxLen)
{
  EEPROM.begin((int)kEepromTotalSize);

  uint32_t magic = eepromReadU32(kBoardConfigHeaderOffsetMagic);
  uint16_t version = eepromReadU16(kBoardConfigHeaderOffsetVersion);
  uint16_t payloadLen = eepromReadU16(kBoardConfigHeaderOffsetLength);
  uint16_t expectedCrc = eepromReadU16(kBoardConfigHeaderOffsetCrc);

  if (magic != kBoardConfigEepromMagic || version != kBoardConfigEepromVersion ||
      payloadLen == 0 || payloadLen > kBoardConfigMaxPayload || payloadLen >= (uint16_t)maxLen)
  {
    EEPROM.end();
    return false;
  }

  for (uint16_t i = 0; i < payloadLen; i++)
  {
    payloadOut[i] = (char)EEPROM.read(kBoardConfigPayloadOffset + i);
  }
  payloadOut[payloadLen] = '\0';
  EEPROM.end();

  uint16_t actualCrc = crc16Ccitt((const uint8_t *)payloadOut, payloadLen);
  return (actualCrc == expectedCrc);
}
#endif

uint32_t getCryptoSeed()
{
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  return (uint32_t)(mac ^ (mac >> 32) ^ 0xA5A55A5Au);
#elif defined(ESP8266)
  return ESP.getChipId() ^ 0xA5A55A5Au;
#else
  return 0x5A5AA5A5u;
#endif
}

String hexEncode(const uint8_t *data, size_t len)
{
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++)
  {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

int hexNibble(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool hexDecode(const String &hexText, String &decoded)
{
  decoded = "";
  if ((hexText.length() % 2) != 0)
  {
    return false;
  }

  decoded.reserve(hexText.length() / 2);
  for (size_t i = 0; i < hexText.length(); i += 2)
  {
    int hi = hexNibble(hexText[i]);
    int lo = hexNibble(hexText[i + 1]);
    if (hi < 0 || lo < 0)
    {
      decoded = "";
      return false;
    }
    decoded += (char)((hi << 4) | lo);
  }
  return true;
}

uint8_t detectTemplateRelayCountFromFilename(const String &filename)
{
  String lower = filename;
  lower.toLowerCase();
  return (lower.indexOf("16") >= 0) ? (uint8_t)16 : (uint8_t)8;
}

uint8_t parseTemplateRelayCount(const String &filename)
{
  String cleanName = filename;
  cleanName.trim();
  if (cleanName.length() == 0)
  {
    return 0;
  }

  if (cleanName.startsWith("/templates/"))
  {
    cleanName = cleanName.substring(String("/templates/").length());
  }

  if (cleanName.indexOf('/') >= 0 || cleanName.indexOf('\\') >= 0 || cleanName.indexOf("..") >= 0)
  {
    return 0;
  }

  String path = String("/templates/") + cleanName;
  if (!LittleFS.exists(path))
  {
    return 0;
  }

  File f = LittleFS.open(path, "r");
  if (!f)
  {
    return 0;
  }

  String probe;
  probe.reserve(640);
  while (f.available() && probe.length() < 640)
  {
    probe += (char)f.read();
  }
  f.close();

  uint8_t relayCount = detectTemplateRelayCountFromFilename(cleanName);

  int relayKey = probe.indexOf("\"relayCount\"");
  if (relayKey < 0)
  {
    relayKey = probe.indexOf("\"n\"");
  }
  if (relayKey >= 0)
  {
    int colon = probe.indexOf(':', relayKey);
    if (colon > 0)
    {
      String parsedNumber;
      for (int i = colon + 1; i < (int)probe.length(); i++)
      {
        char c = probe[i];
        if (c >= '0' && c <= '9')
        {
          parsedNumber += c;
        }
        else if (parsedNumber.length() > 0)
        {
          break;
        }
      }

      int parsedRelayCount = parsedNumber.toInt();
      if (parsedRelayCount == 8 || parsedRelayCount == 16)
      {
        relayCount = (uint8_t)parsedRelayCount;
      }
    }
  }

  return relayCount;
}
} // namespace

extern const char *kBoardConfigPath;
extern String boardName;
extern char themeHex[80];
extern char themeStyle[12];
extern String wifiSsid;
extern String wifiPassword;
extern String selectedRelayTemplateFilename;
extern bool useStaticIp;
extern bool doDelay;
extern uint16_t startupDelaySeconds;
extern bool connectStrongestOnStartup;
extern String hardwareVariant;
extern String activeBoardHardwareFilename;
extern uint8_t relayCount;
extern bool useShiftRegister;
extern IPAddress boardIp;
extern IPAddress boardDns;
extern IPAddress boardGateway;
extern IPAddress boardSubnet;

extern void applyHardwareVariantPinsAndModes();

bool reconcileSelectedTemplateForActiveHardware(bool applyFallbackLabels)
{
  if (relayCount == 0)
  {
    return false;
  }

  String selected = selectedRelayTemplateFilename;
  selected.trim();
  if (selected.length() == 0)
  {
    return false;
  }

  uint8_t templateRelayCount = parseTemplateRelayCount(selected);
  if (templateRelayCount == relayCount)
  {
    return false;
  }

  String fallbackTemplate = "template-" + String(relayCount) + "relay.json";
  bool fallbackAvailable = loadLabelsFromTemplateFile(fallbackTemplate, relayCount);

  if (fallbackAvailable)
  {
    selectedRelayTemplateFilename = fallbackTemplate;
    if (applyFallbackLabels)
    {
      saveRelayLabels();
    }
  }
  else
  {
    selectedRelayTemplateFilename = "";
  }

  saveBoardConfig();

  Serial.printf("Reset selected template due to relay-count mismatch (selected=%s, relayCount=%u, fallback=%s, applied=%d)\n",
                selected.c_str(), relayCount, fallbackTemplate.c_str(), fallbackAvailable ? 1 : 0);
  return true;
}

String encryptConfigSecret(const String &plain)
{
  if (plain.length() == 0)
  {
    return "";
  }

  uint32_t seed = getCryptoSeed();
  const size_t len = plain.length();
  uint8_t *buf = (uint8_t *)malloc(len);
  if (!buf)
  {
    return "";
  }

  for (size_t i = 0; i < len; i++)
  {
    uint8_t keyByte = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
    buf[i] = ((uint8_t)plain[i]) ^ keyByte ^ (uint8_t)(0x3Du + (i * 17u));
  }

  String encoded = hexEncode(buf, len);
  free(buf);
  return encoded;
}

String decryptConfigSecret(const String &encoded)
{
  if (encoded.length() == 0)
  {
    return "";
  }

  String cipher;
  if (!hexDecode(encoded, cipher))
  {
    return "";
  }

  uint32_t seed = getCryptoSeed();
  String plain;
  plain.reserve(cipher.length());
  for (size_t i = 0; i < cipher.length(); i++)
  {
    uint8_t keyByte = (uint8_t)((seed >> ((i % 4) * 8)) & 0xFF);
    plain += (char)(((uint8_t)cipher[i]) ^ keyByte ^ (uint8_t)(0x3Du + (i * 17u)));
  }
  return plain;
}

bool saveBoardConfig()
{
#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("board", false))
  {
    Serial.println("ERROR: Failed to open NVS board namespace");
    return false;
  }
  prefs.putString("name", boardName);
  prefs.putBool("doDelay", doDelay);
  prefs.putUShort("delaySec", startupDelaySeconds);
  prefs.putBool("strongestSsid", connectStrongestOnStartup);
  prefs.putString("hwVar", hardwareVariant);
  prefs.putString("hwFile", activeBoardHardwareFilename);
  prefs.putBool("useStatic", useStaticIp);
  prefs.putString("wifiSsidEnc", encryptConfigSecret(wifiSsid));
  prefs.putString("wifiPwdEnc", encryptConfigSecret(wifiPassword));
  prefs.putString("selTpl", selectedRelayTemplateFilename);
  prefs.putString("themeH", themeHex);
  prefs.putString("themeS", themeStyle);
  if (useStaticIp)
  {
    prefs.putString("ip", boardIp.toString());
    prefs.putString("dns", boardDns.toString());
    prefs.putString("gateway", boardGateway.toString());
    prefs.putString("subnet", boardSubnet.toString());
    Serial.printf("saveBoardConfig: saving static IP %s\n", boardIp.toString().c_str());
  }
  else
  {
    if (prefs.isKey("ip")) prefs.remove("ip");
    if (prefs.isKey("dns")) prefs.remove("dns");
    if (prefs.isKey("gateway")) prefs.remove("gateway");
    if (prefs.isKey("subnet")) prefs.remove("subnet");
    Serial.println("saveBoardConfig: saving DHCP mode");
  }
  prefs.end();
  Serial.printf("Saved board config to NVS: name=%s, dhcp=%d\n", boardName.c_str(), !useStaticIp);
  return true;
#elif defined(ESP8266)
  JsonDocument doc;
  doc["name"] = boardName;
  doc["doDelay"] = doDelay;
  doc["startupDelaySeconds"] = startupDelaySeconds;
  doc["connectStrongestOnStartup"] = connectStrongestOnStartup;
  doc["hardwareVariant"] = hardwareVariant;
  doc["activeBoardHardwareFile"] = activeBoardHardwareFilename;
  doc["selectedRelayTemplate"] = selectedRelayTemplateFilename;
  doc["wifiSsidEnc"] = encryptConfigSecret(wifiSsid);
  doc["wifiPwdEnc"] = encryptConfigSecret(wifiPassword);
  doc["themeH"] = themeHex;
  doc["themeS"] = themeStyle;

  if (useStaticIp)
  {
    JsonObject ipConfig = doc["ipConfig"].to<JsonObject>();
    ipConfig["ip"] = boardIp.toString();
    ipConfig["dns"] = boardDns.toString();
    ipConfig["gateway"] = boardGateway.toString();
    ipConfig["subnet"] = boardSubnet.toString();
  }
  else
  {
    doc["ipConfig"] = nullptr;
  }

  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));
  if (!saveBoardConfigToEepromJson(payload, strlen(payload)))
  {
    Serial.println("Failed to save board config to EEPROM");
    return false;
  }

  Serial.printf("Saved board config to EEPROM: name=%s, dhcp=%d\n", boardName.c_str(), !useStaticIp);
  return true;
#else
  JsonDocument doc;
  doc["name"] = boardName;
  doc["doDelay"] = doDelay;
  doc["startupDelaySeconds"] = startupDelaySeconds;
  doc["connectStrongestOnStartup"] = connectStrongestOnStartup;
  doc["hardwareVariant"] = hardwareVariant;
  doc["activeBoardHardwareFile"] = activeBoardHardwareFilename;
  doc["selectedRelayTemplate"] = selectedRelayTemplateFilename;
  doc["wifiSsidEnc"] = encryptConfigSecret(wifiSsid);
  doc["wifiPwdEnc"] = encryptConfigSecret(wifiPassword);

  if (useStaticIp)
  {
    JsonObject ipConfig = doc["ipConfig"].to<JsonObject>();
    ipConfig["ip"] = boardIp.toString();
    ipConfig["dns"] = boardDns.toString();
    ipConfig["gateway"] = boardGateway.toString();
    ipConfig["subnet"] = boardSubnet.toString();
  }
  else
  {
    doc["ipConfig"] = nullptr;
  }

  File file = LittleFS.open(kBoardConfigPath, "w");
  if (!file) return false;

  size_t bytesWritten = serializeJson(doc, file);
  file.close();
  if (bytesWritten == 0) return false;

  Serial.printf("Saved board config: name=%s, dhcp=%d\n", boardName.c_str(), !useStaticIp);
  return true;
#endif
}

bool clearBoardConfig()
{
#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("board", false))
  {
    return false;
  }
  prefs.clear();
  prefs.end();
  return true;
#elif defined(ESP8266)
  EEPROM.begin((int)kEepromTotalSize);
  for (int i = 0; i < 4; i++)
  {
    EEPROM.write(kBoardConfigHeaderOffsetMagic + i, 0);
  }
  bool ok = EEPROM.commit();
  EEPROM.end();
  return ok;
#else
  if (LittleFS.exists(kBoardConfigPath))
  {
    return LittleFS.remove(kBoardConfigPath);
  }
  return true;
#endif
}

void loadBoardConfig()
{
  boardName = kDefaultBoardName;
  useStaticIp = false;
  doDelay = false;
  startupDelaySeconds = 0;
  connectStrongestOnStartup = true;
  hardwareVariant = kDefaultVariant;
  activeBoardHardwareFilename = boardHardwarePath(hardwareVariant);
  relayCount = 8;
  useShiftRegister = false;
  wifiSsid = "";
  wifiPassword = "";
  selectedRelayTemplateFilename = "";
  strlcpy(themeHex, kDefaultThemeHex, sizeof(themeHex));
  strlcpy(themeStyle, kDefaultThemeStyle, sizeof(themeStyle));

#ifdef ESP32
  Preferences prefs;
  if (!prefs.begin("board", true))
  {
    Serial.println("NVS board namespace empty, using defaults");
    return;
  }

  if (prefs.isKey("name"))
  {
    String name = prefs.getString("name", "");
    name.trim();
    if (name.length() > kMaxBoardNameLength)
      name = name.substring(0, kMaxBoardNameLength);
    if (name.length() > 0)
      boardName = name;
  }

  doDelay = prefs.getBool("doDelay", false);
  startupDelaySeconds = prefs.getUShort("delaySec", 0);
  connectStrongestOnStartup = prefs.getBool("strongestSsid", true);
  hardwareVariant = prefs.getString("hwVar", kDefaultVariant);
  hardwareVariant.trim();
  activeBoardHardwareFilename = prefs.getString("hwFile", "");
  activeBoardHardwareFilename.trim();
  if (activeBoardHardwareFilename.length() == 0)
  {
    activeBoardHardwareFilename = boardHardwarePath(hardwareVariant);
  }
  if (prefs.isKey("wifiSsidEnc"))
    wifiSsid = decryptConfigSecret(prefs.getString("wifiSsidEnc", ""));
  if (prefs.isKey("wifiPwdEnc"))
    wifiPassword = decryptConfigSecret(prefs.getString("wifiPwdEnc", ""));
  selectedRelayTemplateFilename = prefs.getString("selTpl", "");
  selectedRelayTemplateFilename.trim();
  if (prefs.isKey("themeH"))
  {
    String th = prefs.getString("themeH", kDefaultThemeHex);
    strlcpy(themeHex, th.c_str(), sizeof(themeHex));
  }
  if (prefs.isKey("themeS"))
  {
    String ts = prefs.getString("themeS", kDefaultThemeStyle);
    strlcpy(themeStyle, ts.c_str(), sizeof(themeStyle));
  }

  if (wifiSsid.length() > kMaxSsidLength)
    wifiSsid = wifiSsid.substring(0, kMaxSsidLength);
  if (wifiPassword.length() > kMaxPasswordLength)
    wifiPassword = wifiPassword.substring(0, kMaxPasswordLength);

  if (prefs.getBool("useStatic", false))
  {
    String ipStr = prefs.getString("ip", "");
    String dnsStr = prefs.getString("dns", "");
    String gatewayStr = prefs.getString("gateway", "");
    String subnetStr = prefs.getString("subnet", "");

    if (ipStr.length() > 0 && boardIp.fromString(ipStr) &&
        dnsStr.length() > 0 && boardDns.fromString(dnsStr) &&
        gatewayStr.length() > 0 && boardGateway.fromString(gatewayStr) &&
        subnetStr.length() > 0 && boardSubnet.fromString(subnetStr))
    {
      useStaticIp = true;
      Serial.printf("Loaded static IP: %s\n", boardIp.toString().c_str());
    }
  }

  prefs.end();
  applyHardwareVariantPinsAndModes();
  Serial.printf("Loaded board config from NVS: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, strongestSsid=%d, wifiSsidSet=%d\n",
                boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, connectStrongestOnStartup, wifiSsid.length() > 0);
#elif defined(ESP8266)
  JsonDocument doc;
  bool loaded = false;

  char eepromPayload[1024];
  if (loadBoardConfigFromEepromJson(eepromPayload, sizeof(eepromPayload)))
  {
    if (deserializeJson(doc, eepromPayload) == DeserializationError::Ok)
    {
      loaded = true;
      Serial.println("Loaded board config from EEPROM");
    }
  }

  if (!loaded && LittleFS.exists(kBoardConfigPath))
  {
    File file = LittleFS.open(kBoardConfigPath, "r");
    if (file)
    {
      if (deserializeJson(doc, file) == DeserializationError::Ok)
      {
        loaded = true;
        Serial.println("Loaded board config from LittleFS (legacy)");
      }
      file.close();
    }

    // Migrate legacy file config into EEPROM so it survives uploadfs.
    if (loaded)
    {
      char migratedPayload[1024];
      serializeJson(doc, migratedPayload, sizeof(migratedPayload));
      if (saveBoardConfigToEepromJson(migratedPayload, strlen(migratedPayload)))
      {
        Serial.println("Migrated board config to EEPROM");
      }
    }
  }

  if (!loaded)
  {
    Serial.printf("Board config not found in EEPROM, using defaults\n");
    return;
  }

  String name = doc["name"];
  if (name.length() > 0)
  {
    name.trim();
    if (name.length() > kMaxBoardNameLength)
      name = name.substring(0, kMaxBoardNameLength);
    boardName = name;
  }

  if (doc["doDelay"].is<JsonVariantConst>()) doDelay = doc["doDelay"] | false;
  if (doc["startupDelaySeconds"].is<JsonVariantConst>()) startupDelaySeconds = doc["startupDelaySeconds"] | 0;
  if (doc["connectStrongestOnStartup"].is<JsonVariantConst>()) connectStrongestOnStartup = doc["connectStrongestOnStartup"] | true;
  if (doc["hardwareVariant"].is<JsonVariantConst>())
  {
    hardwareVariant = String(doc["hardwareVariant"] | kDefaultVariant);
    hardwareVariant.trim();
  }
  if (doc["activeBoardHardwareFile"].is<JsonVariantConst>())
  {
    activeBoardHardwareFilename = String(doc["activeBoardHardwareFile"] | "");
    activeBoardHardwareFilename.trim();
  }
  if (activeBoardHardwareFilename.length() == 0)
  {
    activeBoardHardwareFilename = boardHardwarePath(hardwareVariant);
  }
  wifiSsid = decryptConfigSecret(String(doc["wifiSsidEnc"] | ""));
  wifiPassword = decryptConfigSecret(String(doc["wifiPwdEnc"] | ""));
  selectedRelayTemplateFilename = String(doc["selectedRelayTemplate"] | "");
  selectedRelayTemplateFilename.trim();
  if (doc["themeH"].is<JsonVariantConst>())
  {
    const char *th = doc["themeH"] | kDefaultThemeHex;
    strlcpy(themeHex, th, sizeof(themeHex));
  }
  if (doc["themeS"].is<JsonVariantConst>())
  {
    const char *ts = doc["themeS"] | kDefaultThemeStyle;
    strlcpy(themeStyle, ts, sizeof(themeStyle));
  }
  if (wifiSsid.length() > kMaxSsidLength)
    wifiSsid = wifiSsid.substring(0, kMaxSsidLength);
  if (wifiPassword.length() > kMaxPasswordLength)
    wifiPassword = wifiPassword.substring(0, kMaxPasswordLength);

  JsonObject ipConfig = doc["ipConfig"];
  if (!ipConfig.isNull())
  {
    String ipStr = ipConfig["ip"], dnsStr = ipConfig["dns"],
           gatewayStr = ipConfig["gateway"], subnetStr = ipConfig["subnet"];
    if (ipStr.length() > 0 && boardIp.fromString(ipStr) &&
        dnsStr.length() > 0 && boardDns.fromString(dnsStr) &&
        gatewayStr.length() > 0 && boardGateway.fromString(gatewayStr) &&
        subnetStr.length() > 0 && boardSubnet.fromString(subnetStr))
      useStaticIp = true;
  }

  applyHardwareVariantPinsAndModes();

  Serial.printf("Loaded board config: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, strongestSsid=%d, wifiSsidSet=%d\n", boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, connectStrongestOnStartup, wifiSsid.length() > 0);
#else
  if (!LittleFS.exists(kBoardConfigPath))
  {
    Serial.printf("Board config file not found, using defaults\n");
    return;
  }

  File file = LittleFS.open(kBoardConfigPath, "r");
  if (!file) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("Failed to parse board config (%s), using defaults\n", error.c_str());
    return;
  }

  String name = doc["name"];
  if (name.length() > 0)
  {
    name.trim();
    if (name.length() > kMaxBoardNameLength)
      name = name.substring(0, kMaxBoardNameLength);
    boardName = name;
  }

  if (doc["doDelay"].is<JsonVariantConst>()) doDelay = doc["doDelay"] | false;
  if (doc["startupDelaySeconds"].is<JsonVariantConst>()) startupDelaySeconds = doc["startupDelaySeconds"] | 0;
  if (doc["connectStrongestOnStartup"].is<JsonVariantConst>()) connectStrongestOnStartup = doc["connectStrongestOnStartup"] | true;
  if (doc["hardwareVariant"].is<JsonVariantConst>())
  {
    hardwareVariant = String(doc["hardwareVariant"] | kDefaultVariant);
    hardwareVariant.trim();
  }
  if (doc["activeBoardHardwareFile"].is<JsonVariantConst>())
  {
    activeBoardHardwareFilename = String(doc["activeBoardHardwareFile"] | "");
    activeBoardHardwareFilename.trim();
  }
  if (activeBoardHardwareFilename.length() == 0)
  {
    activeBoardHardwareFilename = boardHardwarePath(hardwareVariant);
  }
  wifiSsid = decryptConfigSecret(String(doc["wifiSsidEnc"] | ""));
  wifiPassword = decryptConfigSecret(String(doc["wifiPwdEnc"] | ""));
  selectedRelayTemplateFilename = String(doc["selectedRelayTemplate"] | "");
  selectedRelayTemplateFilename.trim();
  if (wifiSsid.length() > kMaxSsidLength)
    wifiSsid = wifiSsid.substring(0, kMaxSsidLength);
  if (wifiPassword.length() > kMaxPasswordLength)
    wifiPassword = wifiPassword.substring(0, kMaxPasswordLength);

  JsonObject ipConfig = doc["ipConfig"];
  if (!ipConfig.isNull())
  {
    String ipStr = ipConfig["ip"], dnsStr = ipConfig["dns"],
           gatewayStr = ipConfig["gateway"], subnetStr = ipConfig["subnet"];
    if (ipStr.length() > 0 && boardIp.fromString(ipStr) &&
        dnsStr.length() > 0 && boardDns.fromString(dnsStr) &&
        gatewayStr.length() > 0 && boardGateway.fromString(gatewayStr) &&
        subnetStr.length() > 0 && boardSubnet.fromString(subnetStr))
      useStaticIp = true;
  }

  applyHardwareVariantPinsAndModes();

  Serial.printf("Loaded board config: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, strongestSsid=%d, wifiSsidSet=%d\n", boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, connectStrongestOnStartup, wifiSsid.length() > 0);
#endif
}
