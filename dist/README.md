# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only **within IDF5**（保 NVS） |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only **within IDF5**（保 NVS） |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | 整片（清 NVS）— **自 ≤v1.1.21 迁入必须** |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | 整片（清 NVS）— **自 ≤v1.1.21 迁入必须** |

## Current build

- Mark: **v1.1.28**
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）— [docs/CURRENT.md](../docs/CURRENT.md)
- First jump from IDF4：勿用 Web OTA — [docs/upgrade_idf5_from_1120.md](../docs/upgrade_idf5_from_1120.md)
- Power / lock / board：见 `docs/power_save.md`、`docs/gps_lock_nmea_fix_20260919.md`、`docs/board_test_s3_idf5_20260919.md`
- C3：`partitions/default_ota_1750k.csv`；新表仅整片刷入后生效

## Verify

```text
wc -c firmware_esp32s3.bin
sha256sum -c SHA256SUMS
```

## China mirror

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/merged_firmware_esp32s3_n16r8_0x0.bin
```
