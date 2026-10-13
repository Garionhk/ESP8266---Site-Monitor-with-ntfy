# SiteCheck — ESP8266 / ESP32-S3 server health checker

Implementation of [plan.md](plan.md). It is controlled day to day through ntfy —
no display, no buttons. A small web UI handles first-time setup and settings,
and stays available on the LAN.

**Current version: v2.0.0** — [release notes and downloads](https://github.com/Garionhk/ESP8266---Site-Monitor-with-ntfy/releases/tag/v2.0.0).

## Download

Prebuilt firmware is attached to each
[GitHub release](https://github.com/Garionhk/ESP8266---Site-Monitor-with-ntfy/releases)
and kept in each variant's `dist/` folder. No Arduino toolchain is needed to
flash these — only [esptool](https://github.com/espressif/esptool).

| File | Board | Write at |
| --- | --- | --- |
| `sitecheck-v2.0.0-esp8266-4m2m.bin` | 4 MB ESP8266 — NodeMCU v2, Wemos D1 mini, most ESP-12 modules | `0x0` |
| `sitecheck-v2.0.0-esp8266-1m64.bin` | 1 MB ESP8266 — ESP-01, older Sonoff | `0x0` |
| `sitecheck-v2.0.0-esp32s3-n16r8-full.bin` | ESP32-S3 N16R8, new board (bootloader + partitions + app) | `0x0` |
| `sitecheck-v2.0.0-esp32s3-n16r8-app.bin` | ESP32-S3 N16R8 already running SiteCheck | `0x10000` |

Verify downloads against `SHA256SUMS`, then follow the flashing steps in
[ESP8266/flash.md](ESP8266/flash.md) or
[ESP32S3-N16R8/flash.md](ESP32S3-N16R8/flash.md). To upgrade a device that is
already set up, write the new image **without erasing** — configuration, hosts
and the poll cursor are kept.

## Variants

The same firmware is maintained for two boards. Each folder is a self-contained
Arduino sketch (`SiteCheck/`) plus its own build and flashing guide.

| Folder | Board | Build & flash |
| --- | --- | --- |
| [ESP8266/](ESP8266) | any ESP8266 with 1 MB+ flash (NodeMCU, D1 mini, ESP-01, …) | [ESP8266/flash.md](ESP8266/flash.md) |
| [ESP32S3-N16R8/](ESP32S3-N16R8) | ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM), e.g. DevKitC-1 | [ESP32S3-N16R8/flash.md](ESP32S3-N16R8/flash.md) |

Behaviour, settings and commands are identical. The ESP32-S3 port differs only
where the platform does:

- the recovery button is **BOOT** (GPIO0) instead of FLASH;
- the 16 MB flash layout comes from
  [partitions.csv](ESP32S3-N16R8/SiteCheck/partitions.csv) in the sketch folder
  (2 × 6.25 MB app, 3.4 MB LittleFS);
- core APIs: `WebServer` / `ESPmDNS` / `HTTPClient`, `configTzTime()`,
  `esp_reset_reason()`, `LittleFS.begin(true)`.

Still 2.4 GHz Wi-Fi only on both — neither chip has a 5 GHz radio.

## Files

Both `SiteCheck/` folders contain the same files (links below go to the ESP8266
copy):

| File | Contents |
| --- | --- |
| [SiteCheck.ino](ESP8266/SiteCheck/SiteCheck.ino) | setup/loop, Wi-Fi, NTP, digest & reboot scheduling |
| [config.h](ESP8266/SiteCheck/config.h) | compile-time tuning and factory defaults (no secrets) |
| [settings.ino](ESP8266/SiteCheck/settings.ino) | runtime config in `/config.json` |
| [webconfig.ino](ESP8266/SiteCheck/webconfig.ino) | setup portal and settings web server |
| [common.h](ESP8266/SiteCheck/common.h) | shared types, globals, prototypes |
| [storage.ino](ESP8266/SiteCheck/storage.ino) | `hosts.json` / `state.json` in LittleFS |
| [ntfy.ino](ESP8266/SiteCheck/ntfy.ino) | publish to the topic, poll it for commands |
| [commands.ino](ESP8266/SiteCheck/commands.ino) | `ADD` / `REMOVE` / `STATUS` parsing, target rules |
| [checks.ino](ESP8266/SiteCheck/checks.ino) | the cooperative, strictly sequential check pass |
| [report.ino](ESP8266/SiteCheck/report.ino) | digest / status formatter |

## Dependencies

ESP8266 Arduino core 3.x or esp32 Arduino core 3.x, plus two libraries:

```bash
arduino-cli lib install ArduinoJson ESPping
```

`ESPping` is only needed when `USE_PING` is 1 in `config.h`. ArduinoJson v6 and
v7 both work. The web UI uses `ESP8266WebServer`/`WebServer`, `DNSServer` and
`ESP8266mDNS`/`ESPmDNS`, all of which ship with the respective core — no extra
install.

## Configuration

**Nothing user-specific is compiled in.** There is no config file to copy and no
credentials in the source. On first boot the device has no configuration, so it
comes up as its own Wi-Fi access point running a setup page.

### First run

1. Flash the firmware and power the device.
2. Join the open Wi-Fi network **`SiteCheck-XXXX`** (the suffix is the last two
   bytes of the board's MAC — the exact name is printed on serial at boot).
3. The setup page should open by itself. If it doesn't, browse to
   **http://192.168.4.1**.
4. Fill in your Wi-Fi, an ntfy topic, a **command passphrase**, an **admin
   password**, timezone and schedule, then **Save and restart**.

The device reboots, joins your network, and publishes a boot message to your
topic containing the address of its settings page.

This setup screen has **no timeout**. An unprovisioned device has no useful
fallback — no credentials means no Wi-Fi, no NTP and no monitoring — so a page
that stays up telling you how to fix it is the most useful state available.

### Changing settings later

The settings server runs **all the time** while the device is on your network —
it is not just a first-boot thing. Browse to the device's LAN address (it is in
the boot message, and in every `STATUS` reply) or to **http://sitecheck.local**
via mDNS, and log in as **`admin`** with the admin password. Same page, same
fields. Saving always reboots: the timezone, schedule and ntfy topic each have different refresh
paths, and a ~5 s restart avoids a class of partial-state bugs.

Password fields (Wi-Fi password, ntfy token, command passphrase, admin
password) are always rendered **empty** — stored secrets are never sent to the
browser. Leave one blank to keep the current value; fill it in only to change
it. Tick *clear stored token* to remove the ntfy token.

### If Wi-Fi fails

After 30 seconds without a connection the device raises the setup AP again
**while continuing to retry your network in the background** (`WIFI_AP_STA`).
So a mistyped password is recoverable from a phone — no USB cable, no reflash.
The fallback AP is open, but its settings page still requires the admin login.
When the real network comes back the AP drops automatically and the settings
page returns to the LAN address.

### Settings in the web UI

| Setting | What it does |
| --- | --- |
| Wi-Fi network / password | 2.4 GHz only — neither the ESP8266 nor the ESP32-S3 has a 5 GHz radio. WPA2-Personal; no enterprise or captive-portal networks. The network list is populated by a scan. |
| Device hostname | DHCP name on your router, and the mDNS name: `http://<hostname>.local` |
| ntfy server | Server root, no trailing slash. `http://` skips TLS and saves ~16 KB of heap per connection — fine on a LAN-only server, but commands then travel in clear text. |
| ntfy topic | The single topic used both ways. On ntfy.sh the name is the only access control, so use something long and random. |
| ntfy access token | Optional `Bearer` token for a server with authentication enabled. |
| Command passphrase | Prefix for every command (`<passphrase> STATUS`). Required; no spaces; matched case-insensitively. |
| Admin password | Protects this settings page (username **`admin`**). Required, and must differ from the command passphrase, because that one travels through ntfy. |
| Timezone | POSIX TZ string, picked from a list or entered by hand. Affects only *when* the digest and reboot fire. |
| Check every (min) | Time between full check passes — also the worst-case lag between a host going down and the alert. 1–1440. |
| Poll every (sec) | Time between command polls — the worst-case delay before a published command is acted on. Each poll is one TLS handshake, ~1.4 s. 30–3600. |
| Digest hour / minute | Local time the daily digest is sent. |
| Reboot hour / minute | Local time of the scheduled self-reboot. Keep it hours away from the digest. |

Invalid values are rejected with the form redisplayed and an error banner —
nothing is written to flash until it validates.

### Security and recovery

- **Command passphrase** prefixes every command. A message without it is
  ignored silently — no reply, so a stranger on a public topic learns nothing.
  `ADD`, `REMOVE` and `FACTORYRESET` *always* need a passphrase to be set.
- **Admin password** gates the settings page (HTTP Basic auth, username
  `admin`) on the LAN and on the Wi-Fi fallback AP. Only the setup portal is
  open, and that is reached only by an unprovisioned device or the button
  below.

| Situation | Way back in |
| --- | --- |
| Device is online | Browse to its address (in the boot message and every `STATUS`) and log in as `admin` |
| Wi-Fi is down | Join the `SiteCheck-XXXX` network (portal at `192.168.4.1`, admin login required) while it keeps retrying your router |
| Moved house, or forgot the admin password | Press **RST**, release it, then **immediately press and hold FLASH** (ESP8266) / **BOOT** (ESP32-S3) for ~2 s — setup portal, no login required |
| Remotely | `<passphrase> FACTORYRESET` over ntfy |

The FLASH/BOOT button must be pressed *after* RST is released: GPIO0 held low
through the reset itself puts the chip into its serial bootloader instead.
The button pin (`BOOT_BUTTON_PIN`, GPIO0) is set in `config.h`.

Physical access bypasses the admin password by design: anyone holding the board
can reflash it anyway.

**Devices set up before this firmware** keep working after a flash: Wi-Fi,
topic, hosts and schedule are read as before. They have **no admin password or
passphrase**, though, so the LAN settings page answers *Settings locked* and
`ADD`/`REMOVE` reply *Passphrase required* until both are set. Use the FLASH
button step once, fill in both (other blank password fields keep their stored
values), and save.

**The settings page is plain HTTP.** The admin login crosses the LAN
unencrypted — use it on a network you trust.

### Settings still in config.h

These are genuinely build-time and stay in `config.h` (one per variant), which
contains no secrets and is committed to the repository:

| Setting | Default | Why it can't be runtime |
| --- | --- | --- |
| `MAX_HOSTS` | `30` | Sizes a static array |
| `USE_PING` | `1` | Gates an `#include` of the ESPping library |
| `HTTP_TIMEOUT_MS` | `8000` | Per-request timeout for HTTP/HTTPS checks |
| `INTER_CHECK_DELAY_MS` | `250` | Pause between hosts so sockets close and the heap settles. **Do not set to 0** — this is what keeps 30 sequential TLS connections from fragmenting the heap. |
| `PING_COUNT` | `2` | ICMP echoes per ping check |
| `NTFY_DEVICE_TAG` | `sitecheck` | Tag identifying the device's own messages (see below) |
| `WIFI_CONNECT_MS` | `30000` | How long to try before opening the portal |
| `BOOT_BUTTON_PIN` | `0` | Button that forces the setup portal at boot (FLASH on NodeMCU / D1 mini, BOOT on ESP32-S3 DevKitC) |
| `NOTIFY_ON_BOOT` | `1` | Publish a boot message with the settings URL |

The `DEFAULT_*` values in the same file are what an unprovisioned device starts
from and what the settings form is pre-filled with.

### How one topic stays out of a feedback loop

Because the device listens on the topic it also publishes to, it reads its own
reports back. Left alone, a digest line like `focaltek: up` would parse as an
unknown command, draw an "unknown command" reply, and that reply would be read
back in turn.

Two mechanisms prevent it:

1. Every message the device publishes carries `NTFY_DEVICE_TAG`, and the poll
   skips any message carrying that tag. **Do not set the tag to an empty
   string** — that disables the filter.
2. The poll cursor advances past *every* message seen, not just commands, so
   the device never re-downloads its own growing tail of alerts.

Note the trade-off of a single topic: anyone you give the name to in order to
*watch* the reports can also *send* commands — there is no read-only half. Use
an access token with a self-hosted ntfy if you need those separated.

## Build and flash

ESP8266:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 ESP8266/SiteCheck
```

```bash
arduino-cli upload -p /dev/cu.usbserial-0001 --fqbn esp8266:esp8266:nodemcuv2 ESP8266/SiteCheck
```

Use whatever FQBN matches your board (`esp8266:esp8266:d1_mini`,
`esp8266:esp8266:generic`, …) and pick a flash layout that includes a LittleFS
partition.

ESP32-S3 N16R8:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,PartitionScheme=custom ESP32S3-N16R8/SiteCheck
```

```bash
arduino-cli upload -p /dev/cu.usbserial-0001 --fqbn esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,PartitionScheme=custom ESP32S3-N16R8/SiteCheck
```

No filesystem upload is needed on either board — all three JSON files are
created on first use. See each variant's `flash.md` for distributable binaries.

## Commands

Publish these to your topic, prefixed with your command passphrase; replies and
alerts come back on the same topic. A message without the right passphrase is
ignored silently.

```bash
curl -d 'mysecret ADD|focaltek|focaltek.com;nas|192.168.1.50' ntfy.sh/your-topic
```

```bash
curl -d 'mysecret REMOVE|nas' ntfy.sh/your-topic
```

```bash
curl -d 'mysecret STATUS' ntfy.sh/your-topic
```

### Syntax

| Command | Form | Notes |
| --- | --- | --- |
| `ADD` | `ADD\|Name\|target;Name2\|target2;…` | Batch several with `;`. Each entry is a name and a target separated by `\|`. |
| `REMOVE` | `REMOVE\|Name;Name2;…` | Aliases: `DEL`, `DELETE`. Names only — no targets. |
| `STATUS` | `STATUS` | Immediate report, identical in format to the daily digest, plus the settings page address. |
| `FACTORYRESET` | `FACTORYRESET` | Deletes the configuration and reboots into the setup portal. Monitored hosts are kept. |

The passphrase and a space come first in every form above, e.g.
`mysecret STATUS`.

Verbs and the passphrase are case-insensitive — `add`, `Add` and `ADD` are the
same, so phone auto-capitalisation doesn't lock you out. Anything else
gets an explicit `Unknown command:` reply rather than being silently dropped.

The verb may be followed by either a `|` or a space, so all three of these are
equivalent, and spaces around the separators are ignored:

```
ADD|FileServer|192.168.0.18
ADD FileServer | 192.168.0.18
ADD | FileServer | 192.168.0.18
```

### Target parsing on ADD

1. A dotted-quad IPv4 address → ping host, stored as the raw IP
2. Starts with `http://` or `https://` → used exactly as entered
3. Anything else (a bare domain) → stored as `https://<input>`

A bare token with no dot in it is rejected as not a hostname; `localhost` is
the one exception.

### Replies

Every `ADD`/`REMOVE` reply ends with a `N/30 hosts` count and reports each
outcome separately, so nothing fails silently:

```
Added: focaltek (https://focaltek.com), nas (192.168.1.50)
Already present: immich
Skipped, 30-host limit reached: extra1
Could not parse: notahost
2/30 hosts
```

`REMOVE` reports `Removed: …` and `Unknown name: …` the same way.

### Staying reachable

The web UI is never blocked for more than a single network request:

- A check pass runs **one host per main-loop iteration**, so the settings page
  is serviced between hosts. A blocking pass would have frozen it for up to
  ~8 minutes with 30 slow hosts.
- Command polls take priority over the check pass, so commands are still
  answered promptly while a pass is running.
- Waits between operations pump the web server instead of sleeping.

The one irreducible stall is a single in-flight HTTP/TLS request, because
`HTTPClient` blocks internally — bounded by `NTFY_TIMEOUT_MS` (5 s) for ntfy
and `HTTP_TIMEOUT_MS` (8 s) for a monitored host. The server is deliberately
*not* pumped while a socket is open: servicing a web request allocates a socket
of its own, and doing that alongside a live TLS session is the concurrent-socket
case the whole design avoids.

### Address changes

DHCP can hand out a different address after a reconnect or a lease renewal,
which would leave the address in the last boot message stale. The device
notices and publishes a `SiteCheck online` message with the new one, and
re-advertises mDNS. `STATUS` always reports the current address too.

### Limits

- Names are truncated to 23 characters, targets to 95
- Name matching (for `REMOVE` and duplicate detection) is case-insensitive
- Non-ASCII characters, quotes and backslashes are stripped from names and
  targets — they would break the JSON store and ntfy's headers
- At most 8 commands are consumed per poll; any beyond that wait for the next
  poll rather than being lost
- Commands are picked up on the next poll (default every 60 s), so a reply is
  not instant — expect ~30 s on average, 60 s worst case, plus a second or two
  for the device to publish it

## Behaviour notes

- Alerts fire only on state transitions; a host that stays up is silent.
- A newly added host that comes back down on its first check alerts
  immediately; a first check that succeeds is silent.
- `hosts.json` is written on ADD/REMOVE, on any state transition, and once a
  day with the digest — not on every check.
- The settings server runs continuously in station mode; the setup access point
  appears only when the network cannot be joined, and drops again automatically
  once it can.
- Configuration lives in `/config.json`; hosts in `hosts.json`; the ntfy poll
  cursor in `state.json`. Reflashing keeps all three. The setup portal comes
  back via the FLASH/BOOT button at boot, `FACTORYRESET`, or erasing the filesystem.
- The device reboots itself daily at the configured reboot hour as insurance against heap
  fragmentation, and logs free heap to Serial on every check pass.

## Versioning

The firmware version is `SITECHECK_VERSION` in each variant's `config.h`. It is
printed on serial at boot and included in the ntfy boot message, so you can
check what a device is running without a cable. Both variants always share the
same version number.

## License

Apache License 2.0 — see [LICENSE-2.0.txt](LICENSE-2.0.txt).
