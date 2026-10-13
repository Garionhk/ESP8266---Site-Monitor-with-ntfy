// webconfig.ino — setup portal and settings server.
//
// Two modes, same routes:
//   AP  — SoftAP + DNS captive portal on 192.168.4.1, used when the device is
//         unprovisioned or the configured network did not come up
//   STA — settings page on the LAN IP (and http://<hostname>.local via mDNS)
//
// WebServer, DNSServer and ESPmDNS all ship with the esp32 core, so this adds
// no third-party dependency. The page is streamed in chunks rather than built
// as one String — inherited from the ESP8266 build, and still cheap here.

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

#include "common.h"

static WebServer        server(80);
static DNSServer        dns;
static bool             apMode    = false;
static bool             serverUp  = false;
static bool             mdnsUp    = false;
static bool             saveDone  = false;  // set by /save, ends the portal loop
// Cleared only by the setup portal. The LAN page and the Wi-Fi fallback AP
// both require the admin login.
static bool             authNeeded = true;

// Working copy the form renders from, so a rejected POST can redisplay what
// the user typed without touching the live settings.
static Settings s_form;

static const byte DNS_PORT = 53;

// A short list covering most users; anything else goes in the custom field.
static const char *TZ_CHOICES[] = {
  "PST8PDT,M3.2.0,M11.1.0",   // America/Vancouver, Los_Angeles
  "MST7MDT,M3.2.0,M11.1.0",   // America/Denver
  "MST7",                     // America/Phoenix
  "CST6CDT,M3.2.0,M11.1.0",   // America/Chicago
  "EST5EDT,M3.2.0,M11.1.0",   // America/Toronto, New_York
  "AST4ADT,M3.2.0,M11.1.0",   // America/Halifax
  "UTC0",                     // UTC
  "GMT0BST,M3.5.0/1,M10.5.0", // Europe/London
  "CET-1CEST,M3.5.0,M10.5.0/3",   // Europe/Paris, Berlin
  "EET-2EEST,M3.5.0/3,M10.5.0/4", // Europe/Helsinki
  "HKT-8",                    // Asia/Hong_Kong
  "JST-9",                    // Asia/Tokyo
  "AEST-10AEDT,M10.1.0,M4.1.0/3", // Australia/Sydney
};
static const uint8_t TZ_COUNT = sizeof(TZ_CHOICES) / sizeof(TZ_CHOICES[0]);

// ---------------------------------------------------------------- helpers ---
String webconfigApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);
  return String(buf);
}

bool webconfigIsAP() { return apMode; }

// The address to advertise. The station address wins whenever it exists: in
// WIFI_AP_STA the fallback AP and a working LAN connection are up at the same
// time, and 192.168.4.1 is only reachable by someone joined to the setup AP —
// useless in an ntfy message.
String webconfigIp() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (apMode) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

// Values are echoed into HTML attributes, so they must be escaped — an SSID
// containing a quote would otherwise break out of the attribute.
static String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&':  out += F("&amp;");  break;
      case '<':  out += F("&lt;");   break;
      case '>':  out += F("&gt;");   break;
      case '"':  out += F("&quot;"); break;
      case '\'': out += F("&#39;");  break;
      default:   out += c;
    }
  }
  return out;
}

// Send a dynamic value as a chunk.
//
// A zero-length chunk is the end-of-response marker in chunked encoding, so
// passing an empty String to sendContent() silently truncates the page. An unprovisioned device has empty
// fields, which ended the form at the SSID input. Every dynamic value goes
// through here; only non-empty F() literals are sent directly.
static void sendVal(const String &v) {
  if (v.length()) server.sendContent(v);
}

