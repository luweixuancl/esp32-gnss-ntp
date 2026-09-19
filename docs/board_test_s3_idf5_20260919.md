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
| NMEA GGA+RMC+ZDA / `nmea=1 rmc=1 zda=1` | **OK**（串口 `[clk] wait`） |
| WiFi / `[pwr] cpu=160 wifi_modem_sleep=1` | **OK** |
| **PPS / LocalClock Locked** | **未过**：`pps=0 fresh=0` → 停在 ACQ、`tv=0`（2026-09-19 串口） |

## 当前阻塞

NMEA 时间已有，**缺 1PPS**（固件 GPIO4 上升沿计数为 0）。LocalClock 必须 PPS+NMEA 才能锚点锁定。

请查：模组 **1PPS → ESP32-S3 GPIO4**（地共地）。万用表/示波器看 PPS 脚是否约 1 Hz 脉冲。  
串口期望：`pps` 递增、`fresh=1`，随后 `clk=LCK`、`tv=1`。

次要：`tsens=0` / `T=nan`（Die 温未读到，不影响锁定）。

## 建议继续

1. 接好 PPS 后确认 LCK / `timeValid=true`  
2. **NTP**：`ntpdate -q <设备IP>`  
3. **稳态**：Locked ≥10 min  
4. **Web OTA**（IDF5→IDF5）：`firmware_esp32s3.bin` 往返一次 

## 合入后可选

- 开 `GPS_PPS_RMT_EN=1` 做 RMT 板测  
- 废弃旁支 `gps-pcas-persist`  
- 外置 RTC（`EXT_RTC_EN`）待购件
