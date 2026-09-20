# S3 特性深挖路线图（PPS 硬件捕获 / PSRAM 历史 / OTA / 外部时钟）

> 状态（2026-09-20）：本分支 **v1.1.40**（`GPS_PPS_RMT_EN=0`）；现网 S3 OTA PASS + 一键全量 PASS；RMT 结案 FAIL；ExtClock 待购件。  
> 总览：[CURRENT.md](CURRENT.md)  
> 相关：[esp32s3_devkitc1_hw.md](esp32s3_devkitc1_hw.md)、[idf5_adapt_20260919.md](idf5_adapt_20260919.md)、[clock_trace.md](clock_trace.md)

## 0. 现状基线（v1.1.39）

| S3 特性 | 现状 |
|---|---|
| 双核 LX7 | ✅ `task-time` 独占 core 1；默认运行 **160 MHz**（可编回 240） |
| 16MB QIO flash | ✅ `default_16MB`；Web OTA 写下一 app 槽 |
| 8MB OPI PSRAM | ✅ 时钟长测环（`CLOCK_TRACE`，见 [clock_trace.md](clock_trace.md)） |
| 温度传感器 | ✅ `temperatureRead()`（偶发首读失败有 lazy retry） |
| RMT | RGB 用 TX；**PPS RX 代码保留，`GPS_PPS_RMT_EN=0`（板测搁置）** |
| UART | 调试 + GNSS；第 3 路闲置 |
| USB-OTG | 不用（`CDC_ON_BOOT=0`，走 UART 座） |

## 1. RMT RX 硬件捕获 PPS —— **板测搁置（EN=0）**

- IDF5 路径已写完并多轮板测（v1.1.29–35）：armed 正常，但 **符号时长恒 0**（DMA/关 RGB/rtc_deinit 后仍如此）。  
- **结案**：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)  
- 生产路径：GPIO ISR + LocalClock（已验证 LCK / stratum 1）。  
- 再开 EN 前：独立最小 sketch 或 GPIO 回环自测，勿在整机盲迭代。

## 2. PSRAM 时钟长测环 —— **已实现并板测 PASS（v1.1.40 无参=全量）**

- start/stop 状态机，**仅 Stopped 可拉**；下载期停 NTP。  
- 板测：[clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md)（10 min、647 样本）。  
- v1.1.39：设备端 **去掉 CSV**；CLI 本地转 CSV。  
- **v1.1.40**：无参 `GET /debug/clock/data` = 一次全量；现网 101 样本 DONE — [fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md)。  
- 见 [clock_trace.md](clock_trace.md)。旧「通用 /history」仍取消。

## 3. OTA 双分区 —— **已实现；IDF5 自动往返 PASS（v1.1.36）**

- `POST /ota` + `/cfg` UI；`Update`；启动后 `esp_ota_mark_app_valid_cancel_rollback`（≥30 s）。  
- OTA busy 拒 NTP（KoD `RSTR`）；magic + chip_id 校验。  
- **v1.1.35→36 自动部署 PASS** — [ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md)。  
- **自 IDF4 首迁须整片烧录**（见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)）。

## 4. 外部高品质时钟 —— **接口已落地，待购件**

- 见 [ext_clock_design.md](ext_clock_design.md)；默认 `EXT_RTC_EN=0`。

## 5. 明确不做

NTS / 加密 NTP、触摸 / LCD、802.11mc。

## 6. 排序备忘

**① ExtClock 购件开 EN**（RMT 搁置；OTA / IDF5 / 降功耗 / 时钟环已完成）