// ------------------------------------------------------------------ auth ----
// Returns false, having already answered, if the request must be rejected.
static bool authorized() {
  if (!authNeeded) return true;

  // A device configured before the admin password existed has none. Leaving
  // the page open would let anyone on the LAN (or in range of the fallback AP)
  // repoint it, so it stays shut until one is set — which needs the BOOT
  // button, i.e. physical access.
  if (!g_settings.adminPass.length()) {
    String p = F("<!doctype html><meta charset=utf-8>"
                 "<meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>Settings locked</title>"
                 "<body style=\"font:16px/1.5 system-ui,sans-serif;padding:2rem;"
                 "max-width:30rem;margin-inline:auto\"><h1>Settings locked</h1>"
                 "<p>No admin password is set yet. Press <b>RST</b> on the "
                 "board, release it, then immediately press and hold "
                 "<b>BOOT</b> for two seconds. It opens the <b>");
    p += htmlEscape(webconfigApSsid());
    p += F("</b> network, where you can set one.</p>");
    server.send(403, F("text/html; charset=utf-8"), p);
    return false;
  }

  if (server.authenticate(ADMIN_USER, g_settings.adminPass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

// ------------------------------------------------------------------ form ----
static void sendField(const __FlashStringHelper *label, const char *name,
                      const String &value, const __FlashStringHelper *hint,
                      const char *type) {
  server.sendContent(F("<label>"));
  server.sendContent(label);
  server.sendContent(F("<input type='"));
  server.sendContent(type);
  server.sendContent(F("' name='"));
  server.sendContent(name);
  server.sendContent(F("' value='"));
  sendVal(htmlEscape(value));
  server.sendContent(F("'></label>"));
  if (hint) {
    server.sendContent(F("<p class=h>"));
    server.sendContent(hint);
    server.sendContent(F("</p>"));
  }
}

static void sendNumField(const __FlashStringHelper *label, const char *name,
                         uint32_t value, uint32_t lo, uint32_t hi) {
  server.sendContent(F("<label>"));
  server.sendContent(label);
  server.sendContent(F("<input type='number' min='"));
  sendVal(String(lo));
  server.sendContent(F("' max='"));
  sendVal(String(hi));
  server.sendContent(F("' name='"));
  server.sendContent(name);
  server.sendContent(F("' value='"));
  sendVal(String(value));
  server.sendContent(F("'></label>"));
}

static void renderForm(const String &error) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), F(""));

  server.sendContent(F(
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SiteCheck setup</title><style>"
    ":root{color-scheme:light dark}"
    "body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:16px;"
    "max-width:34rem;margin-inline:auto}"
    "h1{font-size:1.3rem;margin:.2rem 0 1rem}"
    "h2{font-size:1rem;margin:1.6rem 0 .4rem;opacity:.75}"
    "label{display:block;margin:.6rem 0}"
    "input,select{width:100%;padding:.5rem;font-size:1rem;box-sizing:border-box;"
    "border:1px solid #8888;border-radius:6px;background:transparent;color:inherit}"
    "p.h{margin:.2rem 0 0;font-size:.8rem;opacity:.7}"
    ".row{display:flex;gap:.6rem}.row>*{flex:1}"
    ".err{background:#c0392b;color:#fff;padding:.6rem;border-radius:6px}"
    "button{width:100%;padding:.8rem;font-size:1rem;margin-top:1.4rem;"
    "border:0;border-radius:6px;background:#2d7ff9;color:#fff}"
    "</style><h1>SiteCheck setup</h1>"));

  if (error.length()) {
    server.sendContent(F("<p class=err>"));
    sendVal(htmlEscape(error));
    server.sendContent(F("</p>"));
  }

  server.sendContent(F("<form method=POST action='/save'>"
                       "<h2>Wi-Fi</h2>"
                       "<label>Network (2.4 GHz only)"
                       "<input name='ssid' list='nets' value='"));
  sendVal(htmlEscape(s_form.wifiSsid));
  server.sendContent(F("'></label><datalist id='nets'></datalist>"
                       "<label>Password"
                       "<input type='password' name='pass' value=''></label>"
                       "<p class=h>Leave blank to keep the stored password.</p>"));

  sendField(F("Device hostname"), "host", s_form.hostname,
            F("Also the mDNS name: http://&lt;hostname&gt;.local"), "text");

  server.sendContent(F("<h2>ntfy</h2>"));
  sendField(F("Server"), "base", s_form.ntfyBase,
            F("No trailing slash. Use http:// for a self-hosted server without TLS."),
            "text");
  sendField(F("Topic"), "topic", s_form.ntfyTopic,
            F("One topic both ways. On ntfy.sh the name is the only access "
              "control — use something long and random."), "text");
  // Secrets are never rendered back to the browser; blank keeps the stored one.
  server.sendContent(F(
    "<label>Access token<input type='password' name='token' value=''></label>"
    "<p class=h>Optional. Only for servers with authentication enabled. "
    "Leave blank to keep the stored token.</p>"
    "<label><input type='checkbox' name='tclear' style='width:auto'> "
    "clear stored token</label>"));

  server.sendContent(F(
    "<h2>Passphrases</h2>"
    "<label>Command passphrase<input type='password' name='secret' value=''></label>"
    "<p class=h>Prefix for every command sent over ntfy "
    "(<i>passphrase STATUS</i>). Required for ADD / REMOVE / FACTORYRESET. "
    "No spaces.</p>"
    "<label>Admin password (this page)"
    "<input type='password' name='admpass' value=''></label>"
    "<p class=h>Login is <b>" ADMIN_USER "</b>. Keep it different from the "
    "command passphrase. Blank keeps the stored ones.</p>"));

  server.sendContent(F("<h2>Time</h2><label>Timezone<select name='tz'>"));
  bool matched = false;
  for (uint8_t i = 0; i < TZ_COUNT; i++) {
    server.sendContent(F("<option value='"));
    server.sendContent(TZ_CHOICES[i]);
    server.sendContent(F("'"));
    if (s_form.tz == TZ_CHOICES[i]) {
      server.sendContent(F(" selected"));
      matched = true;
    }
    server.sendContent(F(">"));
    server.sendContent(TZ_CHOICES[i]);
    server.sendContent(F("</option>"));
  }
  // Keep an unrecognised stored value selectable instead of silently losing it.
  if (!matched && s_form.tz.length()) {
    server.sendContent(F("<option selected value='"));
    sendVal(htmlEscape(s_form.tz));
    server.sendContent(F("'>"));
    sendVal(htmlEscape(s_form.tz));
    server.sendContent(F("</option>"));
  }
  server.sendContent(F("</select></label>"));
  sendField(F("Custom POSIX TZ (overrides the list)"), "tzcustom", String(""),
            F("e.g. PST8PDT,M3.2.0,M11.1.0"), "text");

  server.sendContent(F("<h2>Schedule</h2><div class=row>"));
  sendNumField(F("Check every (min)"), "chk", s_form.checkIntervalMin, 1, 1440);
  sendNumField(F("Poll every (sec)"), "poll", s_form.pollIntervalSec, 30, 3600);
  server.sendContent(F("</div><div class=row>"));
  sendNumField(F("Digest hour"), "dh", s_form.digestHour, 0, 23);
  sendNumField(F("Digest min"), "dm", s_form.digestMinute, 0, 59);
  server.sendContent(F("</div><div class=row>"));
  sendNumField(F("Reboot hour"), "rh", s_form.rebootHour, 0, 23);
  sendNumField(F("Reboot min"), "rm", s_form.rebootMinute, 0, 59);
  server.sendContent(F("</div>"));

  server.sendContent(F(
    "<button type=submit>Save and restart</button></form>"
    "<script>fetch('/scan').then(r=>r.json()).then(n=>{"
    "document.getElementById('nets').innerHTML="
    "n.map(s=>'<option value=\"'+s.replace(/\"/g,'&quot;')+'\">').join('')"
    "}).catch(()=>{})</script>"));
  // No explicit terminator needed — _finalizeResponse() emits the final chunk
  // once the handler returns.
}

