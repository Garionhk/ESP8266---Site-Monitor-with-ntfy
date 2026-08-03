// SiteCheck — ESP8266 server health checker, controlled entirely over ntfy.
//
// Board:    any ESP8266 (LittleFS-capable flash, 1MB+)
// Core:     ESP8266 Arduino core 3.x
// Requires: ArduinoJson (v6 or v7), ESPping (dvarrel) if USE_PING is 1
//
// Flash layout: pick any option with a LittleFS partition (e.g. "4MB (FS:2MB)"
// or "1M (FS:64KB)"); the sketch stores two small JSON files.

#include <ESP8266WiFi.h>
#include <time.h>

#include "common.h"

// ------------------------------------------------------------- globals -----
Host     hosts[MAX_HOSTS];
uint8_t  hostCount     = 0;
bool     hostsDirty    = false;
uint32_t lastCmdTime   = 0;
char     lastCmdId[24] = "";
int      lastDigestDay = -1;

static uint32_t lastCheckMs = 0;
static uint32_t lastPollMs  = 0;
static bool     timeSynced  = false;

// -------------------------------------------------------------- helpers ----
const char *stateName(uint8_t state) {
  switch (state) {
    case STATE_UP:   return "up";
    case STATE_DOWN: return "down";
    default:         return "unknown";
  }
}

const char *typeName(uint8_t type) {
  switch (type) {
    case TYPE_HTTP: return "http";
    case TYPE_PING: return "ping";
    default:        return "https";
  }
}

bool timeIsValid() {
  // Anything past 2021 means NTP has answered at least once.
  return time(nullptr) > 1600000000UL;
}

// Strip characters that would break JSON storage or ntfy headers.
void sanitizeText(String &s) {
  s.trim();
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') continue;
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) continue;  // ASCII only
    out += c;
  }
  out.trim();
  s = out;
}

// ----------------------------------------------------------------- wifi ----
static void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("[wifi] connecting"));
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - start) < 30000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[wifi] connected, ip="));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[wifi] connect failed, will retry"));
  }
}

static bool wifiEnsure() {
  if (WiFi.status() == WL_CONNECTED) return true;
  wifiConnect();
  return WiFi.status() == WL_CONNECTED;
}

// ------------------------------------------------------------------ ntp ----
static void ntpSync() {
  configTime(TZ_STRING, NTP_SERVER_1, NTP_SERVER_2);

  Serial.print(F("[ntp] syncing"));
  uint32_t start = millis();
  while (!timeIsValid() && (uint32_t)(millis() - start) < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (timeIsValid()) {
    timeSynced = true;
    time_t now = time(nullptr);
    Serial.print(F("[ntp] "));
    Serial.print(ctime(&now));

    // First ever boot: start listening from now so old commands sitting in the
    // topic's retention window are not replayed.
    if (lastCmdTime == 0) {
      lastCmdTime = (uint32_t)now;
      stateSave();
    }
  } else {
    Serial.println(F("[ntp] no time yet"));
  }
}

// ------------------------------------------------------- time-of-day jobs --
static void runTimedJobs() {
  if (!timeIsValid()) return;

  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);

  // Daily digest.
  if (lt.tm_hour == DIGEST_HOUR && lt.tm_min >= DIGEST_MINUTE &&
      lastDigestDay != lt.tm_yday) {
    lastDigestDay = lt.tm_yday;
    stateSave();
    sendDigest();
    hostsSave();  // daily flush of state/last_change alongside the digest
  }

  // Scheduled self-reboot. The uptime guard keeps a reboot inside the target
  // minute from immediately triggering another one.
  if (lt.tm_hour == REBOOT_HOUR && lt.tm_min == REBOOT_MINUTE &&
      millis() > (15UL * 60UL * 1000UL)) {
    Serial.println(F("[sys] scheduled reboot"));
    hostsSaveIfDirty();
    stateSave();
    delay(200);
    ESP.restart();
  }
}

// ---------------------------------------------------------------- setup ----
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println(F("\n[sys] SiteCheck starting"));
  Serial.print(F("[sys] reset reason: "));
  Serial.println(ESP.getResetReason());

  storageBegin();
  stateLoad();
  hostsLoad();
  Serial.printf("[sys] %u host(s) loaded\n", hostCount);

  wifiConnect();
  ntpSync();

#if NOTIFY_ON_BOOT
  if (WiFi.status() == WL_CONNECTED) {
    String msg = F("SiteCheck booted, ");
    msg += hostCount;
    msg += F(" host(s) monitored (reset: ");
    msg += ESP.getResetReason();
    msg += ')';
    ntfyPublish(msg, "SiteCheck booted", "arrows_counterclockwise", 2);
  }
#endif

  // Stagger the first check and the first poll so they never overlap.
  lastCheckMs = millis() - (CHECK_INTERVAL_MS - FIRST_CHECK_DELAY_MS);
  lastPollMs  = millis();
}

// ----------------------------------------------------------------- loop ----
void loop() {
  if (!wifiEnsure()) {
    delay(2000);
    return;
  }

  if (!timeSynced) ntpSync();

  // Sequential by construction: only one of these ever runs per iteration and
  // each one fully closes its socket before returning.
  if ((uint32_t)(millis() - lastPollMs) >= POLL_INTERVAL_MS) {
    lastPollMs = millis();
    ntfyPollCommands();
  } else if ((uint32_t)(millis() - lastCheckMs) >= CHECK_INTERVAL_MS) {
    lastCheckMs = millis();
    runCheckLoop();
  } else {
    runTimedJobs();
    delay(100);
  }
}
