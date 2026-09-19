# GPS 授时无法锁定排查（v1.1.27）

## 现象

PPS 计数正常、星历/定位看似正常，但 Web/OLED「时间有效」为无效，NTP 无法 Locked。

## 根因（高概率）

Boot `PCAS03` 曾把 **RMC 关掉**，只留 GGA+ZDA。  
`TinyGPSPlus` 的日期时间主要来自 RMC；若模块未稳定输出 ZDA，则：

- 无 `commitNmeaTime` → `nmeaFresh=0`
- `LocalClock` 停在 ACQ，无锚点 → `timeValid=0` → NTP 拒授时

PPS 仍会计数（GPIO），故会出现「PPS 正常但时间无效」。

## v1.1.27 修复

- 过滤器改为 **GGA + RMC + ZDA**
- 已匹配则跳过 PCAS；仅在句型不对时 `PCAS00` 写 FLASH
- 未锁定时每 5 s 打 `[clk] wait ...` 诊断行

## 刷机后请看串口

```text
[gps] probe saw: ...
[gps] NMEA filter: GGA+RMC+ZDA (saved|RAM|skip)
[clk] wait tv=0 clk=ACQ pps=… nmea=0/1 zda=0/1 rmc=0/1 anchor=0/1 …
```

期望在数十秒内：`nmea=1` 且 `rmc=1` 或 `zda=1`，随后 `clk=LCK`、时间有效。

若 `nmea`/`rmc` 长期为 0：查 GNSS TX→ESP RX 接线与波特率；可临时 `-DGPS_DEBUG_NMEA=1` 看原文。

## 板测

S3 上 v1.1.27+ 在成功进入 IDF5 固件后可锁定。若设备仍显示 v1.1.20，先按 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md) **整片刷入**，再测锁定。完整清单见 [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)。
