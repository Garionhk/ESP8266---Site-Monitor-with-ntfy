# Building and distributing firmware — ESP32-S3 N16R8

How to produce a `.bin` other people can flash to their own ESP32-S3 N16R8
board (16 MB QIO flash, 8 MB OPI PSRAM — e.g. ESP32-S3-DevKitC-1-N16R8 and its
clones), and how they flash it.

Nothing user-specific is compiled in — Wi-Fi credentials, the ntfy topic and
token live in `/config.json` on the device, entered through the setup portal on
first boot. That is what makes the binary shareable.

---

## 1. Prerequisites (you, the builder)

[arduino-cli](https://arduino.github.io/arduino-cli/) plus the esp32 core (3.x)
and two libraries:

```bash
arduino-cli core install esp32:esp32
```

```bash
arduino-cli lib install ArduinoJson ESPping
```

`WebServer`, `DNSServer`, `ESPmDNS`, `HTTPClient` and `LittleFS` ship with the
core.

## 2. Build

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,PartitionScheme=custom --export-binaries ESP32S3-N16R8/SiteCheck
```

Outputs land in `ESP32S3-N16R8/SiteCheck/build/esp32.esp32.esp32s3/`:

| File | Flash at | Use |
| --- | --- | --- |
| `SiteCheck.ino.merged.bin` | `0x0` | bootloader + partition table + app, padded with 0xFF to 16 MB |
| `SiteCheck.ino.bin` | `0x10000` | app only — upgrades that keep settings and hosts |

The app is about 1.2 MB of a 6.25 MB slot.

### Release images

The merged image's 16 MB padding is too big to publish and erases LittleFS when
written. Build the same image without the padding (~1.2 MB) with the esptool
bundled in the core (paths for core 3.3.10 on macOS):

```bash
~/Library/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool --chip esp32s3 merge-bin -o dist/sitecheck-v2.0.0-esp32s3-n16r8-full.bin --flash-mode keep --flash-freq keep --flash-size keep 0x0 SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.bootloader.bin 0x8000 SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.partitions.bin 0xe000 ~/Library/Arduino15/packages/esp32/hardware/esp32/3.3.10/tools/partitions/boot_app0.bin 0x10000 SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.bin
```

Run it from `ESP32S3-N16R8/`, and copy `SiteCheck.ino.bin` to
`dist/sitecheck-v2.0.0-esp32s3-n16r8-app.bin`. The result is byte-identical to
the start of `SiteCheck.ino.merged.bin`.

### Flash layout

The partition table is [SiteCheck/partitions.csv](SiteCheck/partitions.csv),
which the esp32 core picks up automatically from the sketch folder (it overrides
whatever partition scheme the board menu says):

| Partition | Offset | Size |
| --- | --- | --- |
| nvs | `0x9000` | 20 KB |
| otadata | `0xE000` | 8 KB |
| app0 / app1 | `0x10000` / `0x650000` | 6.25 MB each |
| spiffs (LittleFS) | `0xC90000` | 3.4 MB |
| coredump | `0xFF0000` | 64 KB |

`PartitionScheme=custom` only stops arduino-cli from checking the app against
the 1.25 MB limit of the default 4 MB scheme. In the Arduino IDE, pick
*Tools → Partition Scheme → Custom*, *Flash Size → 16MB*, *PSRAM → OPI PSRAM*.

`PSRAM=opi` matters: the N16R8's PSRAM is octal, and a QSPI setting fails to
initialise it (the sketch would still run, just without the extra 8 MB).

## 3. Confirm the binary carries no secrets

```bash
strings ESP32S3-N16R8/SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.bin | grep -iE "your-ssid-here|your-password|your-topic"
```

Substitute your own network name, password and ntfy topic. Expect **no output**.
Sanity check — this should print a non-zero count:

```bash
strings ESP32S3-N16R8/SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.bin | grep -c "SiteCheck setup"
```

Publish checksums alongside the binaries:

```bash
shasum -a 256 ESP32S3-N16R8/SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.merged.bin ESP32S3-N16R8/SiteCheck/build/esp32.esp32.esp32s3/SiteCheck.ino.bin
```

Published images live in [dist/](dist) with a `SHA256SUMS` file.

---

## 4. Instructions for the person flashing

They need [esptool](https://github.com/espressif/esptool):

```bash
pip install esptool
```

### Which USB port

DevKitC-style N16R8 boards have two USB-C ports. Either can flash, but serial
logs (115200 baud) come out of the **UART / COM** port, because the build leaves
*USB CDC On Boot* disabled.

| OS | Typically |
| --- | --- |
| macOS | `/dev/cu.usbserial-*`, `/dev/cu.wchusbserial*` (UART port) or `/dev/cu.usbmodem*` (native USB port) |
| Linux | `/dev/ttyUSB0` (UART) or `/dev/ttyACM0` (native) |
| Windows | `COM3`, `COM4`, … |

If the board will not enter the bootloader on its own, hold **BOOT**, tap
**RST**, release BOOT, then run esptool.

### First install: erase, then write the full image

```bash
esptool --chip esp32s3 --port /dev/cu.usbserial-0001 erase-flash
```

```bash
esptool --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 921600 write-flash 0x0 sitecheck-v2.0.0-esp32s3-n16r8-full.bin
```

Older esptool (v4 and earlier) uses underscores: `erase_flash`, `write_flash`.
Press **RST** afterwards if the board does not restart by itself.

### Upgrade: app only

Write only the app, without erasing — settings and hosts are kept. (Never use
the core's 16 MB `SiteCheck.ino.merged.bin` for this: its padding overwrites
the LittleFS partition.)

```bash
esptool --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 921600 write-flash 0x10000 sitecheck-v2.0.0-esp32s3-n16r8-app.bin
```

### First boot

1. Power the board.
2. Join the open Wi-Fi network **`SiteCheck-XXXX`** (suffix is the last two
   bytes of that board's MAC).
3. The setup page should open automatically. If not, browse to
   **http://192.168.4.1**.
4. Enter Wi-Fi, an ntfy topic, a command passphrase, an admin password,
   timezone and schedule → **Save and restart**.

The device then publishes a boot message to the topic with the address of its
settings page (also `http://sitecheck.local`, login **`admin`**).

Forgot the admin password or moved networks? Press **RST**, release it, then
immediately press and hold **BOOT** for ~2 s — the setup portal opens with no
login. (Holding BOOT *through* the reset enters the bootloader instead.)

### GUI alternative

[ESP Web Tools](https://esphome.github.io/esp-web-tools/) or Espressif's
[web flasher](https://espressif.github.io/esptool-js/) flash from Chrome/Edge
over Web Serial: add the full image at offset `0x0`.

---

## 5. Notes

- **Wipe settings and hosts, keep firmware:**

  ```bash
  esptool --chip esp32s3 --port /dev/cu.usbserial-0001 erase-region 0xC90000 0x360000
  ```

- `<passphrase> FACTORYRESET` over ntfy deletes the configuration and keeps the
  hosts, same as on ESP8266.
- Settings are not portable between the ESP8266 and ESP32-S3 builds by flashing;
  set the new board up through the portal and re-`ADD` the hosts.
