# S3 板测记录 — IDF5 分支

> 日期：2026-09-19  
> 固件：`cursor/idf5-adapt-a05e` · **v1.1.28** · 整片 `merged_firmware_esp32s3_n16r8_0x0.bin`  
> 平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）  
> 设备：`192.168.1.24` · `H3C_LuxYang`

## 升级路径

从 v1.1.20 须 USB **erase + `merged*_0x0.bin` @ 0x0**（Web OTA 会回滚）— 见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。

## 已通过

| 项 | 结果 |
|---|---|
| 整片刷入 → **fwMark v1.1.28** | **OK** |
| NMEA / PPS / LocalClock / **S1** | **OK** |
| `[pwr] cpu=160` + 壳温下降 | **OK** |
| **Web OTA**（IDF5→IDF5） | **OK**（人工） |
| **NTP 对时** | **OK** — stratum 1 / GPSS / LI=0；offset 均值 −2.83 ms（n=51） |
| **稳态 ≥10 min**（实跑 13 min） | **OK** — LCK 574/574、跳变 0；详见 [s3_smoke_ntp_soak_20260919.md](s3_smoke_ntp_soak_20260919.md) |

**板测主路径全部 PASS**（`VERDICT: PASS`）。

## 合入后可选

- 开 `GPS_PPS_RMT_EN=1` 做 RMT 板测  
- 废弃旁支 `gps-pcas-persist`  
- 外置 RTC（`EXT_RTC_EN`）待购件  
- 精密 NTP 比对可临时 `-DWIFI_MODEM_SLEEP=0`（modem sleep 会抬高 RTT 散布）
