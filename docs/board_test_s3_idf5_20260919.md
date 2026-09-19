# S3 板测记录 — IDF5 分支

> 日期：2026-09-19  
> 固件：`cursor/idf5-adapt-a05e` · 目标 **v1.1.28** · 须用整片 `merged_firmware_esp32s3_n16r8_0x0.bin`  
> 平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）

## 升级路径

从 v1.1.20 须 USB **erase + `merged*_0x0.bin` @ 0x0**（Web OTA 会回滚）— 见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。

## 已通过

| 项 | 结果 |
|---|---|
| 整片刷入 → **fwMark v1.1.28** | **OK**（用户确认 2026-09-19） |
| GNSS / PPS / LocalClock Locked | OK（IDF5 上曾确认；擦除后请再确认一次） |

## 建议继续

1. **锁定**：擦除重配网后，确认再次 LCK / `timeValid=true`  
2. **NTP**：`ntpdate -q <设备IP>`  
3. **功耗**：串口 `[pwr] cpu=160 MHz …`；壳温  
4. **稳态**：Locked ≥10 min  
5. **Web OTA**（IDF5→IDF5）：`firmware_esp32s3.bin` 往返一次 

## 合入后可选

- 开 `GPS_PPS_RMT_EN=1` 做 RMT 板测  
- 废弃旁支 `gps-pcas-persist`  
- 外置 RTC（`EXT_RTC_EN`）待购件
