#include "config_store.h"

#include "LittleFS.h"
#include <ArduinoJson.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
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
constexpr size_t kMaxBoardNameLength = 64;
constexpr size_t kMaxSsidLength = 32;
constexpr size_t kMaxPasswordLength = 64;

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
} // namespace

extern const char *kBoardConfigPath;
extern String boardName;
extern String wifiSsid;
extern String wifiPassword;
extern bool useStaticIp;
extern bool doDelay;
extern uint16_t startupDelaySeconds;
extern bool doLatched;
extern bool doInterlocked;
extern bool doPulsed;
extern String hardwareVariant;
extern uint8_t relayCount;
extern bool useShiftRegister;
extern IPAddress boardIp;
extern IPAddress boardDns;
extern IPAddress boardGateway;
extern IPAddress boardSubnet;

extern void applyHardwareVariantPinsAndModes();

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
  prefs.putBool("doLatched", doLatched);
  prefs.putBool("doInterlocked", doInterlocked);
  prefs.putBool("doPulsed", doPulsed);
  prefs.putString("hwVar", hardwareVariant);
  prefs.putBool("useStatic", useStaticIp);
  prefs.putString("wifiSsidEnc", encryptConfigSecret(wifiSsid));
  prefs.putString("wifiPwdEnc", encryptConfigSecret(wifiPassword));
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
#else
  StaticJsonDocument<1536> doc;
  doc["name"] = boardName;
  doc["doDelay"] = doDelay;
  doc["startupDelaySeconds"] = startupDelaySeconds;
  doc["doLatched"] = doLatched;
  doc["doInterlocked"] = doInterlocked;
  doc["doPulsed"] = doPulsed;
  doc["hardwareVariant"] = hardwareVariant;
  doc["wifiSsidEnc"] = encryptConfigSecret(wifiSsid);
  doc["wifiPwdEnc"] = encryptConfigSecret(wifiPassword);

  if (useStaticIp)
  {
    JsonObject ipConfig = doc.createNestedObject("ipConfig");
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

void loadBoardConfig()
{
  boardName = kDefaultBoardName;
  useStaticIp = false;
  doDelay = false;
  startupDelaySeconds = 60;
  doLatched = false;
  doInterlocked = false;
  doPulsed = false;
  hardwareVariant = kDefaultVariant;
  relayCount = 8;
  useShiftRegister = false;
  wifiSsid = "";
  wifiPassword = "";

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
  startupDelaySeconds = prefs.getUShort("delaySec", 60);
  doLatched = prefs.getBool("doLatched", false);
  doInterlocked = prefs.getBool("doInterlocked", false);
  doPulsed = prefs.getBool("doPulsed", false);
  hardwareVariant = prefs.getString("hwVar", kDefaultVariant);
  hardwareVariant.trim();
  if (prefs.isKey("wifiSsidEnc"))
    wifiSsid = decryptConfigSecret(prefs.getString("wifiSsidEnc", ""));
  if (prefs.isKey("wifiPwdEnc"))
    wifiPassword = decryptConfigSecret(prefs.getString("wifiPwdEnc", ""));

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
  Serial.printf("Loaded board config from NVS: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, doLatched=%d, doInterlocked=%d, doPulsed=%d, wifiSsidSet=%d\n",
                boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, doLatched, doInterlocked, doPulsed, wifiSsid.length() > 0);
#else
  if (!LittleFS.exists(kBoardConfigPath))
  {
    Serial.printf("Board config file not found, using defaults\n");
    return;
  }

  File file = LittleFS.open(kBoardConfigPath, "r");
  if (!file) return;

  StaticJsonDocument<1536> doc;
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

  if (doc.containsKey("doDelay")) doDelay = doc["doDelay"] | false;
  if (doc.containsKey("startupDelaySeconds")) startupDelaySeconds = doc["startupDelaySeconds"] | 60;
  if (doc.containsKey("doLatched")) doLatched = doc["doLatched"] | false;
  if (doc.containsKey("doInterlocked")) doInterlocked = doc["doInterlocked"] | false;
  if (doc.containsKey("doPulsed")) doPulsed = doc["doPulsed"] | false;
  if (doc.containsKey("hardwareVariant"))
  {
    hardwareVariant = String(doc["hardwareVariant"] | kDefaultVariant);
    hardwareVariant.trim();
  }
  wifiSsid = decryptConfigSecret(String(doc["wifiSsidEnc"] | ""));
  wifiPassword = decryptConfigSecret(String(doc["wifiPwdEnc"] | ""));
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

  Serial.printf("Loaded board config: name=%s, dhcp=%d, doDelay=%d, startupDelaySeconds=%u, wifiSsidSet=%d\n", boardName.c_str(), !useStaticIp, doDelay, startupDelaySeconds, wifiSsid.length() > 0);
#endif
}