// -------------------------------------------------------------- handlers ----
static void handleRoot() {
  if (!authorized()) return;
  s_form = g_settings;
  renderForm(String(""));
}

static void handleScan() {
  if (!authorized()) return;
  int n = WiFi.scanNetworks();
  String out = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) out += ',';
    out += '"';
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "");
    ssid.replace("\"", "");
    out += ssid;
    out += '"';
  }
  out += ']';
  WiFi.scanDelete();
  server.send(200, F("application/json"), out);
}

static void handleSave() {
  if (!authorized()) return;
  s_form = g_settings;

  String ssid  = server.arg("ssid");
  String pass  = server.arg("pass");
  String base  = server.arg("base");
  String topic = server.arg("topic");
  String tzc   = server.arg("tzcustom");
  // Blank password fields mean "keep what is stored" — the form never renders
  // a stored secret back to the browser, so blank is the normal state.
  String token   = server.arg("token");
  String secret  = server.arg("secret");
  String admpass = server.arg("admpass");
  token.trim();
  secret.trim();

  ssid.trim();
  base.trim();
  topic.trim();
  tzc.trim();

  s_form.wifiSsid  = ssid;
  s_form.hostname  = server.arg("host");
  s_form.ntfyBase  = base;
  s_form.ntfyTopic = topic;
  s_form.tz        = tzc.length() ? tzc : server.arg("tz");
  s_form.hostname.trim();

  if (server.hasArg("tclear")) s_form.ntfyToken = "";
  if (token.length())   s_form.ntfyToken = token;
  if (secret.length())  s_form.cmdSecret = secret;
  if (admpass.length()) s_form.adminPass = admpass;

  // An empty password field means "keep what is stored", so a user editing the
  // schedule does not have to retype their Wi-Fi password every time.
  if (pass.length()) s_form.wifiPass = pass;

  s_form.checkIntervalMin = server.arg("chk").toInt();
  s_form.pollIntervalSec  = server.arg("poll").toInt();
  s_form.digestHour       = server.arg("dh").toInt();
  s_form.digestMinute     = server.arg("dm").toInt();
  s_form.rebootHour       = server.arg("rh").toInt();
  s_form.rebootMinute     = server.arg("rm").toInt();

  if (s_form.ntfyBase.endsWith("/")) {
    s_form.ntfyBase.remove(s_form.ntfyBase.length() - 1);
  }
  if (!s_form.hostname.length()) s_form.hostname = DEFAULT_HOSTNAME;

  String err;
  if (!s_form.wifiSsid.length()) {
    err = F("Wi-Fi network name is required.");
  } else if (!s_form.ntfyTopic.length()) {
    err = F("ntfy topic is required.");
  } else if (!s_form.ntfyBase.startsWith("http://") &&
             !s_form.ntfyBase.startsWith("https://")) {
    err = F("ntfy server must start with http:// or https://");
  } else if (s_form.checkIntervalMin < 1 || s_form.checkIntervalMin > 1440) {
    err = F("Check interval must be 1-1440 minutes.");
  } else if (s_form.pollIntervalSec < 30 || s_form.pollIntervalSec > 3600) {
    err = F("Poll interval must be 30-3600 seconds.");
  } else if (s_form.digestHour > 23 || s_form.rebootHour > 23 ||
             s_form.digestMinute > 59 || s_form.rebootMinute > 59) {
    err = F("Digest and reboot times must be a valid 24-hour clock.");
  } else if (!s_form.cmdSecret.length()) {
    err = F("Command passphrase is required.");
  } else if (s_form.cmdSecret.indexOf(' ') >= 0 ||
             s_form.cmdSecret.indexOf('\t') >= 0) {
    // Commands split the passphrase off at the first space, so one containing
    // a space could never match and would silently lock out every command.
    err = F("Command passphrase cannot contain spaces.");
  } else if (!s_form.adminPass.length()) {
    err = F("Admin password is required.");
  } else if (s_form.cmdSecret.length() > 64 || s_form.adminPass.length() > 64) {
    err = F("Passphrase and admin password are limited to 64 characters.");
  } else if (s_form.cmdSecret.equalsIgnoreCase(s_form.adminPass)) {
    // Commands match the passphrase case-insensitively, so compare that way too.
    err = F("Use a different admin password from the command passphrase.");
  }

  if (err.length()) {
    renderForm(err);
    return;
  }

  g_settings = s_form;
  if (!settingsSave()) {
    renderForm(F("Could not write config.json to flash."));
    return;
  }

  server.send(200, F("text/html; charset=utf-8"), F(
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Saved</title>"
    "<body style=\"font:16px/1.5 system-ui,sans-serif;padding:2rem;max-width:30rem;"
    "margin-inline:auto\"><h1>Saved</h1>"
    "<p>Restarting now. The device will join your network — watch your ntfy "
    "topic for a boot message, which includes the address of this settings "
    "page.</p>"
    "<p>If it cannot connect, the setup access point comes back automatically "
    "after 30 seconds.</p>"));

  saveDone = true;
  delay(400);          // let the response actually reach the browser
  ESP.restart();
}

