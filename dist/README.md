# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only update **within IDF5** (keeps NVS) |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only update **within IDF5** (keeps NVS) |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) — **required from v1.1.20** |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) — **required from v1.1.20** |

## Current app build

- Mark: **v1.1.28** (`FW_MARK`)
- Platform: pioarduino 55.03.311 (Arduino 3.3.11 / IDF 5.5.5) — see `docs/idf5_adapt_20260919.md`
- **First jump from main v1.1.20**: do **not** use Web OTA — see `docs/upgrade_idf5_from_1120.md`
- Power: S3 defaults to 160 MHz + WiFi MIN_MODEM sleep — see `docs/power_save.md`
- Lock fix: NMEA filter keeps RMC+ZDA — see `docs/gps_lock_nmea_fix_20260919.md`
- C3 partitions: `partitions/default_ota_1750k.csv` (~1.75 MB app slots). New table only applies after full `firmware_merged_0x0.bin`.

## Verify before flash / Web OTA

```text
wc -c firmware_esp32s3.bin   # expect size in SHA256SUMS
sha256sum firmware_esp32s3.bin
```

Truncated gh-proxy downloads often “succeed” then roll back.

## Keep WiFi / settings (only after already on IDF5)

1. Flash only `firmware.bin` / `firmware_esp32s3.bin` at **`0x10000`**
2. Turn **OFF** erase-flash
3. Do not rewrite bootloader/partitions unless the table changed

## China mirror example

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/idf5-adapt-a05e/dist/merged_firmware_esp32s3_n16r8_0x0.bin
```
