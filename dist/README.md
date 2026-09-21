# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only within IDF5 |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only within IDF5 |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) |

## Current build

- Mark: **v1.1.43**（守时档 15m/30m/1h/2h + HLD 时钟环 1 Hz 补样）— [`docs/holdover_precision_v1143.md`](../docs/holdover_precision_v1143.md)
- `GPS_PPS_RMT_EN=0` — [`docs/rmt_pps_board_test_CLOSED_20260919.md`](../docs/rmt_pps_board_test_CLOSED_20260919.md)
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）
- Branch: `cursor/holdover-options-a05e`

## Verify

```text
wc -c firmware_esp32s3.bin   # 1104096
sha256sum -c SHA256SUMS
```

## China mirror

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/holdover-options-a05e/dist/firmware_esp32s3.bin
```
