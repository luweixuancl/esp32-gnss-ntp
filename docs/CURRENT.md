# 当前基线（与代码一致）

> 更新日期：2026-09-21  
> **本分支线 = v1.1.41**（`/cfg` 时钟长测按钮随 IDLE/REC/STOP）；**`main` 仍是 v1.1.39**  
> **进行中**：Hold 5m 拔模块电源失效链（Alpine / 无 ntpdate）— [gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md)  
> 现网 S3 Web OTA **PASS**（v1.1.40→41）+ 按钮 A–F **PASS** — [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md)  
> 时钟长测板测 **PASS**（v1.1.38）— [clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md) · 83 min 全量分析 — [clock_trace_83min_20260920.md](clock_trace_83min_20260920.md)  
> 12.65 h 只存第 1 页事故 — [clock_trace_analysis_20260920.md](clock_trace_analysis_20260920.md) · RMT 结案 FAIL — [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)

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
| 时钟长测环 | ✅ **v1.1.41** `/cfg` 按钮随状态机（现场 A–F PASS）— [clock_trace.md](clock_trace.md) · [验收](fw_flash_v1141_result_20260920.md) |
| 外置 RTC | `EXT_RTC_EN=0`，待购件 |

## 升级

- 自 IDF4（≤ v1.1.21）：整片烧录 — [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)  
- 已在 IDF5：可用 `firmware_esp32s3.bin` @ `0x10000` 升到 **v1.1.41**

## 文档索引

| 文档 | 用途 |
|---|---|
| [gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md) | **进行中**：Hold 5m 拔模块电源（Alpine / 无 ntpdate） |
| [rmt_pps_agent_brief.md](rmt_pps_agent_brief.md) | 执行侧入口 |
| [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md) | **v1.1.41 S3 OTA + `/cfg` 按钮 PASS** |
| [fw_flash_v1141.md](fw_flash_v1141.md) | 辅助 AI 任务书（已完成） |
| [fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md) | **v1.1.40 S3 OTA + 一键全量 PASS** |
| [clock_trace_83min_20260920.md](clock_trace_83min_20260920.md) | 刷前 83 min 全量分析（v1.1.39，LCK 100%） |
| [fw_flash_v1140.md](fw_flash_v1140.md) | v1.1.40 烧录任务书（已完成） |
| [clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md) | **v1.1.38 时钟环板测 PASS** |
| [clock_trace.md](clock_trace.md) | PSRAM 时钟长测环 API（v1.1.40 无参=全量） |
| [ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md) | **v1.1.36 Web OTA PASS** |
| [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md) | RMT 板测结案 |
| [debug_log.md](debug_log.md) | 免串口 RAM log |
| [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md) | S3 v1.1.28 冒烟 PASS |
| [idf5_adapt_20260919.md](idf5_adapt_20260919.md) | 平台迁移 |
| [s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) | 后续项 |
