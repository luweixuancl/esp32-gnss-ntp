# S3 冒烟记录 — NTP 对时 + 13 min 稳态（v1.1.28）

> **历史冒烟**（2026-09-19）· 固件 **v1.1.28** · 已合入  
> 当前 tip **v1.1.39** — [CURRENT.md](CURRENT.md) · 汇总 [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)  
> 平台：pioarduino 55.03.311 · ESP32-S3 DevKitC-1 N16R8 · `192.168.1.24` / `H3C_LuxYang`

## 前置状态

- 整片烧写后设置回默认：anomaly=`Refuse`、holdover=30s、tempComp=关（WiFi 凭据保留）。
- GNSS 12 星锁定后开跑；全程编译默认 `WIFI_MODEM_SLEEP=1`、CPU 160 MHz。
- 边界：**只测试** —— 未改代码 / 设置 / 分区。

## ① NTP 对时（等效 `ntpdate -q`）

即时 4 样本 + 13 min 窗口内 51 次查询：

| 项 | 结果 |
|---|---|
| stratum / refid / LI | **1 / GPSS / 0**（51/51 全一致） |
| offset（即时 4 样本） | -35.9 → -20.4 → +1.0 → -0.1 ms（快速收敛） |
| offset（13 min，n=51） | 均值 **-2.83 ms**，stdev 22.58 ms，范围 -26.3 ~ +36.3 ms |
| delay（13 min） | 均值 96.1 ms，max 211.6 ms |

**结论：PASS** —— 诚实元数据正确（stratum 1 / `GPSS` / LI=0），可对时。

**备注**：offset 散布与 delay 波动（21~212 ms）强相关，归因 `WIFI_MODEM_SLEEP=1`
下 WiFi RTT 不对称，非时钟误差（时钟由 PPS 伺服）。做精密比对时建议编译
`-DWIFI_MODEM_SLEEP=0` 对照（见 [power_save.md](power_save.md)）。

## ② 稳态 ≥10 min（实跑 13 min，1 Hz 采样）

| 项 | 结果 |
|---|---|
| LCK 占比 | **574/574 = 100%**，ACQ↔LCK 跳变 **0 次** |
| ppsCount | 488 → 1267，严格单调（+30/s，无回退） |
| freqPpm | mean **-10.487**，区间 -10.719 ~ -10.153（n=574），无漂移趋势 |
| tempC | 37.8 ~ 39.8 °C，稳态 37.8 |
| 异常 | 1/575 次 `/status` HTTP 超时（测试端侧，非设备） |

**结论：PASS**。

## 口径备注

- freqPpm **-10.49**（IDF5）与 IDF4 时代长测 **-12.55**
  （[s3_clock_drift_20260917.md](s3_clock_drift_20260917.md)）相差 ~2 ppm：
  同板不同固件基线，伺服已吸收、色散如实反映，不属不合格项；
  归档时按固件口径分别记录。
- 同日人工已确认：壳温下降、Web OTA（IDF5→IDF5）PASS — 见 [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)。

## 原始记录

- [s3_smoke_ntp_soak_20260919.log](s3_smoke_ntp_soak_20260919.log) — 控制台进度 + 汇总（`VERDICT: PASS`）
