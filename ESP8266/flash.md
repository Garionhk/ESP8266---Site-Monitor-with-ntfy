# Building and distributing firmware

How to produce a `.bin` other people can flash to their own ESP8266, and how
they flash it.

Nothing user-specific is compiled in — Wi-Fi credentials, the ntfy topic and
token live in `/config.json` on the device, entered through the setup portal on
first boot. That is what makes the binary shareable.

---

## 1. Prerequisites (you, the builder)

[arduino-cli](https://arduino.github.io/arduino-cli/) plus the ESP8266 core and
two libraries:

```bash
arduino-cli core install esp8266:esp8266
```

```bash
arduino-cli lib install ArduinoJson ESPping
```

`ESP8266WebServer`, `DNSServer` and `ESP8266mDNS` ship with the core.

## 2. Build

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2:eesz=4M2M,xtal=80 --export-binaries ESP8266/SiteCheck
```

The binary lands at:

```
ESP8266/SiteCheck/build/esp8266.esp8266.nodemcuv2/SiteCheck.ino.bin
```

About 500 KB — roughly 45% of the 1 MB application area, so there is plenty of
headroom.

`--export-binaries` is what writes the `.bin` into the sketch folder; without
it arduino-cli builds into a temporary directory and throws the result away.

### Flash layout matters

The FQBN bakes the flash size and filesystem split into the image, so **one
binary is not universal**. The command above targets a 4 MB board with a 2 MB
LittleFS partition, which covers NodeMCU v2, Wemos D1 mini and most modern
ESP-12 modules.

For a 1 MB module (ESP-01, older Sonoff), build a second image:

```bash
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=1M64 --export-binaries ESP8266/SiteCheck
```

That leaves 64 KB for LittleFS, which is ample — the three JSON files are a few
kilobytes. Name the outputs clearly when you publish them, e.g.
`sitecheck-v2.0.0-esp8266-4m2m.bin` and `sitecheck-v2.0.0-esp8266-1m64.bin`.

## 3. Confirm the binary carries no secrets

Do this before publishing. It is the whole point of keeping configuration off
the compile-time path:

```bash
strings ESP8266/SiteCheck/build/esp8266.esp8266.nodemcuv2/SiteCheck.ino.bin | grep -iE "your-ssid-here|your-password|your-topic"
```

Substitute your own network name, password and ntfy topic. Expect **no output**.

Sanity-check that `strings` is actually reading the file, so an empty result
means "clean" rather than "nothing was scanned" — this should print a non-zero
count:

```bash
strings ESP8266/SiteCheck/build/esp8266.esp8266.nodemcuv2/SiteCheck.ino.bin | grep -c "SiteCheck setup"
```

Publish a checksum alongside the binary:

```bash
shasum -a 256 ESP8266/SiteCheck/build/esp8266.esp8266.nodemcuv2/SiteCheck.ino.bin
```

---

## 4. Instructions for the person flashing

They need [esptool](https://github.com/espressif/esptool) — no Arduino
toolchain, no source:

```bash
pip install esptool
```

### Find the port

| OS | Typically |
| --- | --- |
| macOS | `/dev/cu.usbserial-0001` or `/dev/cu.SLAB_USBtoUART` |
| Linux | `/dev/ttyUSB0` |
| Windows | `COM3`, `COM4`, … |

If nothing appears, the USB-serial driver is missing — [CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
for Silicon Labs bridges, [CH340](https://www.wch-ic.com/downloads/CH341SER_ZIP.html)
for WCH. A charge-only USB cable produces the same symptom, so try another
cable before chasing drivers.

### Erase, then write

Erasing first avoids inheriting a stale filesystem from whatever was on the
board before:

```bash
esptool --port /dev/cu.usbserial-0001 erase-flash
```

```bash
esptool --port /dev/cu.usbserial-0001 --baud 460800 write-flash 0x0 sitecheck-v2.0.0-esp8266-4m2m.bin
```

Older esptool (v4 and earlier) uses underscores: `erase_flash`, `write_flash`.

Some boards need the FLASH/GPIO0 button held during the first connect; most
dev boards auto-reset into the bootloader and need nothing.

### First boot

1. Power the board.
2. Join the open Wi-Fi network **`SiteCheck-XXXX`** (suffix is the last two
   bytes of that board's MAC, so it differs per device).
3. The setup page should open automatically. If not, browse to
   **http://192.168.4.1**.
4. Enter Wi-Fi, an ntfy topic, a command passphrase, an admin password,
   timezone and schedule → **Save and restart**.

The device joins the network and publishes a boot message to the topic
containing the address of its settings page. That page stays available on the
LAN afterwards, at the device address or `http://sitecheck.local`, protected by
the admin password (username **`admin`**). Every ntfy command must start with
the passphrase, e.g. `mysecret STATUS`.

Forgot the admin password or moved networks? Press **RST**, release it, then
immediately press and hold **FLASH** for ~2 s — the setup portal opens with no
login.

Watch progress on serial at **115200 baud** if anything goes wrong.

### GUI alternatives

For people who would rather not use a terminal:

- [NodeMCU PyFlasher](https://github.com/marcelstoer/nodemcu-pyflasher) — pick
  the `.bin`, pick the port, click Flash
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) — flashes from a
  Chrome or Edge browser over Web Serial, no install at all

Both need the same erase-then-write behaviour; PyFlasher exposes it as an
"erase flash: yes" option.

---

## 5. Notes

- **Reflashing does not erase settings.** LittleFS survives a firmware write,
  so an upgrade keeps the configuration, monitored hosts and poll cursor. Use
  `erase-flash` for a true factory reset.
- **In-device factory reset:** press RST then hold FLASH for ~2 s (setup
  portal, stored settings pre-filled), or send `<passphrase> FACTORYRESET`
  over ntfy (configuration deleted, hosts kept). To wipe everything over USB
  instead:

  ```bash
  esptool --port /dev/cu.usbserial-0001 erase-region 0x200000 0x1FA000
  ```

  That is the LittleFS region for the 4 MB / 2 MB layout; it wipes the
  configuration *and* the monitored hosts, and leaves the firmware in place.
  Offsets differ for other flash layouts — check `spiffs_start` and
  `spiffs_end` in the core's `boards.txt`.
- **Do not distribute `config.json`.** It never leaves the device, and there is
  no build step that would embed it.
