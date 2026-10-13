// common.h — shared types, globals and prototypes for every .ino tab.
#pragma once

#include <Arduino.h>
#include <time.h>

#include "config.h"

#define HOST_NAME_LEN    24    // including terminating NUL
#define HOST_TARGET_LEN  96

enum HostState : uint8_t {
  STATE_UNKNOWN = 0,
  STATE_UP      = 1,
  STATE_DOWN    = 2,
};

enum HostType : uint8_t {
  TYPE_HTTPS = 0,
  TYPE_HTTP  = 1,
  TYPE_PING  = 2,
};

struct Host {
  char     name[HOST_NAME_LEN];
  char     target[HOST_TARGET_LEN];
  uint8_t  type;        // HostType
  uint8_t  state;       // HostState
  uint32_t lastChange;  // unix time of the last state transition
};

// ------------------------------------------------------------ settings -----
// Everything user-specific, entered in the web UI and stored in /config.json.
// `provisioned` is the sentinel: false means no usable config, so the device
// comes up as a setup access point instead of trying to monitor anything.
struct Settings {
  String   wifiSsid;
  String   wifiPass;
  String   hostname;
  String   ntfyBase;
  String   ntfyTopic;
  String   ntfyToken;
  // Prefix every ntfy command must start with. Required for ADD / REMOVE /
  // FACTORYRESET; when empty (a config from before this existed) read-only
  // commands still work ungated.
  String   cmdSecret;
  // Login for the settings page. Deliberately separate from cmdSecret: that one
  // travels through the ntfy topic, and it should not also unlock the config.
  String   adminPass;
  String   tz;
  uint16_t checkIntervalMin;
  uint16_t pollIntervalSec;
  uint8_t  digestHour;
  uint8_t  digestMinute;
  uint8_t  rebootHour;
  uint8_t  rebootMinute;
  bool     provisioned;
};

extern Settings g_settings;

// Username paired with Settings::adminPass on the settings page.
#define ADMIN_USER "admin"

void settingsLoad();
bool settingsSave();
void settingsClear();
// Deletes /config.json but leaves g_settings in RAM, so a FACTORYRESET reply
// can still be published before the restart. Hosts and poll state are kept.
void settingsErase();

inline uint32_t checkIntervalMs() {
  return (uint32_t)g_settings.checkIntervalMin * 60UL * 1000UL;
}
inline uint32_t pollIntervalMs() {
  return (uint32_t)g_settings.pollIntervalSec * 1000UL;
}

// ----------------------------------------------------------- web config -----
// Setup portal: no login, because it is only reached with nothing to protect
// yet (unprovisioned) or by someone holding the board (FLASH button at boot).
void   webconfigRunSetupPortal();  // blocks until a config is saved (reboots)
void   webconfigBeginSTA();        // settings server on the LAN IP
void   webconfigBeginAP();         // SoftAP + captive portal, non-blocking
void   webconfigStopAP();          // drop the AP once STA comes back
void   webconfigTick();            // call from loop()
bool   webconfigIsAP();
String webconfigIp();
String webconfigApSsid();

// Commands buffered during a poll, replayed once the socket is closed.
// Lives here because the Arduino builder hoists function prototypes above the
// body of each .ino file.
#define MAX_PENDING_CMDS 8

struct PendingCmd {
  String   body;
  uint32_t time;
  char     id[24];
};

// ------------------------------------------------------------- globals -----
extern Host     hosts[MAX_HOSTS];
extern uint8_t  hostCount;
extern bool     hostsDirty;      // in-memory list differs from hosts.json
extern uint32_t lastCmdTime;     // unix time of the last processed command
extern char     lastCmdId[24];   // ntfy message id of the last processed command
extern int      lastDigestDay;   // tm_yday the digest was last sent for

// ------------------------------------------------------------- storage -----
void storageBegin();
bool hostsLoad();
bool hostsSave();
void hostsSaveIfDirty();
bool stateLoad();
bool stateSave();

// ---------------------------------------------------------------- ntfy -----
bool ntfyPublish(const String &body, const char *title, const char *tags,
                 int priority);
void ntfyPollCommands();

// ------------------------------------------------------------ commands -----
void handleCommand(const String &body, uint32_t msgTime);
// True after a FACTORYRESET has wiped the config. The poll loop saves its
// cursor first — so the command is not replayed — then restarts into setup.
bool cmdRestartPending();
int  findHost(const char *name);
bool parseTarget(const String &input, uint8_t &type, char *out, size_t outLen);

// -------------------------------------------------------------- checks -----
// The pass is cooperative: one host per checkTick() call, so the web server
// gets serviced between hosts instead of being frozen for the whole pass.
void    checkStartPass();
bool    checkTick();
bool    checkPassActive();
uint8_t checkHost(const Host &h);

// -------------------------------------------------------------- report -----
String buildStatusReport();
void   sendDigest();

// --------------------------------------------------------------- utils -----
// delay() that keeps the web server answering. Only safe to call when no
// ntfy/check socket is open — see the note in serviceDelay().
void   serviceDelay(uint32_t ms);
String settingsUrl();
const char *stateName(uint8_t state);
const char *typeName(uint8_t type);
bool  timeIsValid();
void  sanitizeText(String &s);
