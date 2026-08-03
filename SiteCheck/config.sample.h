// config.sample.h — template for the sketch's build-time configuration.
//
// Copy this file to config.h in the same directory and edit it:
//
//     cp SiteCheck/config.sample.h SiteCheck/config.h
//
// config.h holds your Wi-Fi password and your private ntfy topic names, so it
// is deliberately git-ignored. This sample is the version that gets committed;
// keep the two in sync when you add a setting.

#pragma once

// ---------------------------------------------------------------- Wi-Fi ----
// 2.4 GHz only — the ESP8266 has no 5 GHz radio. WPA2-Personal; enterprise
// networks and captive portals are not supported.
#define WIFI_SSID              "your-ssid"
#define WIFI_PASSWORD          "your-password"
// DHCP hostname the device announces to your router.
#define WIFI_HOSTNAME          "sitecheck"

// ------------------------------------------------------------------ ntfy ----
// Server root, no trailing slash. Use http:// for a self-hosted server that
// has no TLS; https:// costs ~16 KB of heap per connection but is the default.
#define NTFY_BASE              "https://ntfy.sh"
// One topic carries both directions: you publish ADD/REMOVE/STATUS here and
// the device publishes alerts and the daily digest here. Subscribe to it to
// see reports; publish to it to send commands.
#define NTFY_TOPIC             "your-topic-change-me"
// On public ntfy.sh the topic NAME is the only access control, so use a long
// random name. Leave the token empty for open topics; on a server with auth
// enabled, put an ntfy access token (tk_...) here.
#define NTFY_TOKEN             ""

// Tag stamped on every message the device sends. Because it listens on the
// topic it also publishes to, this is how it recognises and ignores its own
// messages — without it, each report would be read back as a command.
#define NTFY_DEVICE_TAG        "sitecheck"

// ------------------------------------------------------------- timekeeping --
// POSIX TZ string — drives the local-time digest and reboot schedule, including
// DST. America/Vancouver. Full list: the ESP8266 core's TZ.h.
#define TZ_STRING              "PST8PDT,M3.2.0,M11.1.0"
#define NTP_SERVER_1           "pool.ntp.org"
#define NTP_SERVER_2           "time.nist.gov"

// --------------------------------------------------------------- schedule ---
// How often every host is checked. Alerts can therefore lag a real outage by
// up to this long.
#define CHECK_INTERVAL_MS      (30UL * 60UL * 1000UL)
// How often the topic is polled for commands — also the worst-case delay
// before a command you publish is acted on. Each poll is one TLS handshake
// (~2-3 s, ~16 KB of heap while it runs). Below ~30 s an unauthenticated
// client risks being rate-limited by ntfy.sh.
#define POLL_INTERVAL_MS       (60UL * 1000UL)
// Delay between boot and the first check pass, so Wi-Fi and NTP settle first.
#define FIRST_CHECK_DELAY_MS   (30UL * 1000UL)

// Daily digest, local time (24h).
#define DIGEST_HOUR            8
#define DIGEST_MINUTE          0
// Scheduled self-reboot, local time. Keep it well away from the digest hour.
#define REBOOT_HOUR            3
#define REBOOT_MINUTE          0

// ----------------------------------------------------------------- checks ---
// Hard cap on monitored hosts. Each costs ~124 bytes of static RAM; raising it
// much past 30 also stretches one check pass past a couple of minutes.
#define MAX_HOSTS              30
// Per-request timeout for HTTP/HTTPS checks. Too low and slow-but-alive sites
// alert falsely; too high and one dead host stalls the pass.
#define HTTP_TIMEOUT_MS        8000
// ICMP echoes per ping check. Any single reply counts as up.
#define PING_COUNT             2
// Pause between hosts so sockets close and the heap settles. Do not set to 0.
#define INTER_CHECK_DELAY_MS   250

// Set to 0 if the ESPping library is not installed; ping-type hosts are
// then reported as unsupported instead of being checked.
#define USE_PING               1

// ------------------------------------------------------------------ misc ----
// Publish a one-line "booted" message on startup. Useful for spotting crash
// loops; set to 0 once the device is stable.
#define NOTIFY_ON_BOOT         1
// Serial console speed for the diagnostic log.
#define SERIAL_BAUD            115200
