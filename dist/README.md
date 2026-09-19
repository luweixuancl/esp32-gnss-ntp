# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | **Preferred update** (keeps NVS) |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | **Preferred update** (keeps NVS) |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | First install only (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | First install only (wipes NVS) |

## Current app build

- Mark: **v1.1.23** (`FW_MARK`)
- Platform: pioarduino 55.03.311 (Arduino 3.3.11 / IDF 5.5.5) — see `docs/idf5_adapt_20260919.md`

## Verify before Web OTA

Confirm the downloaded app image size/hash (truncated downloads often “succeed” then roll back to the previous version):

```text
# after download
wc -c firmware.bin
md5sum firmware.bin
```

Expected app sizes are recorded next to the bins in `dist/SHA256SUMS` on this branch.

## Keep WiFi / settings

NVS lives outside the app image. For upgrades:

1. Flash only `firmware.bin` / `firmware_esp32s3.bin` at **`0x10000`**
2. Turn **OFF** erase-flash / 全片擦除
3. Do not rewrite bootloader/partitions unless the table changed

## China mirror example

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/<branch>/dist/firmware.bin
```
