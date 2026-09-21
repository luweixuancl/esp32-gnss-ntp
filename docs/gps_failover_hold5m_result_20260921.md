# GPS 失效链（Hold 5m）测试结果 — 恢复项 FAIL，根因已修（v1.1.42）

> 执行：辅助 AI（驱动 + API/日志分析）+ 用户（拔/装 GPS 模块电源）  
> 日期：2026-09-21 · 任务书：[gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md)  
> 固件：v1.1.41 · 设备 `10.121.95.14` · 监测 `tools/pps_failover_monitor.py`（stdlib-only，无 ntpdate）  
> 产物：[failover_hold5m.log](failover_hold5m.log) · [failover_hold5m.csv](failover_hold5m.csv)

```text
RESULT: FAIL（第 6 项「恢复」；第 1–5 项全 PASS；根因已定位并修复，待 v1.1.42 烧录复测）
date: 2026-09-21
ip: 10.121.95.14
fwMark: v1.1.41
fwVersion: 1.1.41
anomalyPolicy_before: 2（Hold 5m，开测前已是，未改动）
anomalyPolicy_during: 2
holdoverSec: 300
inject: module_vcc_pull
env: termux-alpine
ntpdate_used: no
python: 3.12.13
monitor: pps_failover_monitor.py

baseline_ntp: DEV li=0 st=1 ref=GPSS off=-22.25ms rtt=15.4ms（ALI st=2/3 对照）
transitions: [(0.02, 'LCK'), (64.21, 'HLD'), (364.24, 'UNS'), (400.18, 'ACQ')]

hld_enter_s: 拔电后 ~2 s 内 LCK→HLD（holdoverMs 开始累加，1 Hz）
hld_ntp_checkpoints:
  [HLD+120s] li=0 st=1 ref=GPSS disp=0.121s off=-23.89ms rtt=8.7ms
  [HLD+240s] li=0 st=1 ref=GPSS disp=0.127s off=-26.51ms rtt=6.7ms
  （相对基线 -22ms 漂移 <2 ms，守时精确）

uns_at_holdoverMs: 300000（整点转 UNS，q=4294967295）
uns_ntp: li=3 st=16 ref=INIT（诚实拒答 ✓；ALI 正常对照）
recovery_s_after_uns: 未恢复——装回后 PPS/NMEA 全部正常（fix=True 卫星 14-16、
  pps 1/s、zda/rmc 流动），但时钟恒驻 ACQ，/debug/log 显示 anchor=0 stable=0
  持续 25+ min 不自愈；期间设备整机重启一次（uptime 归零）后依旧 ACQ。
lck_ntp_after: 未达成（v1.1.41）

hold_offset_drift_ms: HLD 内 <2 ms（PASS）
refuse_variant: 未测（被本 bug 阻塞，待 v1.1.42）
notes: 用户整案复测时设备重启过一次；49492 样本 STOP 缓冲随重启丢失（13h 数据
  已先行归档 clock_trace_13h_20260920.md，无损失）
```

## 根因（已在 v1.1.42 修复，commit `a2fcf55`）

`LocalClock::onPpsEdge` 离群滤波死锁：PPS 中断 >5 ms 后，每条新边沿都与**停机前最后一条被接受边沿**比较，间隔恒为离群 → 全部丢弃且**基线永不前进** → `ppsBadStreak_≥3`、`ppsStable_=0` → anchor 引导分支永不执行 → 永久 ACQ。只有设备重启（清环）才能恢复。

- 诊断指纹：`fresh=1 nmea=1 zda=1 rmc=1 pps +1/s` 全健康，唯 `anchor=0 stable=0` 恒驻
- 为何史档未爆：OTA 重启走 `reset()` 清环；09-16 拔电测试时尚无此滤波（后为 RMT 双边沿缓解而加）——纯回归
- 修复：间隔 ≥ `CLK_PPS_RESUME_GAP_US`（1.5 s）判为 **PPS 重启** → 重置边沿环 + badStreak 并接受该边沿；<1.5 s 双边沿毛刺维持原吸收路径
- 宿主回归测试：`tools/local_clock_host_test/`（g++ stdlib 即可跑）：360 s 断电 → **+1 s anchor/stable 恢复、+3 s 重锁**；毛刺拒绝不回归

## 遗留

1. v1.1.42 编译 + 烧录（编译侧），烧后重跑本失效链全链 + Refuse 对照轮
2. 现网设备在 v1.1.41 下恢复手段仅剩整机重启（临时）
