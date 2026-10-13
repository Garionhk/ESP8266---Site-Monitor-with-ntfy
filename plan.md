# ESP8266 Server Health Checker — plan.md

## Goal
A standalone ESP8266 device that monitors up to 30 remote hosts (HTTP/HTTPS or
ping) and is entirely controlled and reported on through ntfy. No web UI, no
local display, no buttons — ntfy is the only interface in and out.

## Hardware assumptions
- Any ESP8266 board with LittleFS-capable flash (1MB+; 4MB modules give more
  headroom but aren't required at this scale)
- Wi-Fi only, always-on, mains-powered (not battery)

## Two ntfy topics
- **Command topic** — you publish ADD/REMOVE/STATUS messages here; the device
  polls it
- **Report topic** — the device publishes down alerts, recovery alerts, and
  the daily digest here

Both topic names (and ntfy server, if self-hosted instead of ntfy.sh) are
compile-time or config-file constants.

## Host storage
`hosts.json` in LittleFS. Array of objects:
```json
[
  { "name": "focaltek", "target": "https://focaltek.com", "type": "https",
    "state": "up", "last_change": 1732000000 },
  { "name": "nas", "target": "192.168.1.50", "type": "ping",
    "state": "down", "last_change": 1732003600 }
]
```
`state`/`last_change` are runtime fields, rewritten to flash periodically
(not on every check) to avoid excessive flash wear — e.g. only persist on
state transitions and once daily alongside the digest.

## Target parsing rules (applied on ADD)
1. Input is an IP address (IPv4 dotted-quad check) → `type: ping`, target
   stored as the raw IP
2. Input starts with `http://` or `https://` → `type` = that scheme, used
   exactly as entered
3. Otherwise (bare domain, e.g. `focaltek.com` or `immich.focaltek.com`) →
   `type: https`, target stored as `https://<input>`

## Command protocol (polled from command topic)
Poll `GET <ntfy_base>/<command_topic>/json?since=<last_processed_ts>&poll=1`
every 5–10 minutes (independent timer from the 30-min check loop — this is a
lighter-weight connection but still TLS, so don't poll faster than needed).

Message formats (message body, semicolon-separated for batches):
- `ADD|Name1|target1;Name2|target2;...`
- `REMOVE|Name1;Name2;...`
- `STATUS` — triggers an immediate full status report, same format as the
  daily digest

On each valid command:
- Parse and apply to the in-memory host list + `hosts.json`
- Reply on the report topic with a short confirmation, e.g.
  `Added: focaltek, immich.focaltek.com` / `Removed: nas` / unknown-name or
  parse errors reported explicitly rather than silently ignored
- Advance `last_processed_ts` to the message's timestamp so it's never
  reprocessed, even across reboots (persist this value in LittleFS too)

Enforce the 30-host cap on ADD; if a batch would exceed it, add as many as
fit and report which ones were skipped.

## Check loop (every 30 minutes)
- Iterate the host list **sequentially** — one connection opened, checked,
  and fully closed before the next starts. This is a hard rule on ESP8266
  to avoid heap fragmentation from concurrent TLS buffers.
- HTTP/HTTPS: HEAD request (fall back to GET if HEAD is rejected), 5–8s
  timeout, 2xx–3xx counted as up
- Ping: 1–2 ICMP echoes, short timeout, any reply counts as up
- HTTPS connections use `setInsecure()` — no cert store, no pinning
- On each host, compare new result to stored `state`:
  - **up → down**: publish an immediate down alert, update state
  - **down → up**: publish an immediate recovery alert, update state
  - **no change**: no message sent — this is the "quiet unless something's
    wrong" behavior you asked for
- Persist `hosts.json` after the loop if anything changed

## Daily digest
- Fires once per day at a fixed time (default 08:00, configurable),
  time source is NTP (`configTime()`) synced at boot and periodically
- Message: one line per host, current state only — no uptime %, no
  history, no computed stats. Example:
  ```
  focaltek: up
  immich.focaltek.com: up
  nas (192.168.1.50): down
  ```
- Same formatter used for the on-demand `STATUS` command reply

## Stability safety nets (given a mostly-HTTPS, always-on device)
- Strictly sequential connections everywhere (checks and command polling)
  — never two sockets open at once
- Scheduled self-reboot once daily (default 03:00, away from the digest
  time) as insurance against slow heap fragmentation over long uptime
- Free-heap logged (Serial, if connected) on each check-loop pass for
  debugging during bring-up; not sent anywhere in normal operation

## Config (compile-time or a small config.json, your call at build time)
- Wi-Fi SSID/password
- ntfy base URL (ntfy.sh or self-hosted), command topic, report topic
- Timezone / NTP server
- Digest time, reboot time
- Check interval (default 30 min), command-poll interval (default 5–10 min)

## Explicitly out of scope
- No web UI
- No historical logging/uptime percentages
- No concurrent/parallel checking
- No TLS certificate validation
