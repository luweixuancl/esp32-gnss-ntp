# ESP32-S3 移植 P2/P3 现场验收（DevKitC-1 N16R8）

> **历史归档**：下文口径以当时固件为准。当前 `main` = **v1.1.28** / IDF5，见 [CURRENT.md](CURRENT.md)。


> 状态：P2 点亮 ✔ + P3 验收 ✔（P4 转正待 ≥6.7h 长测）
> 日期：2026-09-17
> 设备：S3 `10.81.127.14`（MAC `C4:9E:7E:07:33:F4`，freeHeap ~264KB 印证双核大 RAM），固件 `7e58789` 线（RGB@48 + B 通道 NTP + 亮度 12）
> 方法：合并固件首烧 @0x0 → app-only 升级 @0x10000（保 NVS）；`ntp_cmp_termux.py` 10 min 比对 + `pps_failover_monitor.py` 失效链；全程设备侧只读
> 数据：[cmp_20260917_150301.csv](cmp_20260917_150301.csv)、[s3_failover_20260917.csv](s3_failover_20260917.csv)
> 相关：[esp32s3_devkitc1_hw.md](esp32s3_devkitc1_hw.md)（§6 计划）、[pps_pull_test_20260916.md](pps_pull_test_20260916.md)（C3 对照基线）

## 1. 验收点一览

| 验收点 | 结果 | 证据 |
|---|---|---|
| 首烧（合并固件 @0x0） | ✅ | 新板冷启动正常，串口 ROM+app 日志正常（初期乱码为终端设置问题，已排除） |
| app-only 升级 @0x10000 | ✅ | 多次热升级，NVS 配置保留 |
| GNSS 交叉接线 | ✅ | GPS TXD→GPIO1 / RXD→GPIO0（与 C3 同映射），LCK、NMEA 解码正常 |
| WiFi 入网不误配网 | ✅ | 复位/断电恢复均直连 STA，`da9417f` 行为在 S3 上复现 |
| 双核绑核 | ✅ | task-time 独占 core1；LCK 下 residual 全 0 |
| 温度传感器（S3 新 tsens） | ✅ | `temperatureRead()` 路径 tsens=1，tempC 41.8→49.8°C 合理，`tempRefC`/rebase 正常 |
| RGB 状态灯 | ✅ | **实测 @GPIO48**（本板兼容走线，非原理图 38）；R/G 心跳 + B=NTP 服务常亮；亮度 12 |
| NTP 比对 10 min | ✅ | **median −2.41 ms / stdev 4.34 ms / 异常 0/60**；全程 `st=1 GPSS` |
| 失效链：断电→HLD | ✅ | 拔模块电源即刻 HLD（q=117 起步，含 12 ppm 频偏诚实加成） |
| 失效链：色散线性爬升 | ✅ | q 117→131（≈ q0+0.05×age），disp 0.118→0.129s |
| 失效链：300s→UNS | ✅ | HLD+300.0s 整点转 UNS，`li=3 st=16 INIT disp=65535` 诚实拒绝 |
| 失效链：恢复 | ✅ | 装回 → ACQ →（重插弹跳一过性 UNS）→ **~10–20 s 回 LCK**（短断电温启动）；NTP offset 回基线带 −28.4 ms 无跳秒 |
| HLD 守时精度 | ✅ | HLD+30/120s offset −29.4/−29.8 ms 与基线一致（漂移 <0.5 ms/2min）；+240s 样本 −53.4 ms 伴 rtt 67.8 ms（手机侧噪声，如实标注） |

## 2. 发现与备注

1. **S3 板载晶振 ≈ −12.3 ppm**（C3 板为 −1~−2.7 ppm）：±20 ppm 廉价晶振规格内，纯硬件特性。伺服完全吸收——residual 恒 0，相位每秒 PPS 重锚，亚秒外推误差 ~12 µs/s 对 stratum-1 无感；唯一外显是 `qualityMs`/disp 如实抬高（5→15–17 ms 地板，disp 0.006→0.017s）——诚实机制按设计工作，无需固件改动。
2. **RGB 引脚 48 vs 38**：原装 V1.1 原理图走 38，本板（兼容走线）实测 48。`PIN_LED_RGB=48` 已入库；38/48 之争只此一处宏。
3. **重插弹跳的一过性 UNS**（592→593s ACQ→UNS→ACQ）：与 C3 拔线首测 Phase 2/4 结论一致——重插弹跳触发 `ppsBadStreak` 诚实降级，队列吸收无崩溃。
4. 温度自热爬升（42.8→49.8°C）与 C3 同规律；freqPpm 随温漂 −11.3→−12.5 ppm（含热平衡瞬态），量级与 C3 温耦家族一致。
5. `/status` 的 `rssi` 字段在 S3 上数值异常偏大（−6/−19 dBm）——显示性小瑕疵，与锁星/授时无关，待查（疑似 S3 WiFi 驱动 RSSI 报告口径差异）。

## 3. 遗留（P4 转正门槛）

- [ ] ≥6.7h 级长测干净（`clock_drift_monitor.py` 复用，S3 版 `tcpc` 待确认）——通过后再议 `default_envs` 是否纳入 s3
- [ ] `rssi` 字段口径核对（纯显示）
- [ ] S3 侧 `tcpc` 确认为 0（当前 `tempCorrPpm=0` @ΔT=0 无法区分，需 ΔT 时段观察或查 `/cfg`）

## 4. 一句话

**S3 移植 P2/P3 全绿**：双核（task-time 独占 core1）在真机上锁得住、说得准（10 min 比对 stdev 4.34 ms / median −2.41 ms）、倒得诚实（300s 整点 UNS、色散线性、恢复无跳秒）——与 C3 金机行为一致，唯晶振偏大 −12 ppm 被伺服与色散机制如实消化。
