# S3 板测记录 — IDF5 分支 v1.1.27

> 日期：2026-09-19  
> 固件：`cursor/idf5-adapt-a05e` · **v1.1.27** · `firmware_esp32s3.bin`  
> 平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）

## 已通过

| 项 | 结果 |
|---|---|
| 烧录 / 启动 | OK（FW v1.1.27） |
| GNSS 定位 / 星数 | OK |
| 1PPS 计数 | OK |
| **时间有效 / LocalClock Locked / NTP 可授时** | **OK**（用户确认「正常锁定」） |

v1.1.26 曾出现 PPS 正常但 `timeValid=0`；v1.1.27 恢复 **GGA+RMC+ZDA** 后锁定正常。见 [gps_lock_nmea_fix_20260919.md](gps_lock_nmea_fix_20260919.md)。

## 建议继续（未回报项）

1. **NTP**：本机 `ntpdate -q <设备IP>`（或等同客户端）能对时、stratum 合理。  
2. **功耗**：串口有 `[pwr] cpu=160 MHz wifi_modem_sleep=1`；壳温是否低于旧 240 MHz 固件。  
3. **稳态**：Locked 保持 ≥10 min，无频繁 ACQ↔LCK 抖动。  
4. **Web**：`/status` 显示 LCK、`timeValid=true`；可选再做一次 Web OTA 往返。

## 合入后可选下一工程

- 开 `GPS_PPS_RMT_EN=1` 做 IDF5 RMT RX 板测（默认仍 0）  
- 废弃旁支 `gps-pcas-persist`（逻辑已并入 1.1.27） / 评估 `rmt-reg-pps` 是否仍要  
- 外置 RTC（`EXT_RTC_EN`）待购件
