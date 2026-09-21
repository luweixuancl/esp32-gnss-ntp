# 守时精度长测（Hold 30m，v1.1.43）结果 — PASS，外推斜率 ≈0.6 µs/s

> 执行：辅助 AI（驱动 + trace/log 分析）+ 用户（拔/装 GPS 模块电源）  
> 日期：2026-09-21 · 任务书：[holdover_precision_v1143.md](holdover_precision_v1143.md) · 分支 `cursor/holdover-options-a05e`（`1bab9de`）  
> 固件 v1.1.43 · 设备 `10.121.95.14` · 策略 **Hold 30m（apol=4 / holdoverSec=1800）**  
> 产物：[hold30m_trace.csv](hold30m_trace.csv) / [.bin](hold30m_trace.bin)（1962 样本）· [hold30m_mon.log](hold30m_mon.log) / [.csv](hold30m_mon.csv) · [hold30m_debug.log](hold30m_debug.log)

```text
RESULT: PASS
fwMark: v1.1.43
anomalyPolicy: 4（Hold 30m）
holdoverSec: 1800
inject: module_vcc_pull
trace_hld_samples: 1799（1 Hz 全覆盖，utc 每秒 +1、无步进异常，dropped=0）
trace_hld_holdoverMs_first/last: 501 / 1799280
debug_log_has_HLD_lines: yes（LCK→HLD + 30s 进度行 ×60 + HLD→UNS→ACQ→LCK）
baseline_ntp_off: ≈-24 ms（本会话恢复后复测 -17.9/-17.2 ms，宿主手机钟漂混入）
hld_checkpoints: +180s off=-21.23ms · +720s off=-20.74ms · +1440s off=-18.79ms
  （disp 0.126→0.189s 随 age 缓升；li=0 st=1 ref=GPSS 全程）
recovery_net_offset_ms: 恢复后复测 -17.9 ms（与检查点连线一致，无跳秒）
drift_ms_per_min: ≈0.035 ms/min（trace 推算，见下）—— 宿主钟漂污染 NTP 口径，
  上界 +2.7 µs/s（1440s 段读数）
notes: [LCK+3s] 单笔 off=-5.6e11 ms 为监测器重锁瞬间解析伪差（设备 utcEpoch 与
  手机 UTC 秒级一致）；恢复后 servo 一次 LCK→ACQ→LCK 微降级自愈（秒级）
```

## 守时精度定量（本测核心）

| 依据 | 数值 |
|---|---|
| HLD 冻结的 freqPpm 估计 | **−12.0211 ppm**（全程不变） |
| 恢复后 servo 重估 | −11.44 → −11.42 ppm（收敛中） |
| **估计误差 → 真实外推漂移** | ≈0.6 ppm × 1800 s ≈ **1.1 ms / 30 min ≈ 0.035 ms/min** |
| NTP 检查点斜率（含宿主钟漂） | +0.9~2.7 µs/s（污染口径，仅作上界） |
| HLD q（dispersion）爬升 | 117 → 206 ms，**斜率恰为 50 ppm 地板**（0.049 ms/s，诚实且保守两量级） |

**结论**：30 min 外推真实漂移 **≈1 ms（0.035 ms/min）**，与「freqPpm EMA 误差 ±0.1–0.3 ppm + 温漂 −0.13 ppm/°C × ΔT」的物理预期吻合（本次 HLD 期 die temp 约 40–43 °C 小幅波动、`tcorr` 最大约 0.4 ppm；EMA `freqPpm` 冻结在 −12.0211）。5 min 短窗测不出斜率的问题被 30m 档解决——斜率已冒出噪声底。

> **云端复核（2026-09-21）**：mon/trace/debug 三路一致 — HLD 时长 1800.02 s、q 斜率 0.0495 ms/s（=50 ppm 地板）、墙钟补样 1799、恢复 ACQ→LCK 2.9 s。监测侧曾有约 473 s `/status` 空隙（Termux WiFi），不影响设备侧采样。LCK+3s 巨大 offset 与报告所述解析伪差一致，不以之为精度口径。

## 验收清单（任务书 §6）

| # | 项 | 结果 |
|---|---|---|
| 1 | 进 HLD | ✅ 拔电 ~2 s 内 LCK→HLD（t=94.2），trace `state=3` 连续 1799 样本 |
| 2 | HLD 时长 | ✅ holdoverMs 撑满 **1799.3 s≈1800 s** 转 UNS；q=206 ≪ 新门限 2000，未被提前砍 |
| 3 | 长测覆盖 HLD | ✅ HLD 段 1799 样本 1 Hz（墙钟补样），ppsCount 冻结 148、holdoverMs 线性爬升 |
| 4 | debug log | ✅ `LCK→HLD` + `HLD age_s=…` ×60 + `→UNS` / `→ACQ` / `→LCK` 全齐 |
| 5 | 守时精度 | ✅ 斜率 ≈0.035 ms/min（trace 口径）；检查点连线净差 ≈3 ms/26 min（含宿主钟漂上界） |
| 6 | 恢复 | ✅ 装回后 ACQ(t=1934)→LCK(t=1937，2.9 s)；`recovered 42.9s after UNS`；设备 utcEpoch 与手机 UTC 一致、无跳秒 |

## 备注

- OTA：v1.1.42→v1.1.43，HTTP 200 / 8.3 s，重启后 **LCK 2 s**（再证重启即锁）
- 恢复后 servo 出现一次秒级 LCK→ACQ→LCK 微降级自愈（ppm −11.60→−11.42 重估），NTP 拒答数秒，符合既往长测观察
- 监测宿主为手机（Termux），NTP offset 含宿主钟漂 ±10~30 ppm——**跨 30 min 的 NTP 口径只能作上界**；trace + servo 重估差值才是干净口径
- 设备终态：Hold 30m + LCK + GPSS；长测缓冲已 stop 并拉全（1962 样本，dropped=0），环内数据已归档
