// SiteCheck — ESP32-S3 server health checker, controlled entirely over ntfy.
//
// Board:    ESP32-S3 N16R8 (16 MB QIO flash, 8 MB OPI PSRAM), e.g. DevKitC-1
// Core:     esp32 Arduino core 3.x
// Requires: ArduinoJson (v6 or v7), ESPping (dvarrel) if USE_PING is 1
//
// Flash layout comes from partitions.csv in this folder (picked up automatically
// by the esp32 core): two 6.25 MB app slots and a 3.4 MB LittleFS partition.
//
// Nothing user-specific is compiled in. On first boot the device comes up as
// an open access point running a captive portal; Wi-Fi credentials, the ntfy
// topic, timezone and schedule are entered there and stored in /config.json.

#include <WiFi.h>
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
static uint32_t lastRetryMs = 0;
static bool     timeSynced  = false;
static String   s_lastIp;   // last address announced, to spot DHCP changes

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

// ESP.getResetReason() is ESP8266-only; this is the ESP32 equivalent.
static const char *resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software restart";
    case ESP_RST_PANIC:     return "Exception/panic";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO reset";
    case ESP_RST_USB:       return "USB reset";
    case ESP_RST_JTAG:      return "JTAG reset";
    default:                return "Unknown";
  }
}

bool timeIsValid() {
  // Anything past 2021 means NTP has answered at least once.
  return time(nullptr) > 1600000000UL;
}

// delay() that keeps the settings page answering.
//
// Only call this when no ntfy or check socket is open. Servicing an inbound web
// request allocates a socket of its own, and doing that while a ~16 KB TLS
// session is live is exactly the concurrent-socket case the whole design
// avoids. Call sites are between operations, never inside one.
void serviceDelay(uint32_t ms) {
  uint32_t start = millis();
  do {
    webconfigTick();
    delay(1);
    yield();
  } while ((uint32_t)(millis() - start) < ms);
}

// Where to reach this device's settings page, for ntfy messages.
String settingsUrl() {
  String u = F("http://");
  u += webconfigIp();
  if (g_settings.hostname.length()) {
    u += F(" (http://");
    u += g_settings.hostname;
    u += F(".local)");
  }
  return u;
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
static bool wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.persistent(false);
  // On ESP32 the hostname only takes effect if set before the interface starts.
  WiFi.setHostname(g_settings.hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPass.c_str());

  Serial.printf("[wifi] connecting to \"%s\"", g_settings.wifiSsid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (uint32_t)(millis() - start) < WIFI_CONNECT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[wifi] connected, ip="));
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println(F("[wifi] connect failed"));
  return false;
}

// Bring the portal up next to a still-retrying station interface. WIFI_AP_STA
// is what lets the device keep trying the real network in the background while
// the settings page stays reachable — a wrong password is recoverable without
// touching a USB cable.
static void startApFallback() {
  Serial.println(F("[wifi] falling back to setup AP, still retrying in background"));
  WiFi.mode(WIFI_AP_STA);
  webconfigBeginAP();
  lastRetryMs = millis();
}

// Pressing BOOT (GPIO0) just after a reset forces the setup portal on an
// already-provisioned device. It has to be pressed AFTER releasing RST: GPIO0
// held low through the reset itself selects the serial bootloader instead.
static bool bootButtonPressed() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  Serial.println(F("[sys] press BOOT now to open the setup portal"));
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < BOOT_WINDOW_MS) {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) return true;
    delay(20);
  }
  return false;
}

