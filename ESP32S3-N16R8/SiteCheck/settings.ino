// settings.ino — runtime configuration in /config.json (LittleFS).
//
// This replaces the Wi-Fi/ntfy/timezone #defines that used to live in
// config.h. Unlike hosts.json, the values here are not sanitised — a Wi-Fi
// password may legitimately contain quotes or backslashes — so both read and
// write go through ArduinoJson, which escapes properly.

#include <LittleFS.h>
#include <ArduinoJson.h>

#include "common.h"

Settings g_settings;

static const char *CFG_PATH     = "/config.json";
static const char *CFG_TMP_PATH = "/config.tmp";

static void settingsDefaults() {
  g_settings.wifiSsid         = "";
  g_settings.wifiPass         = "";
  g_settings.hostname         = DEFAULT_HOSTNAME;
  g_settings.ntfyBase         = DEFAULT_NTFY_BASE;
  g_settings.ntfyTopic        = "";
  g_settings.ntfyToken        = "";
  g_settings.cmdSecret        = "";
  g_settings.adminPass        = "";
  g_settings.tz               = DEFAULT_TZ;
  g_settings.checkIntervalMin = DEFAULT_CHECK_INTERVAL_MIN;
  g_settings.pollIntervalSec  = DEFAULT_POLL_INTERVAL_SEC;
  g_settings.digestHour       = DEFAULT_DIGEST_HOUR;
  g_settings.digestMinute     = DEFAULT_DIGEST_MINUTE;
  g_settings.rebootHour       = DEFAULT_REBOOT_HOUR;
  g_settings.rebootMinute     = DEFAULT_REBOOT_MINUTE;
  g_settings.provisioned      = false;
}

void settingsLoad() {
  settingsDefaults();

  // exists() first: the ESP32 VFS logs an error for every missing-file open.
  File f = LittleFS.exists(CFG_PATH) ? LittleFS.open(CFG_PATH, "r") : File();
  if (!f) {
    Serial.println(F("[cfg] no config.json — device is unprovisioned"));
    return;
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<1024> doc;
#endif
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print(F("[cfg] config.json parse error: "));
    Serial.println(err.c_str());
    return;  // defaults stand; device stays unprovisioned
  }

  g_settings.wifiSsid         = (const char *)(doc["ssid"]  | "");
  g_settings.wifiPass         = (const char *)(doc["pass"]  | "");
  g_settings.hostname         = (const char *)(doc["host"]  | DEFAULT_HOSTNAME);
  g_settings.ntfyBase         = (const char *)(doc["base"]  | DEFAULT_NTFY_BASE);
  g_settings.ntfyTopic        = (const char *)(doc["topic"] | "");
  g_settings.ntfyToken        = (const char *)(doc["token"] | "");
  g_settings.cmdSecret        = (const char *)(doc["secret"]  | "");
  g_settings.adminPass        = (const char *)(doc["admpass"] | "");
  g_settings.tz               = (const char *)(doc["tz"]    | DEFAULT_TZ);
  g_settings.checkIntervalMin = doc["chk"]  | DEFAULT_CHECK_INTERVAL_MIN;
  g_settings.pollIntervalSec  = doc["poll"] | DEFAULT_POLL_INTERVAL_SEC;
  g_settings.digestHour       = doc["dh"]   | DEFAULT_DIGEST_HOUR;
  g_settings.digestMinute     = doc["dm"]   | DEFAULT_DIGEST_MINUTE;
  g_settings.rebootHour       = doc["rh"]   | DEFAULT_REBOOT_HOUR;
  g_settings.rebootMinute     = doc["rm"]   | DEFAULT_REBOOT_MINUTE;

  // Sentinel: a record only counts once it has the two things without which
  // the device cannot do its job at all.
  g_settings.provisioned = (doc["ok"] | false) &&
                           g_settings.wifiSsid.length() &&
                           g_settings.ntfyTopic.length();

  Serial.printf("[cfg] loaded: ssid=%s topic=%s tz=%s chk=%umin poll=%us\n",
                g_settings.wifiSsid.c_str(), g_settings.ntfyTopic.c_str(),
                g_settings.tz.c_str(), g_settings.checkIntervalMin,
                g_settings.pollIntervalSec);
}

bool settingsSave() {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<1024> doc;
#endif
  doc["ssid"]  = g_settings.wifiSsid;
  doc["pass"]  = g_settings.wifiPass;
  doc["host"]  = g_settings.hostname;
  doc["base"]  = g_settings.ntfyBase;
  doc["topic"] = g_settings.ntfyTopic;
  doc["token"] = g_settings.ntfyToken;
  doc["secret"]  = g_settings.cmdSecret;
  doc["admpass"] = g_settings.adminPass;
  doc["tz"]    = g_settings.tz;
  doc["chk"]   = g_settings.checkIntervalMin;
  doc["poll"]  = g_settings.pollIntervalSec;
  doc["dh"]    = g_settings.digestHour;
  doc["dm"]    = g_settings.digestMinute;
  doc["rh"]    = g_settings.rebootHour;
  doc["rm"]    = g_settings.rebootMinute;
  doc["ok"]    = true;

  // A full document drops members silently. Losing "admpass" that way would
  // lock the settings page, so refuse the save instead.
  if (doc.overflowed()) {
    Serial.println(F("[cfg] config too large for the JSON document"));
    return false;
  }

  File f = LittleFS.open(CFG_TMP_PATH, "w");
  if (!f) {
    Serial.println(F("[cfg] cannot open config.tmp for write"));
    return false;
  }
  if (serializeJson(doc, f) == 0) {
    f.close();
    Serial.println(F("[cfg] serialize failed"));
    return false;
  }
  f.close();

  LittleFS.remove(CFG_PATH);
  if (!LittleFS.rename(CFG_TMP_PATH, CFG_PATH)) {
    Serial.println(F("[cfg] config.json rename failed"));
    return false;
  }

  g_settings.provisioned = true;
  Serial.println(F("[cfg] config.json written"));
  return true;
}

void settingsErase() {
  LittleFS.remove(CFG_PATH);
  Serial.println(F("[cfg] config.json erased"));
}

void settingsClear() {
  LittleFS.remove(CFG_PATH);
  settingsDefaults();
  Serial.println(F("[cfg] config cleared"));
}
