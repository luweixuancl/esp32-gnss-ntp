# S3 板测记录 — IDF5 / v1.1.28 冒烟（已合入）+ 后续跟进

> 冒烟固件：**v1.1.28** · 整片 `merged_firmware_esp32s3_n16r8_0x0.bin`  
> 平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）  
> 设备：`192.168.1.24` · `H3C_LuxYang`  
> 当前 tip：**v1.1.39** — [CURRENT.md](CURRENT.md)

## 升级路径

自 ≤ v1.1.21（IDF4）须 USB **erase + `merged*_0x0.bin` @ 0x0** — 见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。

## 已通过

| 项 | 结果 |
|---|---|
| 整片刷入 → **fwMark v1.1.28** | **OK** |
| NMEA / PPS / LocalClock / **S1** | **OK** |
| `[pwr] cpu=160` + 壳温下降 | **OK** |
| **Web OTA**（IDF5→IDF5） | **OK**（人工） |
| **NTP 对时** | **OK** — stratum 1 / GPSS / LI=0；offset 均值 −2.83 ms（n=51） |
| **稳态 ≥10 min**（实跑 13 min） | **OK** — LCK 574/574、跳变 0；[s3_smoke_ntp_soak_20260919.md](s3_smoke_ntp_soak_20260919.md) |

**板测主路径全部 PASS**（`VERDICT: PASS`）。已随 PR #9 合入 `main`。

## 合入后跟进（2026-09-19）

| 项 | 结果 |
|---|---|
| RMT PPS EN=1 板测（v1.1.29–35） | **FAIL → 搁置** — [CLOSED](rmt_pps_board_test_CLOSED_20260919.md) |
| **v1.1.36**（EN=0）Web OTA 自动往返 | **PASS** — [ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md) |
| **v1.1.38** PSRAM 时钟长测环 | **PASS** — [clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md) |
| **v1.1.39** 去掉设备端 CSV 下载 | 仅二进制；CLI 转 CSV — [clock_trace.md](clock_trace.md) |
| **v1.1.41** `/cfg` 按钮随状态机 | **PASS**（A–F）— [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md) |

生产形态：GPIO 授时 + Web OTA + 时钟迹环长测（无参全量，按钮跟状态）；外置 RTC 待购件。当前 tip **v1.1.41**。