// ------------------------------------------------------------------ ntp ----
static void ntpSync() {
  configTzTime(g_settings.tz.c_str(), NTP_SERVER_1, NTP_SERVER_2);

  Serial.print(F("[ntp] syncing"));
  uint32_t start = millis();
  while (!timeIsValid() && (uint32_t)(millis() - start) < 15000) {
    delay(250);
    Serial.print('.');
    webconfigTick();
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
  if (lt.tm_hour == g_settings.digestHour &&
      lt.tm_min >= g_settings.digestMinute && lastDigestDay != lt.tm_yday) {
    lastDigestDay = lt.tm_yday;
    stateSave();
    sendDigest();
    hostsSave();  // daily flush of state/last_change alongside the digest
  }

  // Scheduled self-reboot. The uptime guard keeps a reboot inside the target
  // minute from immediately triggering another one.
  if (lt.tm_hour == g_settings.rebootHour &&
      lt.tm_min == g_settings.rebootMinute &&
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
  Serial.println(F("\n[sys] SiteCheck v" SITECHECK_VERSION " starting"));
  Serial.print(F("[sys] reset reason: "));
  Serial.println(resetReasonStr());

  storageBegin();
  settingsLoad();
  stateLoad();
  hostsLoad();
  Serial.printf("[sys] %u host(s) loaded\n", hostCount);

  // Only worth waiting for on a provisioned device; otherwise setup is next.
  bool forced = g_settings.provisioned && bootButtonPressed();
  if (forced) Serial.println(F("[sys] BOOT pressed — forcing setup portal"));

  if (!g_settings.provisioned || forced) {
    // No usable config (or a forced recovery): the only useful thing this
    // device can do is tell you how to configure it. Deliberately open — there
    // is nothing to protect yet, or the person holds the board and could
    // reflash it anyway. Blocks until a config is saved, then restarts.
    webconfigRunSetupPortal();
  }

  if (wifiConnect()) {
    webconfigBeginSTA();
  } else {
    startApFallback();
  }

  ntpSync();

  // The AP fallback keeps retrying the real network in the background, and NTP
  // gives it up to 15 s to succeed. If it did, promote to plain station mode
  // before announcing — otherwise the boot message would carry the setup AP's
  // address and mDNS would never start.
  if (WiFi.status() == WL_CONNECTED && webconfigIsAP()) {
    Serial.print(F("[wifi] connected during startup, ip="));
    Serial.println(WiFi.localIP());
    webconfigStopAP();
    WiFi.mode(WIFI_STA);
    webconfigBeginSTA();
  }

  s_lastIp = webconfigIp();

#if NOTIFY_ON_BOOT
  if (WiFi.status() == WL_CONNECTED) {
    String msg = F("SiteCheck booted, ");
    msg += hostCount;
    msg += F(" host(s) monitored (reset: ");
    msg += resetReasonStr();
    msg += F(")\nSettings: ");
    msg += settingsUrl();
    msg += F("\nFirmware: v" SITECHECK_VERSION);
    ntfyPublish(msg, "SiteCheck booted", "arrows_counterclockwise", 2);
  }
#endif

  // Stagger the first check and the first poll so they never overlap.
  lastCheckMs = millis() - (checkIntervalMs() - FIRST_CHECK_DELAY_MS);
  lastPollMs  = millis();
}

// ----------------------------------------------------------------- loop ----
void loop() {
  webconfigTick();

  if (WiFi.status() != WL_CONNECTED) {
    // Offline: keep the portal answering and retry the network periodically.
    // Checks and polls are pointless here, so nothing else runs.
    if (!webconfigIsAP()) startApFallback();
    if ((uint32_t)(millis() - lastRetryMs) >= WIFI_RETRY_MS) {
      lastRetryMs = millis();
      Serial.println(F("[wifi] retrying"));
      WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPass.c_str());
    }
    delay(5);
    return;
  }

  // Station came back while the fallback AP was up: drop the AP and move the
  // settings page to the LAN address.
  if (webconfigIsAP()) {
    Serial.print(F("[wifi] reconnected, ip="));
    Serial.println(WiFi.localIP());
    webconfigStopAP();
    WiFi.mode(WIFI_STA);
    webconfigBeginSTA();  // also restarts mDNS on the new address
  }

  if (!timeSynced) ntpSync();

  // DHCP can hand out a different address after a reconnect or a lease
  // renewal, which would leave the address in the boot message stale and the
  // settings page unreachable. Announce the new one.
  String ip = webconfigIp();
  if (ip != s_lastIp && ip != "0.0.0.0") {
    Serial.printf("[wifi] address changed %s -> %s\n", s_lastIp.c_str(),
                  ip.c_str());
    s_lastIp = ip;
    webconfigBeginSTA();  // re-advertise mDNS on the new address
    String msg = F("Back online\nSettings: ");
    msg += settingsUrl();
    ntfyPublish(msg, "SiteCheck online", "satellite", 2);
  }

  // Sequential by construction: only one network operation runs per iteration
  // and each fully closes its socket before returning. Polls take priority so
  // commands are still answered promptly during a long check pass.
  if ((uint32_t)(millis() - lastPollMs) >= pollIntervalMs()) {
    lastPollMs = millis();
    ntfyPollCommands();
  } else if (checkPassActive()) {
    checkTick();  // one host, then back here to service the web server
  } else if ((uint32_t)(millis() - lastCheckMs) >= checkIntervalMs()) {
    lastCheckMs = millis();
    checkStartPass();
  } else {
    runTimedJobs();
    serviceDelay(5);
  }
}
