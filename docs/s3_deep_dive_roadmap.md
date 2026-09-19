# S3 特性深挖路线图（PPS 硬件捕获 / PSRAM 历史 / OTA / 外部时钟）

> 状态（2026-09-19）：**IDF5 已合入 main（v1.1.28）**；OTA 再验收通过；RMT 默认 EN=0 待板测；ExtClock 待购件；PSRAM history 已取消。  
> 总览：[CURRENT.md](CURRENT.md)  
> 相关：[esp32s3_devkitc1_hw.md](esp32s3_devkitc1_hw.md)、[idf5_adapt_20260919.md](idf5_adapt_20260919.md)

## 0. 现状基线（v1.1.28）

| S3 特性 | 现状 |
|---|---|
| 双核 LX7 | ✅ `task-time` 独占 core 1；默认运行 **160 MHz**（可编回 240） |
| 16MB QIO flash | ✅ `default_16MB`；Web OTA 写下一 app 槽 |
| 8MB OPI PSRAM | 闲置（history 已取消） |
| 温度传感器 | ✅ `temperatureRead()`（偶发首读失败有 lazy retry） |
| RMT | RGB 用 TX；**PPS RX 代码就绪，`GPS_PPS_RMT_EN=0`** |
| UART | 调试 + GNSS；第 3 路闲置 |
| USB-OTG | 不用（`CDC_ON_BOOT=0`，走 UART 座） |

## 1. RMT RX 硬件捕获 PPS —— **IDF5 路径就绪，待 EN=1 板测**

- **IDF4（已封存）**：legacy RMT 在 S3 上 RX 只推空帧；见历史探针记录。  
- **IDF5（v1.1.24+，现随 v1.1.28 在 main）**：`driver/rmt_rx.h`（`rmt_new_rx_channel` / `rmt_receive` / `on_recv_done`）。默认 EN=0，生产路径仍为 GPIO ISR。  
- **验收准则**：`EN=1` 后 `ppsRmt.idfDataFrames` 随 PPS 增长；`deltaMeanUs` 合理；长测 LCK 不抖。

## 2. PSRAM `/history` —— **已取消（v1.1.14）**

## 3. OTA 双分区 —— **已实现；IDF5 上再验收通过（v1.1.28）**

- `POST /ota` + `/cfg` UI；`Update`；启动后 `esp_ota_mark_app_valid_cancel_rollback`（≥30 s）。  
- OTA busy 拒 NTP（KoD `RSTR`）；magic + chip_id 校验。  
- **自 IDF4 首迁须整片烧录**，不能依赖 Web OTA（见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)）。

## 4. 外部高品质时钟 —— **接口已落地，待购件**

- 见 [ext_clock_design.md](ext_clock_design.md)；默认 `EXT_RTC_EN=0`。

## 5. 明确不做

NTS / 加密 NTP、触摸 / LCD、802.11mc。

## 6. 排序备忘

**① RMT `EN=1` 板测 → ② ExtClock 购件开 EN**（OTA / IDF5 / 降功耗已完成）