// Captive-portal probes: answering with a redirect is what makes phones pop
// the login sheet on their own instead of silently marking the AP as offline.
static void handleCaptive() {
  server.sendHeader(F("Location"), String("http://") + webconfigIp(), true);
  server.send(302, F("text/plain"), F(""));
}

static void handleNotFound() {
  if (apMode) {
    handleCaptive();
  } else {
    server.send(404, F("text/plain"), F("Not found"));
  }
}

static void installRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/generate_204", HTTP_GET, handleCaptive);         // Android
  server.on("/gen_204", HTTP_GET, handleCaptive);              // Android
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);  // Apple
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);             // Windows
  server.on("/fwlink", HTTP_GET, handleCaptive);               // Windows
  server.onNotFound(handleNotFound);
}

// ----------------------------------------------------------------- modes ----
void webconfigBeginAP() {
  if (apMode) return;

  String ap = webconfigApSsid();
  WiFi.softAP(ap.c_str());  // open network — no password to mistype
  delay(200);

  IPAddress ip = WiFi.softAPIP();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(DNS_PORT, "*", ip);

  if (!serverUp) {
    installRoutes();
    server.begin();
    serverUp = true;
  }
  apMode = true;

  Serial.printf("[web] setup AP up: ssid=%s  http://%s\n", ap.c_str(),
                ip.toString().c_str());
}

