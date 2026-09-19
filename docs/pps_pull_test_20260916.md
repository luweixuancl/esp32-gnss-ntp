# 拔 GPS 模块电源 / PPS 线失效链现场验收（Holdover 完整超时链）

> **历史归档**：下文口径以当时固件为准。当前 `main` = **v1.1.39** / IDF5，见 [CURRENT.md](CURRENT.md)。


> 状态：已完成（HoldoverLong 全链通过；Refuse 变体仍待测）
> 日期：2026-09-16
> 设备：`10.81.127.143`，策略 `Hold 5m`（`apol=2`/`ahold=300`），温补开
> 方法：1 Hz 只读轮询 `/status` + Python NTP 三方查询（设备 / 本机钟 / 阿里云），全程未改设备配置
> 失效注入：**拔 GPS 模块电源**（含杜邦线拔 PPS 的计划外对照）；真·拔天线（模块带电失星）未测
> 相关：[local_clock_gps_check.md](local_clock_gps_check.md) §9、[clock_eval_two_ntp_cmp.md](clock_eval_two_ntp_cmp.md)、[ppm_monitor_20260916.md](ppm_monitor_20260916.md)

## 1. 结论一览

| 验收点（对照 design §9） | 结果 | 证据 |
|---|---|---|
| 失效及时进入 Holdover | ✅ | 模块断电 → UART 立即静默、PPS 停 → **1.5–2.5 s** 内 `tick()` 经 `!ppsFresh` 分支进 HLD |
| HLD 期间 NTP 诚实 stratum 1 | ✅ | `li=0 st=1 GPSS`，disp 0.108→0.113 s 随 age 上升 |
| 色散随 ppm×age 线性爬升 | ✅ | q 107→121 ≈ `106 + 0.05×age(s)`（50 ppm 地板 + entry 100 ms + \|ppm\|>1 加成），与 `qualityMs()` 公式吻合 |
| 守时精度 | ✅ **优** | HLD 2 min 内 offset −13.90/−13.68 ms 与拔线前基线差 **<0.5 ms**（esp_timer + ppm 外推有效） |
| 300 s 超时 → UNS | ✅ | holdoverMs=300000 准时转 UNS，`holdoverMs=0`、q=0xFFFFFFFF |
| UNS 拒绝授时 | ✅ | `li=3 st=16 ref=INIT disp=65535 prec=-6`；offset 为未同步时间基（−4×10⁹ s 字面值，客户端不可用） |
| 恢复无整秒跳变 | ✅ | 三次重锁后 offset 全部回到基线带（−13.5～−15.9 ms），residual=0、q 回 7–8 |
| 恢复时长 | ✅ | PPS 插回：**ACQ→LCK ≈5 s**；天线装回：含重捕星 **≈50 s** |

## 2. 测试时间线与关键数据

### Phase 0 基线（60 s，全 LCK）

- DEV：`st=1 GPSS`，off −13.54/−15.91 ms，rtt 8–11 ms
- ALI：st=2，off +7.5～+17.6 ms，rtt 84–105 ms（路径抖动 ±10 ms，与历史一致）
- 配对差 DEV−ALI ≈ −23～−31 ms

### Phase 1「脏拔」PPS 线（计划外但有效）

手动拔杜邦线存在接触抖动：假边沿使 `ppsCount` 瞬间 +286（1 s 内）、拔出过程 ~2 边沿/s，`ppsFresh` 持续为真压制了 `tick()` 的 HLD 路径；不规则间隔使 `ppsBadStreak`≥3 → **UNS→ACQ**（`onPpsEdge` 的 `ppsStable_` 失效路径抢先生效）。

- 行为依然诚实：q=0xFFFFFFFF，NTP `li=3 st=16 INIT disp=65535`
- PPS 引脚 `INPUT_PULLDOWN`（gps_service.cpp:40）：拔空后无线毛刺误触发 ✔

### Phase 2 PPS 插回恢复

- 插入弹跳边沿被 ISR 队列吸收（delta>1 catch-up 路径被走到）
- **ACQ→LCK ≈5 s**；恢复后 `disp=0.008s GPSS`，off −15.24 ms ≈ 基线 → 无相位漂移

### Phase 3 拔 GPS 模块电源（HLD 完整链）

时间线修正（2026-09-16 复盘确认：实际拔的是**模块电源**，非天线）：

