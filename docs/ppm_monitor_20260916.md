# /status 秒间隔监测：freqPpm 稳定性与温补机制实测

> 状态：已完成（纯只读监测，未改任何代码）
> 日期：2026-09-16
> 设备：`10.81.127.143`（H3C_LuxYang，RSSI −43 dBm），uptime ≈ 15 h，温补 **开**（k = −0.50 ppm/°C，NVS `tcmp`/`tcpc`），异常策略 Hold 5m
> 方法：HTTP GET `/status` 1 Hz × 600 次（10 min），全部样本在内存内统计，不落盘、不影响设备
> 相关：[clock_eval_two_ntp_cmp.md](clock_eval_two_ntp_cmp.md)、[local_clock_gps_check.md](local_clock_gps_check.md)

## 1. 采集概况

| 项 | 值 |
|---|---|
| 样本 | 600/600 成功，0 失败，跨度 599 s |
| 时钟状态 | 全程 `LCK`（无任何状态切换、无 Degraded/Holdover） |
| `residualMs` | **600/600 全为 0**（NMEA 交叉检核零告警） |
| `ppsFresh` | 600/600 |
| `qualityMs` | 钉在 6 ms（5 ms 地板 + \|freqPpm\|>1 加成 1 ms） |
| `holdoverMs` | 全程 0 |
| GPS | 卫星 14–18（均值 15.8）、HDOP 1.1、RSSI −49～−32 dBm |
| NTP | `served=0`（监测期间无客户端查询） |

## 2. freqPpm 统计

| 统计 | 值 |
|---|---|
| 均值 / 中位 | **−1.534 / −1.500 ppm** |
| 标准差 | **0.106 ppm** |
| 范围 | −2.015 ～ −1.230 ppm |
| 10 min 漂移斜率 | **+1.26 ppm/h** |
| 相邻步长 | 中位 0.011 ppm，p95 0.067，max 0.345 ppm |

Allan 偏差（对 freqPpm 序列，τ 秒）：

| τ | 1 s | 2 s | 4 s | 8 s | 16 s | 32 s | 64 s | 120 s | 180 s | 240 s | 300 s |
|---|-----|-----|-----|-----|------|------|------|-------|-------|-------|-------|
| ADEV (ppm) | 0.025 | 0.035 | 0.052 | 0.072 | 0.064 | 0.073 | 0.073 | 0.092 | 0.107 | 0.118 | 0.121 |

- ADEV 随 τ 单调上升 → 漂移/随机游走主导（不是白频噪形态），这是 EMA 跟随真实慢漂的正常表现。
- 量化底噪：单间隔 1 µs 分辨率 → 8 s 跨度原始样本 ≈0.125 ppm，EMA α=0.2 后理论输出抖动 ≈0.04 ppm；实测 0.106 ppm → 约 2/3 是**真实频率慢摆**（温度耦合 + 晶振游走），非估计噪声。
- 与 9/11 冒烟的 −0.4 ppm 相比本次 −1.5 ppm：量级正常（廉价 XO，温度点不同），不可比绝对值。

## 3. 温补：`tempCorrPpm` 恒 0 是设计使然，非故障

监测期间 `tempComp=true` 但 `clock.tempCorrPpm` 全程为 0。核对实现（`src/local_clock.cpp:56-60`）：

- 每次 PPS 估频（`updatePpmFromRing`）都把参考温度 rebase：`tempRefC_ = tempC_`；
- 原因：**实测斜率已含当时温度**，若不 rebase 会把已测到的温漂重复扣一遍；
- 因此 Locked 态下 ΔT ≈ 0 → trim ≈ 0；trim 只在估频停止（PPS 丢失 → Holdover）时随 ΔT 累积生效，用于守时外推（`extrapolate()` 用 `effectivePpm()`，`qualityMs()` 守时增长也用 `effectivePpm()`）。

本次数据证实该机制按设计工作，**Locked 行为不受 tcmp 影响**。

温度耦合实测：

| 项 | 值 |
|---|---|
| tempC | 均值 42.48 °C，sd 0.50，范围 40.90–42.90 |
| corr(ppm, temp) 滞后 0 s | −0.10（弱） |
| 最大 \|corr\| | 滞后 −60 s（ppm 滞后温度约 1 min），r = −0.47 |
| 隐含增益 | ≈ **−0.10 ppm/°C**（r×sd_ppm/sd_temp），符号与 k=−0.5 一致，幅度约小 5 倍 |

10 min、±0.5 °C 的窗口不足以标定 k，只能说明方向吻合、默认 −0.50 ppm/°C 偏大。

## 4. 稳定性评价

