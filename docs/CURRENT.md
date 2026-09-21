# 当前基线（与代码一致）

> 更新日期：2026-09-21  
> **本分支 `cursor/holdover-options-a05e` = v1.1.43**（守时档扩展 + HLD 时钟环补样）· **Hold 30m 精度长测 PASS** — [holdover_precision_v1143_result_20260921.md](holdover_precision_v1143_result_20260921.md)  
> **`main`**：已合入至 failover brief 线；本分支待合入升到 v1.1.43  
> 现网 S3：`fwMark=v1.1.43` · Hold 30m · LCK / GPSS  
> v1.1.42 Hold 5m / Refuse PASS — [fw_flash_v1142_result_20260921.md](fw_flash_v1142_result_20260921.md) · RMT 结案 FAIL — [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)

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
| GNSS + LocalClock + Stratum-1 NTP | ✅ GPIO PPS |
| NMEA | ✅ **GGA + RMC + ZDA** + PCAS persist-skip |
| Web OTA / S3 160 MHz + modem sleep | ✅ |
| **RMT PPS** | ❌ 板测搁置（EN=0）— [CLOSED](rmt_pps_board_test_CLOSED_20260919.md) |
| Web 调试 log | ✅ `GET /debug/log?pass=` — HLD 进度 30 s 一行（v1.1.43） |
| 时钟长测环 | ✅ **v1.1.43** PPS 停转时墙钟 1 Hz 补样（守时段可录）— [clock_trace.md](clock_trace.md) |
| GPS 异常守时档 | ✅ Refuse / 30s / 5m / **15m / 30m / 1h / 2h** · 30m 精度现场 PASS — [验收](holdover_precision_v1143_result_20260921.md) |
| PPS 断电恢复 | ✅ v1.1.42 现场 PASS — [验收](fw_flash_v1142_result_20260921.md) |
| 外置 RTC | `EXT_RTC_EN=0`，待购件 |

## 升级

- 自 IDF4（≤ v1.1.21）：整片烧录 — [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)  
- 已在 IDF5：OTA `firmware_esp32s3.bin` @ `0x10000` 升到 **v1.1.43**（本分支）

## 文档索引

| 文档 | 用途 |
|---|---|
| [holdover_precision_v1143_result_20260921.md](holdover_precision_v1143_result_20260921.md) | **v1.1.43 Hold 30m 守时精度 PASS** |
| [holdover_precision_v1143.md](holdover_precision_v1143.md) | 任务书（已完成） |
| [rmt_pps_agent_brief.md](rmt_pps_agent_brief.md) | 执行侧入口（无进行中任务） |
| [fw_flash_v1142_result_20260921.md](fw_flash_v1142_result_20260921.md) | **v1.1.42 Hold 5m + Refuse PASS** |
| [clock_trace.md](clock_trace.md) | PSRAM 时钟长测环 API |
| [debug_log.md](debug_log.md) | RAM 调试日志 |
| [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md) | v1.1.41 恢复 FAIL 史档 |
| [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md) | RMT 板测结案 |
| [s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) | 后续项 |
