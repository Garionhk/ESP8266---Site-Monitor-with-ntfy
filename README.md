# SiteCheck — ESP8266 server health checker

Implementation of [plan.md](plan.md). The sketch lives in [SiteCheck/](SiteCheck)
and is controlled entirely through ntfy — no web UI, no display, no buttons.

## Files

| File | Contents |
| --- | --- |
| [SiteCheck.ino](SiteCheck/SiteCheck.ino) | setup/loop, Wi-Fi, NTP, digest & reboot scheduling |
| [config.sample.h](SiteCheck/config.sample.h) | configuration template — copy to `config.h` |
| [common.h](SiteCheck/common.h) | shared types, globals, prototypes |
| [storage.ino](SiteCheck/storage.ino) | `hosts.json` / `state.json` in LittleFS |
| [ntfy.ino](SiteCheck/ntfy.ino) | publish to the topic, poll it for commands |
| [commands.ino](SiteCheck/commands.ino) | `ADD` / `REMOVE` / `STATUS` parsing, target rules |
| [checks.ino](SiteCheck/checks.ino) | the sequential 30-minute check loop |
| [report.ino](SiteCheck/report.ino) | digest / status formatter |

## Dependencies

ESP8266 Arduino core 3.x, plus two libraries:

```bash
arduino-cli lib install ArduinoJson ESPping
```

`ESPping` is only needed when `USE_PING` is 1 in `config.h`. ArduinoJson v6 and
v7 both work.

## Configuration

The sketch will not compile until `SiteCheck/config.h` exists. It is not in the
repository — it holds your Wi-Fi password and your private topic names, so
[.gitignore](.gitignore) excludes it. Create it from the template:

```bash
cp SiteCheck/config.sample.h SiteCheck/config.h
```

Then edit `SiteCheck/config.h`. At minimum you must change `WIFI_SSID`,
`WIFI_PASSWORD` and `NTFY_TOPIC`; everything else has a working default. When
you add or rename a setting, update `config.sample.h` too — it is the only copy
that gets committed.

### Wi-Fi

| Setting | Default | What it does |
| --- | --- | --- |
| `WIFI_SSID` | `your-ssid` | Network to join. **2.4 GHz only** — the ESP8266 has no 5 GHz radio. WPA2-Personal; enterprise logins and captive portals won't work. |
| `WIFI_PASSWORD` | `your-password` | WPA2 passphrase. Leave `""` for an open network. |
| `WIFI_HOSTNAME` | `sitecheck` | DHCP hostname the device announces, so it's identifiable in your router's client list. |

### ntfy

| Setting | Default | What it does |
| --- | --- | --- |
| `NTFY_BASE` | `https://ntfy.sh` | Server root, **no trailing slash**. Point at your own instance if self-hosting. `http://` skips TLS and saves ~16 KB of heap per connection — reasonable on a LAN-only server, but then commands travel in clear text. |
| `NTFY_TOPIC` | placeholder | The single topic used in both directions: the device polls it for `ADD`/`REMOVE`/`STATUS` and publishes alerts, replies and the digest to it. Subscribe here, and publish commands here. |
| `NTFY_TOKEN` | `""` | Sent as `Authorization: Bearer …`. Leave empty for an open topic; set an ntfy access token (`tk_…`) if your server requires auth. |
| `NTFY_DEVICE_TAG` | `sitecheck` | Tag stamped on every outgoing message so the device can recognise and skip its own traffic when polling. Changing it is fine; emptying it is not (see below). |

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

The tag shows up as a small label on each notification in the ntfy app, which
also makes device messages easy to tell apart from your own at a glance.

On public ntfy.sh the topic **name is the only access control**, so use a long
random name. Note the trade-off you accept with a single topic: anyone you give
the name to in order to *watch* the reports can also *send* commands — there is
no read-only half any more. Use `NTFY_TOKEN` with a self-hosted server if you
need those separated.

### Timekeeping

| Setting | Default | What it does |
| --- | --- | --- |
| `TZ_STRING` | `PST8PDT,M3.2.0,M11.1.0` | POSIX timezone string, including DST rules. Only affects *when* the digest and reboot fire. The bundled list is in the core's `TZ.h`; the shown value is America/Vancouver. |
| `NTP_SERVER_1` | `pool.ntp.org` | Primary time source, queried at boot and re-synced periodically. |
| `NTP_SERVER_2` | `time.nist.gov` | Fallback. |

