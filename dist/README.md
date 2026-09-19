# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only within IDF5 |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only within IDF5 |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) |

## Current build

- Mark: **v1.1.36** · [GitHub Release](https://github.com/luweixuancl/esp32-gnss-ntp/releases/tag/v1.1.36)
- `GPS_PPS_RMT_EN=0`（RMT 板测搁置）— [`docs/rmt_pps_board_test_CLOSED_20260919.md`](../docs/rmt_pps_board_test_CLOSED_20260919.md)
- Web OTA 往返 **PASS** — [`docs/ota_deploy_v1136_20260919.md`](../docs/ota_deploy_v1136_20260919.md)
- Debug log: [`docs/debug_log.md`](../docs/debug_log.md)
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）
- On **`main`** / tag **`v1.1.36`**

## Verify

```text
wc -c firmware_esp32s3.bin   # 1091520
sha256sum -c SHA256SUMS
```

## China mirror

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/firmware_esp32s3.bin
```
