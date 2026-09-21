# 当前基线（与代码一致）

> 更新日期：2026-09-21  
> **本分支 `cursor/pps-resume-deadlock-6c51` = v1.1.42**（PPS 流重启清环，修 ACQ 死锁）  
> **`main` = v1.1.41**（PR #11 已合入 clock-trace 线）  
> **进行中**：OTA v1.1.42 + Hold 5m 复测 — [fw_flash_v1142.md](fw_flash_v1142.md) · 计划 [gps_failover_fix_plan_v1142.md](gps_failover_fix_plan_v1142.md)  
> 现网仍是 **v1.1.41**，第 6 项恢复 FAIL — [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)  
> v1.1.41 S3 OTA + 按钮 A–F **PASS** — [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md)  
> RMT 结案 FAIL — [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)

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
| Web OTA / S3 160 MHz + modem sleep | ✅ **v1.1.36 OTA 往返 PASS** |
| **RMT PPS** | ❌ 板测搁置（EN=0）— [CLOSED](rmt_pps_board_test_CLOSED_20260919.md) |
| Web 调试 log | ✅ `GET /debug/log?pass=` — [debug_log.md](debug_log.md) |
| 时钟长测环 | ✅ **v1.1.41** `/cfg` 按钮随状态机（现场 A–F PASS）— [clock_trace.md](clock_trace.md) |
| PPS 断电恢复 | ⚠️ v1.1.41 现场死锁 ACQ；**v1.1.42 代码已修，待 OTA 复测** |
| 外置 RTC | `EXT_RTC_EN=0`，待购件 |

## 升级

- 自 IDF4（≤ v1.1.21）：整片烧录 — [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)  
- 已在 IDF5：可用 `firmware_esp32s3.bin` @ `0x10000` 升到 **v1.1.42**（本分支）

## 文档索引

| 文档 | 用途 |
|---|---|
| [fw_flash_v1142.md](fw_flash_v1142.md) | **进行中**：OTA v1.1.42 + Hold 5m 复测 |
| [gps_failover_fix_plan_v1142.md](gps_failover_fix_plan_v1142.md) | 修正计划（根因 + 加固点 + 验收） |
| [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md) | v1.1.41 第 6 项 FAIL |
| [gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md) | 上一轮任务书（已跑完） |
| [rmt_pps_agent_brief.md](rmt_pps_agent_brief.md) | 执行侧入口 |
| [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md) | **v1.1.41 S3 OTA + `/cfg` 按钮 PASS** |
| [clock_trace.md](clock_trace.md) | PSRAM 时钟长测环 API |
| [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md) | RMT 板测结案 |
| [s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) | 后续项 |
