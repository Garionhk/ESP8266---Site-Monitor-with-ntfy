// config.h — compile-time tuning and factory defaults.
//
// No secrets live here any more. Wi-Fi credentials, the ntfy topic and token,
// the timezone and the schedule are entered through the web UI on first boot
// and stored in /config.json on LittleFS. That is what makes a built binary
// shareable: `strings SiteCheck.ino.bin` turns up nothing private.
//
// The values below are either factory defaults (what an unprovisioned device
// starts from, and what the settings form is pre-filled with) or genuinely
// build-time constants — MAX_HOSTS sizes a static array and USE_PING gates an
// #include, so neither can move to runtime.

#pragma once

// ----------------------------------------------------- factory defaults -----
#define DEFAULT_HOSTNAME            "sitecheck"
#define DEFAULT_NTFY_BASE           "https://ntfy.sh"
#define DEFAULT_TZ                  "PST8PDT,M3.2.0,M11.1.0"
#define DEFAULT_CHECK_INTERVAL_MIN  30
#define DEFAULT_POLL_INTERVAL_SEC   60
#define DEFAULT_DIGEST_HOUR         8
#define DEFAULT_DIGEST_MINUTE       0
#define DEFAULT_REBOOT_HOUR         3
#define DEFAULT_REBOOT_MINUTE       0

// ------------------------------------------------------- setup / portal -----
// SoftAP name is this plus the last two bytes of the MAC, e.g. SiteCheck-4F84.
#define AP_SSID_PREFIX              "SiteCheck-"
// How long to wait for the configured network before opening the portal.
#define WIFI_CONNECT_MS             30000UL
// While the AP fallback is up, how often to retry the real network.
#define WIFI_RETRY_MS               60000UL
// Pressing this button just after a reset forces the setup portal on an
// already-provisioned device — the way back from a forgotten admin password or
// a moved network. GPIO0 is the BOOT button on ESP32-S3 DevKitC boards. It
// must be pressed AFTER releasing RST: GPIO0 held low through the reset itself
// puts the ESP32-S3 into its serial bootloader instead.
#define BOOT_BUTTON_PIN             0
#define BOOT_WINDOW_MS              2000UL

// ------------------------------------------------------------------ ntfy ----
// Tag stamped on every message the device sends. Because it listens on the
// topic it also publishes to, this is how it recognises and ignores its own
// messages — without it, each report would be read back as a command.
#define NTFY_DEVICE_TAG             "sitecheck"
// ntfy.sh answers in ~1.4 s; the longer HTTP_TIMEOUT_MS budget exists for slow
// monitored hosts, not for it. A shorter timeout here bounds how long the web
// UI can be stalled by a single ntfy request.
#define NTFY_TIMEOUT_MS             5000

// ------------------------------------------------------------------- ntp ----
#define NTP_SERVER_1                "pool.ntp.org"
#define NTP_SERVER_2                "time.nist.gov"

// ---------------------------------------------------------------- checks ----
// Hard cap on monitored hosts; sizes a static array, so it cannot be runtime.
#define MAX_HOSTS                   30
// Per-request timeout for HTTP/HTTPS checks.
#define HTTP_TIMEOUT_MS             8000
// ICMP echoes per ping check. Any single reply counts as up.
#define PING_COUNT                  2
// Pause between hosts so sockets close and the heap settles. Do not set to 0.
#define INTER_CHECK_DELAY_MS        250
// Delay between boot and the first check pass.
#define FIRST_CHECK_DELAY_MS        (30UL * 1000UL)
// Set to 0 to build without the ESPping library; ping hosts are then skipped.
#define USE_PING                    1

// ------------------------------------------------------------------ misc ----
#define SITECHECK_VERSION           "1.0.0"
#define NOTIFY_ON_BOOT              1
#define SERIAL_BAUD                 115200