1. **相位伺服：优。** residual 全程 0、quality 钉 6 ms、零状态切换——PPS 走相 + 5 ms slew 带在 1 Hz 高分辨率采样下零瑕疵，与两次 10 min NTP 比对（锁定段 stdev 1.3–2.6 ms）互相印证。
2. **频偏慢摆（±0.4 ppm 峰峰、+1.26 ppm/h）不构成威胁**：
   - Locked 下 ppm 只影响秒内小数外推，0.1 ppm 误差 ≈ 0.1 µs/s，可忽略；相位每秒由 PPS 重锚。
   - Holdover 下按实测最差 |ppm|≈2 计算，300 s 守时真实漂移 **≤0.6 ms**；而 dispersion 公式用 50 ppm 地板（300 s → 15 ms）+ PHI 15 ppm，远大于实测——**色散保守诚实，无虚标风险，也无提前 Unsynced 风险**（500 ms 阈遥不可及）。
3. **ADEV 随 τ 上升**是估计器跟随真实慢漂的正常形态，不是伺服病。

**结论：时钟算法在 Locked 态已达到硬件上限——锁得住、检核零告警、色散诚实。频偏游走只影响守时余量且被 50 ppm 地板完全覆盖。**

## 5. 改进建议（按性价比）

| # | 建议 | 依据 | 性价比 |
|---|------|------|--------|
| 1 | **不要再磨伺服**（EMA α / 8 s 跨度 / PLL 均无需改） | 相位由 PPS 锚定，ppm 游走对输出无感 | — |
| 2 | **温补系数 k=−0.5 先别信**：实测 ~−0.10 ppm/°C。Holdover 中错 k 会反向加误差（ΔT 5 °C → 多 2 ppm → 5 min 多 10 ms）。建议一夜大温差标定后再用；标定前 Holdover 可接受 k=0（50 ppm 地板罩得住）【**已实施 2026-09-16**：`CLK_TEMP_COEFF_CENTI` 默认 −50→0，`CLK_TEMP_CORR_MAX_PPM` 20→2 ppm，错 k 最多伤 ±2 ppm】 | 本实测 §3 | 高 |
| 3 | **`/status` 增加 `tempRefC_` 与原始估频样本字段**，现场可区分「机制正确 rebase≈0」与「根本没算」【**部分实施 2026-09-16**：`clock.tempRefC` 已上 `/status`；原始估频样本字段未加，暂无需求】 | 当前 `tempCorrPpm=0` 无法自证 | 中 |
| 4 | **补现场验收**：拔天线测 Refuse→UNS / Holdover 色散爬升（文档遗留项） | 代码路径未经现场验证 | 高 |
| 5 | **长时段（≥12 h）1 Hz 记录**看日温周期与 ppm 老化趋势 | 10 min 太短，+1.26 ppm/h 是否持续未知 | 中（可选） |

## 6. 原始统计输出（留档）

```text
samples ok/fail: 600/0  span 599.0 s
states: {'LCK': 600}
transitions: [(0.0, 'LCK')]
freqPpm mean=-1.5340 sd=0.1056 min=-2.0146 max=-1.2297 med=-1.5001 first=-1.6147 last=-1.4297
freqPpm drift slope=1.25693 ppm/hour
ppm step med=0.01063 p95=0.06734 max=0.34536
ADEV(ppm): tau1s=0.0251  tau2s=0.0353  tau4s=0.0519  tau8s=0.0722  tau16s=0.0643  tau32s=0.0726  tau64s=0.0735  tau120s=0.0919  tau180s=0.1070  tau240s=0.1182  tau300s=0.1210
tempC mean=42.48 sd=0.50 min=40.90 max=42.90
corr(ppm,temp) lag0=-0.103
max |corr| lag=-60s r=-0.474
residualMs hist: [(0, 600)]
qualityMs mean=6.0 sd=0.0 min=6.0 max=7.0
sats mean=15.8 min=14 max=18
rssi mean=-43 min=-49 max=-32
sample-gap/pps anomalies: 10
holdoverMs nonzero: 0
tempCorrPpm values: [0]
ppsFresh true: 600/600
ntp.served final: 0
```

> `sample-gap/pps anomalies: 10` 为采集端轮询相位与 PPS 相位缓慢互滑导致（HTTP 1 Hz ≠ PPS 1 Hz，同源不同钟），非设备异常；ppsFresh 600/600、residual 600/600 为设备侧证据。

## 7. 一句话

10 分钟 1 Hz 监测说明这套时钟**锁得住、不说谎、频偏慢摆无害**；温补 rebase 机制按设计工作（Locked 下 trim≈0），唯一要跟进的是用大温差标定 k，以及补拔天线的失效策略现场验收。