void webconfigStopAP() {
  if (!apMode) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  apMode = false;
  Serial.println(F("[web] setup AP down"));
}

void webconfigBeginSTA() {
  if (!serverUp) {
    installRoutes();
    server.begin();
    serverUp = true;
  }
  // Always re-advertise: after an AP-fallback recovery or a DHCP change the
  // responder would otherwise keep announcing the previous address.
  if (mdnsUp) {
    MDNS.end();
    mdnsUp = false;
  }
  if (MDNS.begin(g_settings.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  Serial.printf("[web] settings page: http://%s  (http://%s.local)\n",
                webconfigIp().c_str(), g_settings.hostname.c_str());
}

void webconfigTick() {
  if (!serverUp) return;
  if (apMode) dns.processNextRequest();
  server.handleClient();
  // No MDNS.update() on ESP32: the responder runs in its own task.
}

// Unprovisioned: there is nothing useful this device can do without a config,
// so the portal stays up indefinitely rather than timing out into a state
// where it neither monitors anything nor tells you how to fix it.
void webconfigRunSetupPortal() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  authNeeded = false;
  webconfigBeginAP();

  Serial.println(F("[web] unprovisioned — waiting for setup, no timeout"));
  Serial.printf("[web] join \"%s\" then open http://%s\n",
                webconfigApSsid().c_str(), webconfigIp().c_str());

  saveDone = false;
  while (!saveDone) {
    webconfigTick();
    delay(5);
    yield();
  }
  // handleSave() restarts the device, so this is only reached if that failed.
}
