# 当前基线（与代码一致）

> 更新日期：2026-09-19  
> 分支：`main` / 工作分支 `cursor/work-a05e`  
> 固件标记：**v1.1.28**（`FW_MARK` / `FW_VERSION` in `include/config.h`）

本文是文档入口；**与代码冲突时以源码与本页为准**。带日期的 `docs/*_202609*.md` 多为历史实测归档，口径以当时固件为准。

## 平台与构建

| 项 | 实际 |
|---|---|
| Platform | pioarduino **55.03.311** = Arduino-ESP32 **3.3.11** / ESP-IDF **5.5.5** |
| 入口 | `platformio.ini` → `[idf5]`；`default_envs = esp32-c3`；S3：`pio run -e esp32-s3` |
| C3 分区 | `partitions/default_ota_1750k.csv`（OTA 槽 ~1.75 MB） |
| S3 分区 | `default_16MB.csv`；`board_build.f_cpu = 160000000L` |
| 产物 | `dist/`（见 [dist/README.md](../dist/README.md)） |

## 功能现状（产品路径）

| 能力 | 状态 |
|---|---|
| GNSS + PPS GPIO ISR → LocalClock → Stratum-1 NTP | ✅ 生产路径（`GPS_PPS_RMT_EN=0`） |
| NMEA 过滤 | ✅ Boot 探测；目标 **GGA + RMC + ZDA**；已匹配则跳过 PCAS；错集才 `$PCAS00` |
| Web OTA | ✅ `/cfg` 上传 app 镜像；chip_id 校验；PENDING_VERIFY → mark valid |
| S3 功耗 | ✅ 160 MHz + `WIFI_PS_MIN_MODEM` + `TASK_TIME_IDLE_MS=5` |
| 状态页 / OLED / 编码器 / SoftAP 配网 | ✅ |
| RMT PPS 硬件捕获 | 代码就绪，**默认关**；待 `EN=1` 板测 |
| 外置 RTC（DS3231） | 接口在，**`EXT_RTC_EN=0`**，待购件 |
| PSRAM `/history` | ❌ 已取消（v1.1.14） |

## 升级注意

- **自 IDF4 固件（≤ v1.1.21）升到 v1.1.28**：必须 USB **整片擦除** + `merged*_0x0.bin` @ `0x0`。Web OTA / 只刷 `0x10000` 常回滚。见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。  
- **已在 IDF5（v1.1.28+）**：可用 Web OTA / app-only @ `0x10000`（保 NVS）。C3↔S3 镜像不可混用。

## S3 板测（v1.1.28）

全部 PASS：整片升级、S1、壳温下降、Web OTA、NTP、13 min soak。  
见 [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)、[s3_smoke_ntp_soak_20260919.md](s3_smoke_ntp_soak_20260919.md)。

## 关键文档索引

| 文档 | 用途 |
|---|---|
| [idf5_adapt_20260919.md](idf5_adapt_20260919.md) | 平台迁移与模块审核 |
| [power_save.md](power_save.md) | S3 功耗宏与验收 |
| [gps_lock_nmea_fix_20260919.md](gps_lock_nmea_fix_20260919.md) | 无 RMC → 无法锁定根因 |
| [s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) | RMT / ExtClock 等后续项 |
| [module_boundaries.md](module_boundaries.md) | 三任务边界 |
| [ext_clock_design.md](ext_clock_design.md) | 外置 RTC 方案 |
| [esp32s3_devkitc1_hw.md](esp32s3_devkitc1_hw.md) | S3 硬件与引脚 |
| [wifi_event_fsm.md](wifi_event_fsm.md) | WiFi 事件 / 重连 FSM |
| [../芯片资料/gps/README.md](../芯片资料/gps/README.md) | DX-GP10 手册索引 |
