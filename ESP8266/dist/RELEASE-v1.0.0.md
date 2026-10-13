# SiteCheck v1.0.0 — ESP8266

First release of SiteCheck as a shareable binary.

## Highlights

- **No credentials in the firmware.** Wi-Fi, hostname, ntfy server/topic/token,
  command passphrase, admin password, timezone and schedule are entered on a
  built-in web page and stored in `/config.json` on LittleFS.
- **Setup portal** — first boot opens the `SiteCheck-XXXX` Wi-Fi with a captive
  page at `http://192.168.4.1`.
- **Settings page on the LAN** at the device's IP or `http://sitecheck.local`,
  login `admin`.
- **Command passphrase** — every ntfy command starts with it
  (`mysecret STATUS`); anything else is ignored silently. `ADD`, `REMOVE` and
  `FACTORYRESET` always require one.
- **Wi-Fi fallback** — if the router is unreachable the setup network comes back
  (login required) while the device keeps retrying.
- **`FACTORYRESET`** command over ntfy; **FLASH-button** recovery on the board
  (press RST, release, then hold FLASH for ~2 s).
- Boot message includes the settings URL and firmware version.

## Download

| File | For |
| --- | --- |
| `sitecheck-v1.0.0-esp8266-4m2m.bin` | 4 MB boards — NodeMCU v2, Wemos D1 mini, most ESP-12 modules (2 MB LittleFS) |
| `sitecheck-v1.0.0-esp8266-1m64.bin` | 1 MB modules — ESP-01, older Sonoff (64 KB LittleFS) |

SHA-256 (also in `SHA256SUMS`):

```
fd7c20f7c2fb41f32228edec7327e97af25b8e0b97285cc960379c7f1e32b1f4  sitecheck-v1.0.0-esp8266-4m2m.bin
f7648c88f493a70a39b1a3ae1bd8b14cdc84ef9523236d53a4c54321d252bc07  sitecheck-v1.0.0-esp8266-1m64.bin
```

New board — erase, then write at `0x0`:

```bash
esptool --port <PORT> erase-flash
```

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 sitecheck-v1.0.0-esp8266-4m2m.bin
```

Full instructions: `flash.md`.

## Upgrading a device that is already set up

Write **without erasing** (skip `erase-flash`). Wi-Fi, topic, hosts and schedule
keep working. The device has no admin password or passphrase yet, so the
settings page says *Settings locked* and `ADD`/`REMOVE` reply *Passphrase
required*: press RST, release, hold FLASH within 2 s, and save the form with a
passphrase and an admin password.

Every ntfy command now needs the passphrase prefix — update any scripts or
saved phone messages.

## Build

ESP8266 Arduino core 3.1.2, ArduinoJson 7.4.3, ESPping 1.0.5, arduino-cli 1.5.1.

- `esp8266:esp8266:nodemcuv2:eesz=4M2M,xtal=80`
- `esp8266:esp8266:generic:eesz=1M64`

518,160 bytes each (~45% of the 1 MB application area), 36.8 KB static RAM.

## Known limitations

- Build-verified; the passphrase, admin login, FLASH-button recovery and
  `FACTORYRESET` have not yet been exercised on hardware.
- Settings page is plain HTTP; the admin login crosses the LAN unencrypted.
- ntfy and monitored-host TLS certificates are not verified.
- The FLASH button is GPIO0; boards without one (bare ESP-01) recover via
  `FACTORYRESET` or `erase-flash` only.
