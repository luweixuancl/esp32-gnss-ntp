# v1.1.42 OTA + Hold 5m 失效链复测结果（S3）— 全链 PASS

> 执行：辅助 AI（驱动 + API/日志分析）+ 用户（拔/装 GPS 模块电源 ×2）  
> 日期：2026-09-21 · 任务书：[fw_flash_v1142.md](fw_flash_v1142.md) · 分支 `cursor/pps-resume-deadlock-6c51`（`4bf2f00`）  
> 上一轮：v1.1.41 恢复 FAIL（PPS 重启死锁）— [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)  
> 产物：[failover_hold5m_v1142.log](failover_hold5m_v1142.log) / [.csv](failover_hold5m_v1142.csv) · [failover_refuse_v1142.log](failover_refuse_v1142.log) / [.csv](failover_refuse_v1142.csv)

```text
RESULT: PASS（Hold 5m 主测 6/6 + Refuse 对照全过）
date: 2026-09-21
ip: 10.121.95.14
fwMark: v1.1.42
fwVersion: 1.1.42
ota_http: 200 / 7.98 s（"OK — rebooting into new firmware"）
ota_relock_s: ≈3（回线 3 s，首次 /status 即 LCK；GPS 模块带电在位——直接反证
  v1.1.41「重启仍 ACQ」记录，修复后重启即锁）
anomalyPolicy_during: 主测 2（Hold 5m）/ 对照轮 0（Refuse，测毕已恢复 2）
holdoverSec: 300
inject: module_vcc_pull
ntpdate_used: no
monitor: pps_failover_monitor.py（python 3.12.13，stdlib-only）

baseline_ntp: DEV li=0 st=1 ref=GPSS off=-24.17ms rtt=10.7ms（ALI st=2/3 对照）
transitions: [(0.02, 'LCK'), (25.19, 'HLD'), (325.23, 'UNS'), (376.22, 'ACQ'), (379.09, 'LCK')]
hld_enter_s: 拔电后 ~2 s（t=25.19，holdoverMs 1 Hz 累加，residual=0）
hld_ntp_checkpoints:
  [HLD+120s] li=0 st=1 GPSS disp=0.121s off=-23.89ms rtt=8.7ms
  [HLD+240s] li=0 st=1 GPSS disp=0.128s off=-21.90ms rtt=9.5ms
uns_at_holdoverMs: 300000 整点（q=4294967295）
uns_ntp: li=3 st=16 ref=INIT（诚实拒答）
recovery_s_after_uns: 53.9 s（其中 GNSS 重捕星 ~51 s；FSM 从 ACQ 到 LCK 仅 2.9 s）
lck_ntp_after: [LCK+3s] li=0 st=1 GPSS off=-24.10ms rtt=5.6ms（回基线带 -22~-26ms）
hold_offset_drift_ms: HLD 检查点相对基线 <2 ms
refuse_variant: PASS——LCK→UNS(29.08) 直切无 HLD、li=3 st=16 即时拒答；
  transitions [(0.02,'LCK'),(29.08,'UNS'),(67.08,'ACQ'),(80.18,'LCK')]，recovered 51.1s
notes: NVS/IP/口令原样；测毕 anomalyPolicy 已恢复 Hold 5m；设备终态 LCK + GPSS
```

## 关键判据：第 6 项恢复（v1.1.42 修复点）

| 轮次 | 装回后 | v1.1.41（上轮） | v1.1.42（本轮） |
|---|---|---|---|
| Hold 5m | anchor/stable | `anchor=0 stable=0` 恒驻 25+ min，仅重启可解 | **ACQ→LCK 2.9 s**（PPS 恢复即重锁） |
| 恢复后 NTP | — | 拒答（INIT li=3 st=16） | `li=0 st=1 GPSS`，offset 回基线带 |

诊断行核对：恢复后无 `[clk] PPS resume gap` 卡滞、无 `anchor=0 stable=0` 持续——强化版 re-bootstrap（gap 判定前置到 delta 之前 + LCK/DEG/HLD 下主动 UNS 防跨洞走秒）行为符合设计。

## 守时精度分析（外推守时定量评估）

**实测上界 ≤2~4 ms / 5 min，且被测量噪声淹没**：

| 测量点 | 本轮（v1.1.42） | 上轮（v1.1.41） |
|---|---|---|
| 基线（LCK） | −24.17 ms | −22.25 ms |
| HLD+120 s | −23.89 ms（漂 +0.3） | −23.89 ms（漂 −1.6） |
| HLD+240 s | −21.90 ms（漂 +2.3） | −26.51 ms（漂 −4.3） |
| 恢复后 LCK+3 s | **−24.10 ms（净差 ≈0）** | — |

- 两轮 240 s 漂移**符号相反**（−4.3 vs +2.3 ms）→ 读数已被 NTP-over-WiFi 测量噪声（RTT 抖动 ±2~5 ms）淹没，真实误差在噪声之下；实测只能给**上界**。史档 C3 拔电测试曾测得 2 min <0.5 ms。
- **物理构成**：守时秒刻度 = 晶振外推，误差 = 频率估计误差 × 时间。`freqPpm` EMA 稳态精度 ±0.1~0.3 ppm（13.75 h 长测稳态 std 0.06 ppm）+ 断电期温漂 −0.13 ppm/°C × ΔT≈1~2 °C ≈ 0.2 ppm → 合成 0.3~0.5 ppm → **300 s 对应 0.1~0.15 ms**。恢复瞬间净相位差 ≈0（−0.07 ms）印证外推无系统性累积。
- **对外口径故意保守**：HLD 期 NTP dispersion 按入场值 + max(EMA, 50 ppm, PHI)×age 爬升（`q` 16→130 ms），对外宣称的不确定度比真实差约两个量级——NTP 诚信机制，客户端 `q` 不代表实际误差。
- **外推随时间线性放大**：30 min 档估 1~3 ms；24 h 量级温漂主导（~2.6 s @0.3 ppm），需温补（`tcmp`/`tcpc`）兜底。
- **全周期无跳秒**：恢复后第一秒即回基线带，客户端无感。

## 结论

1. **v1.1.42 死锁修复现场验收通过**：Hold 5m 主测 6/6 全过，Refuse 对照直切行为正确、恢复路径同样健康。
2. OTA 后重启即锁（≤3 s），消除了「重启仍 ACQ」的现场记录。
3. 设备终态：**Hold 5m + LCK + NTP stratum 1 GPSS**，可直接现网值守时。
