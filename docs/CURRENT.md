# 当前基线（与代码一致）

> 更新日期：2026-09-19  
> 工作分支：`cursor/work-a05e` · 固件 **v1.1.33**（`GPS_PPS_RMT_EN=1` 板测构建）  
> `main` 仍为 v1.1.28（RMT 默认关），合入前以板测结果为准。

本文是文档入口；**与代码冲突时以源码与本页为准**。

## 平台与构建

| 项 | 实际 |
|---|---|
| Platform | pioarduino **55.03.311** = Arduino-ESP32 **3.3.11** / ESP-IDF **5.5.5** |
| 入口 | `platformio.ini` → `[idf5]`；S3：`pio run -e esp32-s3` |
| 产物 | `dist/` · 见 [dist/README.md](../dist/README.md) |

## 功能现状

| 能力 | 状态 |
|---|---|
| GNSS + LocalClock + Stratum-1 NTP | ✅ |
| NMEA | ✅ **GGA + RMC + ZDA** + PCAS persist-skip |
| Web OTA / S3 160 MHz + modem sleep | ✅ |
| **RMT PPS** | **v1.1.33：`GPS_PPS_RMT_EN=1`** — [rmt_pps_retest_v1133.md](rmt_pps_retest_v1133.md)（v1.1.32 armed OK，符号仍全零 → 本版 dump+修） |
| 外置 RTC | `EXT_RTC_EN=0`，待购件 |

## 升级

- 自 IDF4（≤ v1.1.21）：整片烧录 — [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)  
- 已在 IDF5：可用 `firmware_esp32s3.bin` @ `0x10000` 升到 v1.1.33

## 文档索引

| 文档 | 用途 |
|---|---|
| [rmt_pps_retest_v1133.md](rmt_pps_retest_v1133.md) | **RMT 板测流程（给执行 AI）** |
| [rmt_pps_retest_v1132_result_20260919.md](rmt_pps_retest_v1132_result_20260919.md) | v1.1.32 FAIL：零时长符号 |
| [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md) | S3 v1.1.28 冒烟 PASS |
| [idf5_adapt_20260919.md](idf5_adapt_20260919.md) | 平台迁移 |
| [s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) | 后续项 |