Without a valid NTP time the checks and alerts still run — only the daily
digest and the scheduled reboot are suppressed.

### Schedule

| Setting | Default | What it does |
| --- | --- | --- |
| `CHECK_INTERVAL_MS` | 30 min | Time between full check passes. This is also the worst-case lag between a host going down and the alert. |
| `POLL_INTERVAL_MS` | 60 s | Time between polls for commands, i.e. the worst-case delay before a command you publish is acted on (average is half of it). Every poll is one TLS handshake — roughly 2-3 s and ~16 KB of heap while it runs, ~1440 of them a day at this setting. Below ~30 s an unauthenticated client risks being rate-limited by ntfy.sh. |
| `FIRST_CHECK_DELAY_MS` | 30 s | Grace period after boot before the first check, letting Wi-Fi and NTP settle. |
| `DIGEST_HOUR` / `DIGEST_MINUTE` | `8` / `0` | Local time the daily digest is sent (24-hour clock). |
| `REBOOT_HOUR` / `REBOOT_MINUTE` | `3` / `0` | Local time of the scheduled self-reboot. Keep it hours away from the digest — the reboot takes the device offline for ~15 s. |

### Checks

| Setting | Default | What it does |
| --- | --- | --- |
| `MAX_HOSTS` | `30` | Hard cap, enforced on `ADD`. Each host costs ~124 bytes of static RAM. Raising it much past 30 also stretches a check pass beyond a couple of minutes, since hosts are checked one at a time. |
| `HTTP_TIMEOUT_MS` | `8000` | Per-request timeout for HTTP/HTTPS checks. Too low gives false alarms on slow-but-alive sites; too high lets one dead host stall the whole pass. |
| `PING_COUNT` | `2` | ICMP echoes per ping check. Any single reply counts as up. |
| `INTER_CHECK_DELAY_MS` | `250` | Pause between hosts so the previous socket is fully closed and the heap settles. **Do not set to 0** — this is what keeps 30 sequential TLS connections from fragmenting the heap. |
| `USE_PING` | `1` | Set to `0` to build without the ESPping library. Ping-type hosts are then skipped and keep their previous state instead of being checked. |

### Misc

| Setting | Default | What it does |
| --- | --- | --- |
| `NOTIFY_ON_BOOT` | `1` | Publishes a one-line "booted" message with the reset reason. Handy for spotting crash loops during bring-up; set to `0` once the device is stable and you want silence. |
| `SERIAL_BAUD` | `115200` | Speed of the diagnostic serial log. Match it in your serial monitor. |

## Build and flash

With `config.h` in place:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 SiteCheck
```

```bash
arduino-cli upload -p /dev/cu.usbserial-0001 --fqbn esp8266:esp8266:nodemcuv2 SiteCheck
```

Use whatever FQBN matches your board (`esp8266:esp8266:d1_mini`,
`esp8266:esp8266:generic`, …) and pick a flash layout that includes a LittleFS
partition. No filesystem upload is needed — both JSON files are created on
first use.

## Commands

Publish these to your topic; replies and alerts come back on the same topic.

```bash
curl -d 'ADD|focaltek|focaltek.com;nas|192.168.1.50' ntfy.sh/your-topic
```

```bash
curl -d 'REMOVE|nas' ntfy.sh/your-topic
```

```bash
curl -d 'STATUS' ntfy.sh/your-topic
```

### Syntax

| Command | Form | Notes |
| --- | --- | --- |
| `ADD` | `ADD\|Name\|target;Name2\|target2;…` | Batch several with `;`. Each entry is a name and a target separated by `\|`. |
| `REMOVE` | `REMOVE\|Name;Name2;…` | Aliases: `DEL`, `DELETE`. Names only — no targets. |
| `STATUS` | `STATUS` | Immediate report, identical in format to the daily digest. |

Verbs are case-insensitive — `add`, `Add` and `ADD` are the same. Anything else
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
- The device reboots itself daily at `REBOOT_HOUR` as insurance against heap
  fragmentation, and logs free heap to Serial on every check pass.
