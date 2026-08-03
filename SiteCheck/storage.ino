// storage.ino — hosts.json / state.json persistence in LittleFS.
//
// Flash wear is kept down by only writing hosts.json when something actually
// changed: on ADD/REMOVE, on a state transition, and once a day with the
// digest.

#include <LittleFS.h>
#include <ArduinoJson.h>

#include "common.h"

static const char *HOSTS_PATH     = "/hosts.json";
static const char *HOSTS_TMP_PATH = "/hosts.tmp";
static const char *STATE_PATH     = "/state.json";

void storageBegin() {
  if (!LittleFS.begin()) {
    Serial.println(F("[fs] mount failed, formatting"));
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println(F("[fs] mount failed again, running without persistence"));
      return;
    }
  }

  FSInfo info;
  if (LittleFS.info(info)) {
    Serial.printf("[fs] mounted, %u/%u bytes used\n",
                  (unsigned)info.usedBytes, (unsigned)info.totalBytes);
  }
}

// ------------------------------------------------------------ hosts.json ---
bool hostsLoad() {
  hostCount = 0;

  File f = LittleFS.open(HOSTS_PATH, "r");
  if (!f) {
    Serial.println(F("[fs] no hosts.json yet"));
    return false;
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(8192);
#endif
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print(F("[fs] hosts.json parse error: "));
    Serial.println(err.c_str());
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject o : arr) {
    if (hostCount >= MAX_HOSTS) break;

    Host &h = hosts[hostCount];
    memset(&h, 0, sizeof(h));

    const char *name   = o["name"]   | "";
    const char *target = o["target"] | "";
    if (!name[0] || !target[0]) continue;

    strlcpy(h.name, name, sizeof(h.name));
    strlcpy(h.target, target, sizeof(h.target));

    const char *type = o["type"] | "https";
    if (!strcmp(type, "ping"))      h.type = TYPE_PING;
    else if (!strcmp(type, "http")) h.type = TYPE_HTTP;
    else                            h.type = TYPE_HTTPS;

    const char *state = o["state"] | "unknown";
    if (!strcmp(state, "up"))        h.state = STATE_UP;
    else if (!strcmp(state, "down")) h.state = STATE_DOWN;
    else                             h.state = STATE_UNKNOWN;

    h.lastChange = o["last_change"] | 0UL;
    hostCount++;
  }

  hostsDirty = false;
  return true;
}

bool hostsSave() {
  // Names and targets are sanitized on ADD, so plain printing is safe here and
  // avoids building a multi-kilobyte JSON document in RAM.
  File f = LittleFS.open(HOSTS_TMP_PATH, "w");
  if (!f) {
    Serial.println(F("[fs] cannot open hosts.tmp for write"));
    return false;
  }

  f.print(F("[\n"));
  for (uint8_t i = 0; i < hostCount; i++) {
    const Host &h = hosts[i];
    f.printf("  {\"name\":\"%s\",\"target\":\"%s\",\"type\":\"%s\","
             "\"state\":\"%s\",\"last_change\":%lu}%s\n",
             h.name, h.target, typeName(h.type), stateName(h.state),
             (unsigned long)h.lastChange, (i + 1 < hostCount) ? "," : "");
    yield();
  }
  f.print(F("]\n"));
  f.close();

  LittleFS.remove(HOSTS_PATH);
  if (!LittleFS.rename(HOSTS_TMP_PATH, HOSTS_PATH)) {
    Serial.println(F("[fs] hosts.json rename failed"));
    return false;
  }

  hostsDirty = false;
  Serial.printf("[fs] hosts.json written (%u hosts)\n", hostCount);
  return true;
}

void hostsSaveIfDirty() {
  if (hostsDirty) hostsSave();
}

// ------------------------------------------------------------ state.json ---
bool stateLoad() {
  File f = LittleFS.open(STATE_PATH, "r");
  if (!f) return false;

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<256> doc;
#endif
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  lastCmdTime   = doc["cmd_time"]   | 0UL;
  lastDigestDay = doc["digest_day"] | -1;
  strlcpy(lastCmdId, doc["cmd_id"] | "", sizeof(lastCmdId));

  // The cursor is a message id from one specific topic. If the configured
  // topic changed since the last boot it is meaningless — and asking the new
  // topic for `since=<foreign id>` would give unpredictable results — so start
  // clean and let the NTP sync re-anchor the cursor to "now".
  const char *savedTopic = doc["topic"] | "";
  if (strcmp(savedTopic, NTFY_TOPIC) != 0) {
    Serial.printf("[fs] topic changed (%s -> %s), resetting command cursor\n",
                  savedTopic[0] ? savedTopic : "(none)", NTFY_TOPIC);
    lastCmdId[0] = '\0';
    lastCmdTime  = 0;
  }

  Serial.printf("[fs] state: cmd_time=%lu cmd_id=%s digest_day=%d\n",
                (unsigned long)lastCmdTime, lastCmdId, lastDigestDay);
  return true;
}

bool stateSave() {
  File f = LittleFS.open(STATE_PATH, "w");
  if (!f) return false;

  f.printf("{\"cmd_time\":%lu,\"cmd_id\":\"%s\",\"digest_day\":%d,"
           "\"topic\":\"%s\"}\n",
           (unsigned long)lastCmdTime, lastCmdId, lastDigestDay, NTFY_TOPIC);
  f.close();
  return true;
}
