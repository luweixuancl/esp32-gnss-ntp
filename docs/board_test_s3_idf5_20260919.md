# S3 板测记录 — IDF5 分支

> 日期：2026-09-19  
> 固件：`cursor/idf5-adapt-a05e` · 目标 **v1.1.28** · 须用整片 `merged_firmware_esp32s3_n16r8_0x0.bin`  
> 平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）

## 升级路径（重要）

设备若仍显示 **v1.1.20**：说明仍在 main（IDF4）。  
**Web OTA / 只刷 `0x10000` 会失败或回滚** — 见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。

正确：USB **erase + write `merged_firmware_esp32s3_n16r8_0x0.bin` @ 0x0**，确认 `fwMark=v1.1.28` 后再测。

## 已通过（在成功进入 IDF5 固件的前提下）

| 项 | 结果 |
|---|---|
| GNSS 定位 / 星数 | OK（用户曾确认） |
| 1PPS 计数 | OK |
| **时间有效 / LocalClock Locked** | OK（v1.1.27 NMEA 修复后曾确认「正常锁定」） |

若当前仍停在 1.1.20：上表需在 **整片刷入 v1.1.28 后重测**。

## 建议继续

1. 整片刷入后确认 OLED/`/status` 为 **v1.1.28**  
2. **NTP**：`ntpdate -q <设备IP>`  
3. **功耗**：串口 `[pwr] cpu=160 MHz …`；壳温  
4. **稳态**：Locked ≥10 min  
5. **Web OTA**（仅 IDF5→IDF5）：再测一次 app 镜像往返  

## 合入后可选

- 开 `GPS_PPS_RMT_EN=1` 做 RMT 板测  
- 废弃旁支 `gps-pcas-persist`  
- 外置 RTC（`EXT_RTC_EN`）待购件
