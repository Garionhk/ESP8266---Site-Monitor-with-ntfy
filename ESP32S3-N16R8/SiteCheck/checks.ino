// checks.ino — the 30-minute check loop.
//
// Kept from the ESP8266 build, where it was a hard stability rule: hosts are checked strictly sequentially and
// every socket (especially the TLS ones) is fully closed before the next host
// is touched.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <memory>

#if USE_PING
#include <ESPping.h>
#endif

#include "common.h"

// ------------------------------------------------------------ http/https ---
static uint8_t checkWeb(const Host &h) {
  std::unique_ptr<WiFiClient> client;
  if (h.type == TYPE_HTTPS) {
    WiFiClientSecure *sec = new WiFiClientSecure();
    sec->setInsecure();  // no certificate validation, by design
    client.reset(sec);
  } else {
    client.reset(new WiFiClient());
  }
  client->setTimeout(HTTP_TIMEOUT_MS);

  // HEAD first; some servers reject it, in which case retry with GET.
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    HTTPClient http;
    if (!http.begin(*client, h.target)) {
      Serial.printf("[check] %s: bad url\n", h.name);
      return STATE_DOWN;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    http.setUserAgent(F("SiteCheck-ESP32S3"));
    // 3xx counts as up, so redirects are reported, not followed.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    int code = (attempt == 0) ? http.sendRequest("HEAD") : http.GET();
    http.end();

    Serial.printf("[check] %s %s -> %d\n", attempt == 0 ? "HEAD" : "GET",
                  h.target, code);

    if (code >= 200 && code < 400) return STATE_UP;
    if (code >= 400) {
      // A real HTTP error: retry once with GET, then call it down.
      if (attempt == 0) {
        delay(200);
        continue;
      }
      return STATE_DOWN;
    }
    // code <= 0 is a connection/TLS failure — a GET retry rarely helps but is
    // cheap and covers servers that drop HEAD without answering.
    if (attempt == 0) {
      delay(200);
      continue;
    }
    return STATE_DOWN;
  }
  return STATE_DOWN;
}

// ------------------------------------------------------------------ ping ---
static uint8_t checkPing(const Host &h) {
#if USE_PING
  IPAddress ip;
  if (!ip.fromString(h.target)) {
    Serial.printf("[check] %s: bad ip\n", h.name);
    return STATE_DOWN;
  }
  bool ok = Ping.ping(ip, PING_COUNT);
  Serial.printf("[check] PING %s -> %s\n", h.target, ok ? "ok" : "fail");
  return ok ? STATE_UP : STATE_DOWN;
#else
  Serial.printf("[check] PING %s skipped (USE_PING=0)\n", h.target);
  return STATE_UNKNOWN;
#endif
}

uint8_t checkHost(const Host &h) {
  return (h.type == TYPE_PING) ? checkPing(h) : checkWeb(h);
}

// ------------------------------------------------------------- the pass ----
// A pass used to be one blocking for-loop, which froze the web UI for its whole
// duration — up to ~8 minutes with 30 slow hosts. It is now cooperative: each
// checkTick() handles exactly one host and returns, so loop() can service the
// web server in between. Still strictly one socket at a time; each host's
// client is opened and destroyed inside its own tick.

static bool    passActive  = false;
static uint8_t passIndex   = 0;
static bool    passChanged = false;

bool checkPassActive() { return passActive; }

void checkStartPass() {
  if (hostCount == 0) {
    Serial.println(F("[check] no hosts configured"));
    return;
  }
  passActive  = true;
  passIndex   = 0;
  passChanged = false;
  Serial.printf("[check] starting pass, heap=%u\n", (unsigned)ESP.getFreeHeap());
}

static void passFinish() {
  passActive = false;
  Serial.printf("[check] pass done, heap=%u\n", (unsigned)ESP.getFreeHeap());
  if (passChanged) {
    hostsDirty = true;
    hostsSave();  // persist on transitions only
  }
}

bool checkTick() {
  if (!passActive) return false;

  // A REMOVE arriving mid-pass shrinks the list under us; the bound is
  // re-tested every tick rather than cached for that reason.
  if (passIndex >= hostCount) {
    passFinish();
    return false;
  }

  Host &h = hosts[passIndex++];
  uint8_t result = checkHost(h);
  yield();

  if (result != STATE_UNKNOWN && result != h.state) {
    uint8_t previous = h.state;
    h.state      = result;
    h.lastChange = (uint32_t)time(nullptr);
    passChanged  = true;

    String body = String(h.name) + " (" + h.target + ") is " +
                  (result == STATE_UP ? "UP" : "DOWN");

    if (previous == STATE_UNKNOWN) {
      // First result after a boot or an ADD: report only the bad news.
      if (result == STATE_DOWN) {
        ntfyPublish(body, (String("DOWN: ") + h.name).c_str(),
                    "rotating_light", 4);
      }
    } else if (result == STATE_DOWN) {
      ntfyPublish(body, (String("DOWN: ") + h.name).c_str(),
                  "rotating_light", 4);
    } else {
      ntfyPublish(body, (String("Recovered: ") + h.name).c_str(),
                  "white_check_mark", 3);
    }
  }
  // No change: stay quiet.

  // Safe to pump the web server here: this host's client was destroyed when
  // checkHost() returned, so no socket is open.
  serviceDelay(INTER_CHECK_DELAY_MS);

  if (passIndex >= hostCount) passFinish();
  return passActive;
}
