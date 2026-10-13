// ntfy.ino — the device's only interface: publish to the report topic, poll
// the command topic.
//
// Every function here opens exactly one connection and closes it before
// returning. Command messages are buffered and only acted on after the poll
// connection is closed, so a command reply never overlaps the poll socket.

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <memory>

#include "common.h"

static bool ntfyUsesTls() {
  return g_settings.ntfyBase.startsWith("https://");
}

// Caller owns the returned client.
static WiFiClient *ntfyNewClient() {
  if (ntfyUsesTls()) {
    WiFiClientSecure *sec = new WiFiClientSecure();
    sec->setInsecure();  // no cert store, no pinning — by design
    return sec;
  }
  return new WiFiClient();
}

// --------------------------------------------------------------- publish ---
static bool publishOnce(const String &body, const char *title,
                        const char *tags, int priority) {
  std::unique_ptr<WiFiClient> client(ntfyNewClient());
  HTTPClient http;

  String url = g_settings.ntfyBase + "/" + g_settings.ntfyTopic;
  if (!http.begin(*client, url)) {
    Serial.println(F("[ntfy] begin() failed"));
    return false;
  }

  // The device tag goes on everything we send: it is what stops the poll from
  // reading our own reports back as commands on the shared topic.
  String tagList = (tags && tags[0]) ? String(tags) + "," + NTFY_DEVICE_TAG
                                     : String(NTFY_DEVICE_TAG);

  http.setTimeout(NTFY_TIMEOUT_MS);
  http.setReuse(false);
  http.setUserAgent(F("SiteCheck-ESP8266"));
  http.addHeader(F("Content-Type"), F("text/plain; charset=utf-8"));
  if (title && title[0]) http.addHeader(F("Title"), title);
  http.addHeader(F("Tags"), tagList);
  if (priority > 0)      http.addHeader(F("Priority"), String(priority));
  if (g_settings.ntfyToken.length()) {
    http.addHeader(F("Authorization"), String(F("Bearer ")) + g_settings.ntfyToken);
  }

  int code = http.POST((uint8_t *)body.c_str(), body.length());
  http.end();

  if (code != HTTP_CODE_OK) {
    Serial.printf("[ntfy] publish failed, code=%d\n", code);
    return false;
  }
  return true;
}

bool ntfyPublish(const String &body, const char *title, const char *tags,
                 int priority) {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (publishOnce(body, title, tags, priority)) return true;

  // Both clients are destroyed by now, so pumping the web server here cannot
  // overlap sockets.
  serviceDelay(1000);  // one retry — transient TLS failures are common
  return publishOnce(body, title, tags, priority);
}

// ------------------------------------------------------------------ poll ---
// Pulls new command messages into `out`. Returns the HTTP status code.
// `cursorId`/`cursorTime` come back pointing at the last message examined —
// including ones that were skipped — so the next poll starts past them.
static int fetchCommands(const String &since, PendingCmd *out, uint8_t &count,
                         char *cursorId, uint32_t &cursorTime) {
  std::unique_ptr<WiFiClient> client(ntfyNewClient());
  HTTPClient http;

  String url = g_settings.ntfyBase + "/" + g_settings.ntfyTopic +
               "/json?poll=1&since=" + since;
  if (!http.begin(*client, url)) return -1;

  http.setTimeout(NTFY_TIMEOUT_MS);
  http.setReuse(false);
  http.setUserAgent(F("SiteCheck-ESP8266"));
  if (g_settings.ntfyToken.length()) {
    http.addHeader(F("Authorization"), String(F("Bearer ")) + g_settings.ntfyToken);
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return code;
  }

  // The response is newline-delimited JSON, oldest message first.
  //
  // getStreamPtr() returns nullptr once the connection has closed, which is
  // the normal case for a poll that matched no new messages: the server sends
  // an empty body and hangs up before we get here. Dereferencing it was a
  // null-store crash (exception 29) on every quiet poll.
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    http.end();
    return code;
  }
  stream->setTimeout(2000);

  uint32_t start = millis();
  while ((http.connected() || stream->available()) &&
         (uint32_t)(millis() - start) < NTFY_TIMEOUT_MS &&
         count < MAX_PENDING_CMDS) {
    if (!stream->available()) {
      delay(10);
      continue;
    }

    String line = stream->readStringUntil('\n');
    line.trim();
    yield();
    if (line.length() < 2) continue;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif
    if (deserializeJson(doc, line)) continue;

    const char *event = doc["event"] | "";
    if (strcmp(event, "message") != 0) continue;  // open/keepalive/poll_request

    const char *id  = doc["id"]      | "";
    uint32_t    ts  = doc["time"]    | 0UL;
    const char *msg = doc["message"] | "";
    if (!id[0]) continue;

    // Messages arrive oldest-first, so the last one examined is always the
    // furthest along — safe to use as the next cursor whether or not we act
    // on it.
    strlcpy(cursorId, id, 24);
    cursorTime = ts;

    if (!msg[0]) continue;

    // Our own message on the shared topic? Everything we publish carries the
    // device tag, and reading it back as a command would be a feedback loop.
    // Kept inline: a helper taking a JsonDocument would need its prototype
    // hoisted into the main sketch, which does not include ArduinoJson.
    bool own = false;
    JsonArrayConst msgTags = doc["tags"];
    for (JsonVariantConst t : msgTags) {
      const char *s = t.as<const char *>();
      if (s && strcmp(s, NTFY_DEVICE_TAG) == 0) {
        own = true;
        break;
      }
    }
    if (own) continue;

    // Skip anything already handled: `since` is inclusive on the server side,
    // and the id guards against two commands sharing one timestamp.
    if (ts < lastCmdTime) continue;
    if (lastCmdId[0] && !strcmp(id, lastCmdId)) continue;

    out[count].body = msg;
    out[count].time = ts;
    strlcpy(out[count].id, id, sizeof(out[count].id));
    count++;
  }

  http.end();
  return code;
}

void ntfyPollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  PendingCmd pending[MAX_PENDING_CMDS];
  uint8_t    count = 0;
  int        code  = -1;
  char       cursorId[24] = "";
  uint32_t   cursorTime   = 0;
  uint32_t   started      = millis();

  // ntfy accepts a message id as `since`, which is exclusive and therefore the
  // cleanest cursor. Older servers reject it, so fall back to a timestamp.
  if (lastCmdId[0]) {
    code = fetchCommands(lastCmdId, pending, count, cursorId, cursorTime);
  }
  if (code != HTTP_CODE_OK) {
    count = 0;
    cursorId[0] = '\0';
    // Without a cursor (NTP never came up), ask only for one poll window's
    // worth rather than replaying everything still in the topic's retention.
    String since = lastCmdTime
                       ? String(lastCmdTime)
                       : String(g_settings.pollIntervalSec / 60UL + 1UL) + "m";
    code = fetchCommands(since, pending, count, cursorId, cursorTime);
  }

  if (code != HTTP_CODE_OK) {
    Serial.printf("[ntfy] poll failed, code=%d (%lums)\n", code,
                  (unsigned long)(millis() - started));
    return;
  }

  // Timing and heap are logged every poll: this is the most frequent network
  // operation, so it is where a leak or a slow server shows up first.
  Serial.printf("[ntfy] poll ok in %lums, %u cmd, heap=%u\n",
                (unsigned long)(millis() - started), count, ESP.getFreeHeap());
  if (count) {
    Serial.printf("[ntfy] %u command(s) received\n", count);

    // Connection is closed now — safe to run commands and publish replies.
    // The cursor is advanced per command so a crash mid-batch cannot replay
    // the ones already applied.
    for (uint8_t i = 0; i < count; i++) {
      handleCommand(pending[i].body, pending[i].time);
      if (pending[i].time > lastCmdTime) lastCmdTime = pending[i].time;
      strlcpy(lastCmdId, pending[i].id, sizeof(lastCmdId));
      stateSave();
      yield();

      // FACTORYRESET: its reply is out and the cursor is past it, so it will
      // not be replayed once the device is set up again.
      if (cmdRestartPending()) {
        Serial.println(F("[sys] factory reset — restarting into setup"));
        hostsSaveIfDirty();
        delay(500);
        ESP.restart();
      }
    }
  }

  // Move the cursor past everything we looked at, including our own reports.
  // Without this the poll would re-download a growing tail of alerts all day.
  if (cursorId[0] && strcmp(cursorId, lastCmdId) != 0) {
    strlcpy(lastCmdId, cursorId, sizeof(lastCmdId));
    if (cursorTime > lastCmdTime) lastCmdTime = cursorTime;
    stateSave();
  }
}
