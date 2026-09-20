# 时钟长测 bin 解析：2.22 h 稳定性/精确度分析（第 1 页，总录制 12.65 h 仅存 1/6）

> 状态：已完成（离线分析，未改任何代码；设备未做任何状态变更）
> 日期：2026-09-20（数据采集 2026-09-19 15:47Z–18:01Z）
> 设备：ESP32-S3 GNSS NTP（PSRAM 环形缓冲 86400 样本，`CLOCK_TRACE` v1.1.39+）
> 数据来源：`data/data`（CTRB v1，336 032 B）→ `tools/ct_fetch_bin.py --from-bin` → `data/clock_trace.csv`（8 000 行）
> 相关：[clock_trace.md](clock_trace.md)、[clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md)、[s3_clock_drift_20260917.md](s3_clock_drift_20260917.md)

## 1. 数据完整度（重要）

bin 头解析：`magic=CTRB ver=1 sampleSize=42`，`seqFrom=0 count=8000 seqNext=8000 **seqEnd=45523** dropped=0 flags=0`。

| 项 | 值 |
|---|---|
| 完整录制 | 45 523 样本 ≈ **12.65 h** |
| 本文件实际 | seq 0..7999，**8 000 样本 ≈ 2.22 h（仅第 1/6 页）** |
| 其余 5 页 | 从未下载；**设备断电 PSRAM 环丢失，不可恢复** |
| 丢因 | `GET /debug/clock/data` 默认 `limit=8000`（336 KiB/页），网页「下载 BIN」/单次 GET 只取第 1 页（`flags=0` 未拉完即断电） |

**教训**：长测结束先 `POST /debug/clock/stop` → 用 `ct_fetch_bin.py` 在线模式（自动分页拉全量）→ 再断电。顺序反了就只剩第 1 页。

## 2. 采集概况（本文件覆盖的 2.22 h）

| 项 | 值 |
|---|---|
| 时间窗 | 2026-09-19 15:47:45Z → 18:01:04Z（7 999 s ≈ 2.22 h @ 1 Hz） |
| 时钟状态 | **LCK 8 000/8 000（100%）**，无 ACQ/DEG/HLD/UNS |
| 连续性 | seq 无缺口、UTC 严格 +1 s/样本；uptimeΔ p50=1000 ms、max 1001 ms（首样本 369 ms 为起始偏移，正常） |
| `ppsFresh`/`timeValid` | 全程 1 |
| `holdoverMs` | 恒 0（未进 Holdover） |
| 卫星 | 11–23 颗 |
| 温补 | `tcpc=0`（关），`tempCorrPpm` 恒 0 |

## 3. 精确度

- **`residualMs` 8 000/8 000 全为 0**：NMEA 交叉检核零告警（远低于 50 ms warn 线），PPS 与 GPS 秒沿一致
- `qualityMs` 14–16 ms：NTP dispersion 口径的保守上界；实际授时精度由 GNSS PPS 秒沿决定
- `freqPpm` 伺服均值 −10.97 ppm：晶振固有偏差被伺服持续吸收，不进入秒沿

## 4. 稳定性

| 段 | freqPpm mean±std | 漂移斜率 | 温度 |
|---|---|---|---|
| 全程 2.22 h | −10.969 ± 0.181（ptp 1.769） | −0.069 ppm/h | 30.8 → 42.8 °C |
| 前 1 h（升温段） | −10.953 ± 0.250（去趋势 0.190） | **−0.563 ppm/h（温漂主导）** | 30.8 → 42.8 °C |
| 后 1.2 h（稳态） | −10.981 ± **0.088**（去趋势 0.088） | **+0.008 ppm/h ≈ 0（无老化）** | 39.8 → 42.8 °C |

- 全程 ptp 1.77 ppm 几乎全部来自**开机升温瞬态**；60 s 块均值 std 全程 0.173 → 稳态 0.088 ppm
- 稳态温耦 **−0.083 ppm/°C**，与既往 S3 实测（−0.107，s3_clock_drift_20260917）同量级；全程协方差回归 −0.122 ppm/°C 被升温段放大，不代表性
- 稳态无老化漂移，与 C3 6.7 h 长测结论（±0.06 ppm/h）一致

## 5. 结论

1. 本段数据质量满分：0 丢样、100% LCK、NMEA 检核零告警，可作健康基线。
2. 热平衡后频率稳定度 ~0.09 ppm、无老化；ppm 波动被伺服吸收，秒沿精度不受影响。
3. 12.65 h 完整录制因**只下载了第 1 页即断电**而损失 5/6——已固化为操作顺序：stop → 拉全 → 断电。
4. 复查时设备（`10.121.95.14`）已在重新录制（state=REC，755 样本，dropped=0）；录满停止后用在线模式拉全量即可避免重蹈。

```bash
# 拉全量（只读；需 state=STOP）
python3 tools/ct_fetch_bin.py --host 10.121.95.14 --pass NTP-9EC4 -o clock_trace_full.csv
```
