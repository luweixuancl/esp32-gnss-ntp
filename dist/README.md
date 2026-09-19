# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only within IDF5 |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only within IDF5 |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) |

## Current build

- Mark: **v1.1.34**
- **`GPS_PPS_RMT_EN=1`** + RAM debug log — [`docs/rmt_pps_retest_v1133.md`](../docs/rmt_pps_retest_v1133.md) · [`docs/debug_log.md`](../docs/debug_log.md)
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）
- Branch tip: `cursor/work-a05e`

## Verify

```text
wc -c firmware_esp32s3.bin
sha256sum -c SHA256SUMS
```

## China mirror (work branch)

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```
