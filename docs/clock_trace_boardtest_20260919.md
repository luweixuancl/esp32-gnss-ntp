# 时钟迹环板测 — v1.1.37/38 PASS（2026-09-19）

> **10 min 时钟信息测试 PASS**。设备侧 PSRAM 环形录制 + v1.1.38 二进制下载
> 全链路验证通过；授时面零回归。

## 部署

| 步骤 | 结果 |
|---|---|
| v1.1.37 OTA（Web OTA，默认口令） | PASS：1100016 B SHA256 一致，app0→app1，38 s 回 LCK |
| v1.1.38 OTA（二进制下载修复版） | PASS：1101840 B SHA256 一致，app1→app0，35 s 回 LCK |
| PSRAM 环 | `capacity=86400 · psram=true`（懒分配，start 时占 3.8 MB） |

## 10 min 采集（v1.1.38，647 采样 @ 精确 1 Hz）

| 项 | 结果 |
|---|---|
| 采样节奏 | **utc 步长全部恰 1 s**（0 缺口），647/647，dropped=0 |
| 锁定 | **LCK 100%**（state 分布 {1: 647}），timeValid/ppsFresh 全 1 |
| ppsCount | 64 → 710，**严格单调 0 回退** |
| freqPpm | mean **-9.978**，stdev **0.068**，区间 -10.210 ~ -9.645 |
| residual / quality | residualMs 全 0；qualityMs 14–15 ms |
| 环境 | tempC 34.8~36.8 °C · 星数 14~18 |
| NTP 旁检（窗内） | stratum 1 / GPSS / LI=0（offset -5~+44 ms，modem sleep 抖动如常） |

## v1.1.38 传输修复验收

- 旧 CSV 流：~26 KB/60 s 且断流（v1.1.37 实测，客户端被迫断点续拉）
- 新二进制页：**647 样本 0.1 s（5132 samp/s）单页完成**，`CTRB` 结构 + Content-Length，
  传输期 `/status` 如约见 `ntpServing=false · refId=RSTR`，完毕自动恢复 GPSS
- 已知注意：录制中客户端异常退出可能遗留 REC/STOP 态，`POST /debug/clock/clear` 可复位

## 结论

- **时钟迹环功能 PASS**（S3 深挖路线图 ② PSRAM 环：24 h 容量、1 Hz、零丢失）
- 授时主链路在录制/下载全程零回归
- 原始数据：`clock_trace_boardtest_20260919.tar.gz`（647 行 CSV + 本文档）
