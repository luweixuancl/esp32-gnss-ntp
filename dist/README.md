# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only within IDF5 |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only within IDF5 |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) |

## Current build

- Mark: **v1.1.39**（时钟环仅二进制；CSV 由 CLI）— [`docs/clock_trace.md`](../docs/clock_trace.md)
- 板测 PASS（v1.1.38）— [`docs/clock_trace_boardtest_20260919.md`](../docs/clock_trace_boardtest_20260919.md)
- `GPS_PPS_RMT_EN=0` — [`docs/rmt_pps_board_test_CLOSED_20260919.md`](../docs/rmt_pps_board_test_CLOSED_20260919.md)
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）
- Branch: `cursor/clock-psram-ring-a05e`

## Verify

```text
wc -c firmware_esp32s3.bin   # 1099856
sha256sum -c SHA256SUMS
```

## China mirror

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/clock-psram-ring-a05e/dist/firmware_esp32s3.bin
```
