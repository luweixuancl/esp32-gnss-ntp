# Firmware drops (app images)

| File | Chip | Flash offset | Notes |
|------|------|--------------|-------|
| `firmware.bin` | ESP32-C3 | `0x10000` | App-only within IDF5 |
| `firmware_esp32s3.bin` | ESP32-S3 | `0x10000` | App-only within IDF5 |
| `firmware_merged_0x0.bin` | ESP32-C3 | `0x0` | Full image (wipes NVS) |
| `merged_firmware_esp32s3_n16r8_0x0.bin` | ESP32-S3 | `0x0` | Full image (wipes NVS) |

## Current build

- Mark: **v1.1.43**（守时档 15m/30m/1h/2h + HLD 时钟环 1 Hz 补样）  
- 现场：Hold 30m 守时精度 ≈1 ms/30 min — [`docs/holdover_precision_v1143_result_20260921.md`](../docs/holdover_precision_v1143_result_20260921.md)  
- `GPS_PPS_RMT_EN=0` — [`docs/rmt_pps_board_test_CLOSED_20260919.md`](../docs/rmt_pps_board_test_CLOSED_20260919.md)  
- Platform: pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）  
- Branch / tag: **`main` / `v1.1.43`**

## Verify

```text
wc -c firmware_esp32s3.bin   # 1104096
sha256sum -c SHA256SUMS
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `firmware_esp32s3.bin` | 1104096 | `784a63c9375078dc0a42f2e56ea45286a557f5c5bdb4816d8bb2662e625b1dc7` |
| `firmware.bin` | 1213152 | `d4f2346084bea239d42b59d6c5026d6816501ba4154049f53fcaf187f590fe48` |

## China mirror

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/firmware_esp32s3.bin
```

或从 [Releases · v1.1.43](https://github.com/luweixuancl/esp32-gnss-ntp/releases/tag/v1.1.43) 下载。
