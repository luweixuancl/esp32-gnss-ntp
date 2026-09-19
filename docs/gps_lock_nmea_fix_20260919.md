# GPS 授时无法锁定排查（NMEA / RMC）

> 根因修复自 **v1.1.27**；当前 **main = v1.1.39**  
> 总览：[CURRENT.md](CURRENT.md)

## 现象

PPS 计数正常、定位看似正常，但「时间有效」无效，NTP 无法 Locked。

## 根因

Boot `PCAS03` 若只留 **GGA+ZDA** 而关掉 **RMC**：  
`TinyGPSPlus` 日期时间主要来自 RMC；ZDA 不稳定时：

- 无 `commitNmeaTime` → `nmeaFresh=0`
- LocalClock 停在 ACQ → `timeValid=0` → NTP 拒授时  

PPS 仍会计数，故「PPS 正常但时间无效」。

## 当前固件行为（正确）

- 过滤器：**GGA + RMC + ZDA**
- 已匹配则跳过 PCAS；句型不对才 `$PCAS00` 写 FLASH
- 未锁定时每 5 s `[clk] wait ...` 诊断

## 串口期望

```text
[gps] NMEA filter: GGA+RMC+ZDA (saved|RAM|skip)
[clk] wait tv=0 clk=ACQ pps=… nmea=0/1 zda=0/1 rmc=0/1 …
```

数十秒内应 `nmea=1` 且 `rmc=1` 或 `zda=1`，随后 `clk=LCK`。  
若 `pps=0`：查 **1PPS→GPIO4** 接线（与 NMEA 无关）。  
若仍显示 ≤ v1.1.21：先 [整片升 IDF5](upgrade_idf5_from_1120.md)。

板测：[board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)