- **0–178 s**：设备正常运行（口令后的操作延迟），LCK、PPS +178、NMEA 正常解码（ageMs 始终 <5 s）；卫星 18→7 为时段正常波动，**不是**失效前兆
- **t≈178 s 断电**：UART 立即静默、PPS 立即停。入口样本 `ageMs=1609`（最后 NMEA 时间字段为断电前 1.6 s 解码，「活着的冻结」）、sat 冻结 7、ppsCount 冻结 56717
- **t=179 s：LCK→HLD**（holdoverMs=266，q=107）——断电后 ~1.5 s `ppsFresh` 超时，`tick()` 的 `!ppsFresh` 分支生效（此时 nmeaFresh 尚未过 5 s 阈值）
- HLD 期间 holdoverMs 与墙钟 1:1（esp_timer）；q 爬升：107→121 = 106+0.05×age ✔；sat 恒 7、nmeaAgeMs 无限增长（断电特征：所有字段冻结在最后解码值，fix=False 来自固件 `GPS_FIX_MAX_AGE_MS` staleness 判定）
- NTP 检查点：

| 时刻 | DEV | ALI |
|---|---|---|
| HLD+30 s | `li=0 st=1 GPSS disp=0.108s off=−13.90ms` | st=2 off +18.53ms |
| HLD+120 s | `li=0 st=1 GPSS disp=0.113s off=−13.68ms` | st=3 off +2.01ms |
| UNS+2 s | `li=3 st=16 INIT disp=65535` ❌拒绝 | st=2 off +23.57ms |

- **holdoverMs=300000 准时转 UNS**（策略 `ahold=300` 生效），q=0xFFFFFFFF

### Phase 4 模块电源装回（上电冷启动）

- 模块重新上电冷启动：卫星 0→5→7，fix ~49 s 恢复；PPS 56721→56725 恢复
- **ACQ→LCK**（t=50 s）；`GPSS` 立即恢复，`disp=0.007s`，off **−13.58 ms** 与全部基线一致

## 3. 发现与备注

1. **杜邦线拔插必然产生假边沿**（插入瞬间 +286 count，拔出期间 ~2/s）：PPS ISR 队列全部兜住未崩溃，delta>1 catch-up 与 `ppsBadStreak` 路径按设计走，最终诚实 UNS。**现场演示拔 PPS 线时应期望走 UNS 而非 HLD**——HLD 路径只在电气干净断开时触发；模块断电即是干净的（PPS 线随模块断电自然归零，配合 `INPUT_PULLDOWN` 无毛刺）。
2. **模块断电 = 立即失效，无灰色期**：UART 静默 → PPS 停 → 1.5–2.5 s 内进 HLD。失效注入最彻底（NMEA+PPS 同时静默）。
3. **`/status` 观测面上「断电」与「拔天线」不可区分**：两者都表现为字段冻结 + 各 age 无限增长 + ppsCount 停。要区分需抓 UART0 NMEA 流（拔天线时语句仍在流入、内容 stale/失效；断电则完全静默）。
4. **守时外推精度远优于色散标称**：实测 2 min HLD 相位漂移 <0.5 ms，而色散按 50 ppm 地板标称（120 s → 6 ms）——色散诚实保守，客户端不会被误导。
5. dispersion 解析注意：NTP 包 root dispersion 在 bytes 8:12（首测脚本误读 Reference Timestamp 得 61012 s，已修正）。
6. NTP 客户端对 UNS 响应的 `off=−4×10⁹ s` 是未同步时间基字面值（与 9/14 文档一致），`ntpdate` 会判 unsuitable，非设备真实钟差。

## 4. 遗留

- **Refuse 策略拔线变体未测**（本次策略为 Hold 5m）：预期 LCK→UNS 直接拒绝，待改 `apol` 后现场验证
- **真·拔天线（模块带电失星）未测**：预期行为与断电在 /status 上等价（见发现 3），如需区分可顺带验证 DX-GP10 失星后 PPS 是否继续走
- HoldoverShort（30 s）变体未单独测（同一代码路径，仅 `ahold` 不同）
- OLED/Web 在 HLD/UNS 下的显示核对未记录（Web `/status` 已核实；OLED 未拍照）

## 5. 一句话

**拔模块电源完整超时链验收通过**：断电 1.5–2.5 s 内进 HLD、色散诚实爬升、守时 2 min 漂移 <0.5 ms、300 s 准时拒绝、恢复无跳秒——`local_clock` 的失效策略分支首次全部拿到现场证据。
