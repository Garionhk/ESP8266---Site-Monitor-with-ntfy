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
int  findHost(const char *name);
bool parseTarget(const String &input, uint8_t &type, char *out, size_t outLen);

// -------------------------------------------------------------- checks -----
void runCheckLoop();
uint8_t checkHost(const Host &h);

// -------------------------------------------------------------- report -----
String buildStatusReport();
void   sendDigest();

// --------------------------------------------------------------- utils -----
const char *stateName(uint8_t state);
const char *typeName(uint8_t type);
bool  timeIsValid();
void  sanitizeText(String &s);
