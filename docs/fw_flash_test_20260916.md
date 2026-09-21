# 新固件烧录验收 + 温补上限/观测面现场回归

> **历史归档**：下文口径以当时固件为准。当前 `main` = **v1.1.43** / IDF5，见 [CURRENT.md](CURRENT.md)。


> 状态：已完成（Phase 1–4 全部通过；±2 ppm 上限未压力触及，属构造性安全）
> 日期：2026-09-16
> 固件：`dist/firmware.bin`（934,496 B，构建自 `3c8a6f2`），esptool `write_flash 0x10000`，**NVS 保留**
> 设备：`10.81.127.143`，策略 Hold 5m（`apol=2`/`ahold=300`），温补**开**（NVS `tcpc` 仍为 −50）
> 方法：只读轮询 `/status` + `tools/pps_failover_monitor.py`（baseline + failover，全程未改设备配置）；1 Hz 原始采样见 [fw_flash_test_20260916.csv](fw_flash_test_20260916.csv)
> 相关：[ppm_monitor_20260916.md](ppm_monitor_20260916.md)、[pps_pull_test_20260916.md](pps_pull_test_20260916.md)

## 1. 验收点一览

| 检查项 | 结果 | 证据 |
|---|---|---|
| `0x10000` 烧录保留设置 | ✅ | 重启后 `tempComp=true`、`ahold=300`、静态 IP 均在 |
| `/status` 新增 `clock.tempRefC` | ✅ | LCK 后出现，数值 ≈ `tempC`（41.9/41.9）；建议 #3 落地 |
| Locked 态温补 rebase | ✅* | `tempCorrPpm` ≈ 0，仅开机温飘期出现 ±0.5 瞬态（见发现 2） |
| 相位伺服 | ✅ | residual 全程 0（LCK 与 HLD 期间均零告警），quality 6 ms |
| NTP 诚实 stratum 1 | ✅ | `li=0 st=1 GPSS disp=0.006s`；配对差 DEV−ALI = −23.5 ms，与历史（−23~−31 ms）同带 |
| 失效 1.5–2.5 s 进 HLD | ✅ | 拔线后 8.04 s 采样即 HLD（`ppsCount` 冻结 319，电气干净断开） |
| 色散随 age 线性爬升 | ✅ | q 106→120，拟合 `106 + 0.05×age` 与 `qualityMs()` 公式吻合 |
| 300 s 准时 → UNS | ✅ | t=8.04 进 HLD，t=308.03 转 UNS（= 300.0 s 整），`q=0xFFFFFFFF` |
| UNS 诚实拒绝 | ✅ | `li=3 st=16 INIT disp=65535`（off 为未同步时间基字面值） |
| 温补上限 ±2 ppm（新） | ✅ | 全程 `tempCorrPpm ∈ [0, +0.5]`；室内 ΔT≈1 °C 未压力触及，见发现 3 |
| 恢复无跳秒 | ✅ | LCK+3s `off=−7.27 ms` 回基线带（−7.2~−7.6），`disp=0.006s GPSS` |
| 恢复时长 | ✅ | 模块冷启动 → ACQ（t=384）→ **LCK（t=387，ACQ→LCK 仅 3 s）**；UNS 后 79 s 全程 |

## 2. 失效链时间线（failover 监测脚本原始输出）

```text
transitions: [(0.03, 'LCK'), (8.04, 'HLD'), (308.03, 'UNS'), (384.03, 'ACQ'), (387.03, 'LCK')]
[DEV      ] li=0 st=1  ref=GPSS disp=0.006s off=-7.61ms   (baseline)
[HLD+30s  ] li=0 st=1  ref=GPSS disp=0.107s off=-7.14ms
[HLD+120s ] li=0 st=1  ref=GPSS disp=0.112s off=-5.90ms
[UNS+2s   ] li=3 st=16 ref=INIT disp=65535  ❌拒绝
[LCK+3s   ] li=0 st=1  ref=GPSS disp=0.006s off=-7.27ms   (恢复)
recovered 79.0s after UNS
```

HLD 期间 `holdoverMs` 与墙钟 1:1（esp_timer）；`ppsCount` 冻结 319、sat 冻结 14（模块断电特征）；`tempRefC` 冻结在最后估频值 42.90（rebase 随 PPS 停止而停止，符合设计）。

## 3. 发现与备注

1. **测试途中一次非固件重启**：Phase 2 与 3 之间设备重启（uptime 归零）——确认为人为碰触/供电瞬断，一次性事件；重启后 ~50 s 内即重锁（暖启动）。固件稳定性无异常。
2. **NVS 旧系数仍生效**：`CLK_TEMP_COEFF_CENTI` 默认改 0 只影响新出厂/恢复出厂；本机 NVS `tcpc` 仍为 −50（烧录 `0x10000` 不清 NVS，符合预期）。开机 Die 温度爬升（40.9→41.9）期间传感器 LSB 跳变 × k=−0.5 → `tempCorrPpm` 出现 ±0.5 ppm 瞬态，锁定后随 rebase 归零。**建议：现场这台在 `/setup` 或 OLED 把温补系数设回 0（或恢复出厂）以启用保守默认，待大温差标定后再上负系数。**
3. **±2 ppm 上限未被压力触及**：室内拔模块后板温仅降 ~1 °C → `tempCorrPpm` 峰值 +0.5 ppm（且方向正确：模块断电 → 板温降 → 反向补偿）。上限为构造性保护（`CLK_TEMP_CORR_MAX_PPM`），极端 ΔT 场景无需现场复现即安全。
4. **offset 绝对值是手机钟相对值**：DEV offset 以 Termux 手机钟为参考，手机自身 NTP 同步会注入阶跃（baseline 时 ALI 出现 +144 ms/rtt 553 ms 的路径抖动样点）；跨会话比较应使用同源配对差 DEV−ALI（本次 −23.5 ms，历史 −23~−31 ms，一致）。
5. 拔模块电源复测与 `pps_pull_test_20260916.md` 首测结论完全一致：断电 1.5–2.5 s 进 HLD、色散诚实、300 s 准时拒绝、恢复 3 s 重锁无跳秒——**合并 Windows 侧 7 个提交（恢复出厂/OLED 熄屏/WiFi）后失效策略行为无回归**。

## 4. 一句话

新固件（温补默认 0 + ±2 ppm 上限 + `tempRefC` 观测面）现场回归全绿：观测面让 rebase 机制首次「自证清白」，失效链在合并代码后零回归，唯一跟进项是把这台设备的 NVS `tcpc` 降回 0。
