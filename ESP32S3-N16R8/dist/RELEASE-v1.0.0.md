# SiteCheck v1.0.0 — ESP32-S3 N16R8

First ESP32-S3 build of SiteCheck. Same firmware and features as the ESP8266
v1.0.0 release, ported to the ESP32-S3 N16R8 (16 MB QIO flash, 8 MB OPI PSRAM),
e.g. ESP32-S3-DevKitC-1-N16R8 and its clones.

## Differences from the ESP8266 build

- Recovery button is **BOOT** (GPIO0): press RST, release, then hold BOOT for
  ~2 s to open the setup portal with no login.
- 16 MB flash layout: 2 × 6.25 MB app slots, 3.4 MB LittleFS.
- Everything else — setup portal, LAN settings page, ntfy commands, passphrase,
  `FACTORYRESET` — behaves identically.

## Download

| File | Flash at | For |
| --- | --- | --- |
| `sitecheck-v1.0.0-esp32s3-n16r8-full.bin` | `0x0` | New board: bootloader + partition table + app |
| `sitecheck-v1.0.0-esp32s3-n16r8-app.bin` | `0x10000` | Upgrading a board already running SiteCheck |

SHA-256 (also in `SHA256SUMS`):

```
2940f0942c9bdccae6e63b407a45e4c15a345742416b2ed77bd93deb310ae70e  sitecheck-v1.0.0-esp32s3-n16r8-full.bin
d31725161915b48dd0873ba05fbbf7415dd9a691935535e213942d911dd3acc2  sitecheck-v1.0.0-esp32s3-n16r8-app.bin
```

New board — erase, then write at `0x0`:

```bash
esptool --chip esp32s3 --port <PORT> erase-flash
```

```bash
esptool --chip esp32s3 --port <PORT> --baud 921600 write-flash 0x0 sitecheck-v1.0.0-esp32s3-n16r8-full.bin
```

Upgrade, keeping settings and hosts — app only, no erase:

```bash
esptool --chip esp32s3 --port <PORT> --baud 921600 write-flash 0x10000 sitecheck-v1.0.0-esp32s3-n16r8-app.bin
```

Full instructions: `../flash.md`.

## Build

esp32 Arduino core 3.3.10, ArduinoJson 7.4.3, ESPping 1.0.5, arduino-cli 1.5.1.

- `esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,PartitionScheme=custom`
- Partition table from `SiteCheck/partitions.csv`

App: 1,173,888 bytes (~18% of a 6.25 MB slot), 53.8 KB static RAM.
The full image is the core's merged image without its 0xFF padding to 16 MB
(byte-identical otherwise), so writing it does not touch the LittleFS region.

## Known limitations

- Build-verified only; not yet exercised on ESP32-S3 hardware.
- Settings page is plain HTTP; the admin login crosses the LAN unencrypted.
- ntfy and monitored-host TLS certificates are not verified.
- Serial logs (115200) come out of the UART/COM USB port, not the native USB
  port.
