# PSRAM 诊断历史缓冲 + `/history`（方案）

> 状态：**已实现（v1.1.8+），v1.1.9 打磨 O(1) 概要；待板测**（2026-09-18）  
> 路线图项：③（原暂缓；OTA 板测通过后启动）  
> 相关：[s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md)、[module_boundaries.md](module_boundaries.md)、`tools/clock_drift_monitor.py`、`tools/history_pull.py`

## 1. 目标与非目标

### 目标

- S3（8MB OPI PSRAM）上自录约 **24 h × 1 Hz** 时钟/GNSS 诊断样本，掉电可丢。
- 提供只读 **`/history`（JSON 概要）** 与 **`/history.csv`（流式 CSV）**，可与现有 `clock_drift_monitor` 字段对照。
- **C3 无 PSRAM**：功能编译期/运行期关闭，`/history` 返回 `enabled:false`，不占 DRAM 大缓冲。
- 采样与 HTTP 导出遵守任务边界，**不拖慢 NTP / PPS 路径**。

### 非目标（本阶段不做）

- 掉电持久化（Flash/NVS 存满 24 h 样本）。
- 外置 SD / SPIFFS 大文件。
- Web 上画图（只提供数据；分析仍用脚本/表格）。
- 改分区表或 Arduino/IDF 大版本升级。
- C3 上用 DRAM 做「迷你历史」（可列为后续可选项）。

## 2. 现状与约束

| 项 | 约束 |
|----|------|
| S3 | `BOARD_HAS_PSRAM` + `qio_opi` 已开；**零使用** |
| C3 | 无 PSRAM；DRAM ~320KB，大环不可接受 |
| 任务 | `task-time` 拥有 GPS/时钟；`task-net` 拥有 HTTP；跨任务勿调 `WiFi.*` |
| OTA | `ipcOtaBusy()` 时应停采，避免无意义样本 + 抢 Flash |
| 长测对照 | `clock_drift_monitor` 1 Hz 字段：`state,holdoverMs,residualMs,freqPpm,tempC,tempRefC,tempCorrPpm,tempComp,qualityMs,ppsCount,ppsFresh,satellites,fix,rssi` |

## 3. 容量与样本布局

### 3.1 容量

| 参数 | 值 |
|------|-----|
| 采样周期 | 1 s（与现长测脚本一致） |
| 深度 | **86400**（24 h） |
| 单样本 | **24 B**（packed） |
| 环缓冲 | \(86400 × 24 ≈ 2.07\,\mathrm{MB}\) |
| PSRAM 余量 | 8 MB − 2 MB ≈ 6 MB（预留后续扩展） |

若 `ps_malloc` 失败：降级 `enabled=false`（与 C3 同路径），串口打一行原因。

### 3.2 样本结构（固定布局，版本号可演进）

```cpp
// HistorySample v1 — 24 bytes, packed
struct HistorySample {
  uint32_t utcEpoch;     // 0 = 当时无有效 UTC
  int16_t  residualMs;   // 钳位到 int16
  int16_t  freqPpmX100;  // centi-ppm，例 −12.73 → −1273
  int16_t  tempCenti;    // 0.01 °C；无效 → INT16_MIN
  int16_t  tempRefCenti; // 同上
  uint16_t qualityMs;    // 0xFFFF = 无效/未授时
  uint16_t holdoverSec;  // holdoverMs/1000，饱和 uint16
  uint32_t ppsCount;
  uint8_t  state;        // ClockState
  uint8_t  satellites;
  int8_t   rssi;         // 来自 WifiLinkSnapshot；未知 → 0
  uint8_t  flags;        // b0 ppsFresh, b1 fix, b2 tempComp, b3 gap
};
static_assert(sizeof(HistorySample) == 24);
```

- **`flags.gap`**：本拍相对上一拍 `ppsCount` 或单调时间不连续时置位（设备侧自检空洞）。
- **`tempCorrPpm`**：可由 `tempCenti/tempRefCenti` + 设置系数在导出端重算，不单占字段（CSV 可算出或留空列以兼容脚本）。

### 3.3 环缓冲元数据（DRAM，很小）

```text
enabled, capacity=86400, count, head, seq
lastPushMs, dropCount (分配失败/忙时丢弃计数)
```

写入：仅 **task-time**。  
读取导出：仅 **task-net**；开始导出时原子拷贝 `(head, count, seq)`，流式读期间允许覆盖最旧样本（典型 ring 语义；概要里带 `seq` 便于发现导出中途 wrap）。

## 4. 模块与任务边界

```text
┌─────────────┐   1 Hz sample    ┌──────────────────┐
│  task-time  │ ───────────────► │ HistoryRecorder  │  (PSRAM ring)
│  GpsStatus  │                  │  (new module)    │
└─────────────┘                  └────────┬─────────┘
                                          │ snapshot indices
┌─────────────┐   GET /history*           │
│  task-net   │ ◄─────────────────────────┘
│  WebPortal  │   stream CSV / JSON summary
└─────────────┘
```

| 组件 | 职责 |
|------|------|
| `HistoryRecorder`（`include/history_recorder.h` + `src/history_recorder.cpp`） | `begin()` 分配；`push`；`summary()`（状态直方图/均值 O(1)，freq min/max 粗步长 ≤256）；`beginExport`/`sampleLogical` |
| `task-time` | 每秒（或 `millis` 跨秒）在 `gGps.loop` 之后 `push`；`ipcOtaBusy()` 则跳过 |
| `WebPortal` | 注册路由；组 JSON/CSV；**不**在 time 任务里发 HTTP |
| `main` | S3 `begin()`；C3 no-op |

**rssi**：time 任务读 `gWifi.linkSnapshot().rssi`（已是跨任务安全快照），禁止 `WiFi.RSSI()`。

## 5. HTTP API

均 **只读、免登录**（与 `/status` `/metrics` 同策略；局域网诊断）。OTA/写配置仍需登录。

### 5.1 `GET /history`

JSON 概要（小，可频繁拉）：

```json
{
  "enabled": true,
  "version": 1,
  "capacity": 86400,
  "count": 3600,
  "intervalSec": 1,
  "seq": 12345,
  "oldestUtc": 1726680000,
  "newestUtc": 1726683600,
  "psramBytes": 2073600,
  "stateCounts": { "LCK": 3500, "ACQ": 80, "HLD": 20, "UNS": 0, "DEG": 0 },
  "freqPpm": { "min": -12.9, "max": -12.0, "mean": -12.55 },
  "gaps": 3,
  "otaSkipped": 12
}
```

`enabled:false`（C3 或分配失败）时仅返回 `enabled`、`reason`。

### 5.2 `GET /history.csv`

- `Content-Type: text/csv`
- 首行表头（对齐长测脚本，便于粘贴/对比）：

```text
utcEpoch,state,holdoverSec,residualMs,freqPpm,tempC,tempRefC,tempCorrPpm,tempComp,qualityMs,ppsCount,ppsFresh,satellites,fix,rssi,gap
```

- **流式写出**：按批（如 64～256 行）`server.sendContent`，避免一次占用数 MB DRAM。
- 查询参数（实现阶段建议支持，方案预留）：
  - `last=N`：最近 N 秒（默认全量，上限 `capacity`）
  - `max=N`：最多 N 行（手机浏览器防爆）

全量 24 h CSV 约数 MB 文本，仅建议在 **STA 局域网** 拉取。

### 5.3 `GET /metrics` 增补

```text
history_enabled 1
history_count 3600
history_capacity 86400
history_gaps_total 3
```

### 5.4 状态页入口（轻量）

`/` 或 `/cfg` 旁增加链接：`/history`、`/history.csv?last=3600`（不必做图表）。

## 6. 生命周期与异常

| 事件 | 行为 |
|------|------|
| 启动 | `HistoryRecorder::begin()`；失败 → disabled |
| 每秒 | time 任务 `push`；OTA busy → 跳过并 `otaSkipped++` |
| SoftAP / 未锁星 | 仍采样（`utcEpoch=0`、`state=ACQ/UNS`），便于看冷启曲线 |
| 导出中 wrap | 允许；JSON `seq` 变化表示环在动 |
| 工厂复位 | 不强制清 PSRAM；重启后自然空环 |

## 7. C3 策略

```cpp
#if defined(BOARD_HAS_PSRAM)
  // allocate SPIRAM
#else
  enabled_ = false; reason_ = "no PSRAM";
#endif
```

同一套源码；C3 固件体积仅多几个 stub 符号。验收：**C3 编译通过 + `/history` → enabled false + 无大块 DRAM 分配**。

## 8. 实现切片（建议顺序）

1. **骨架**：`HistoryRecorder` + `begin`/`push`/`summary`；S3 分配；C3 stub；串口打印 `psram=… enabled=`。
2. **挂钩**：`task-time` 1 Hz push（OTA 跳过）；`/status` 可加 `historyCount` 可选字段。
3. **HTTP**：`/history` JSON → `/history.csv` 流式 → `?last=`。
4. **打磨**：metrics、状态页链接、`tools/history_pull.py`；`summary()` 增量统计（避免满环 O(N)）。
5. **验收**：S3 跑满数小时后拉 CSV，与并行 `clock_drift_monitor` 抽样比对（freqPpm/state 趋势一致即可，不要求逐秒 bit 相同）。

## 9. 验收准则

| # | 准则 |
|---|------|
| A | S3：`enabled=true`，`psramBytes≈2.07e6`，连续运行后 `count` 随时间增至 cap 后稳定 |
| B | `/history.csv?last=600` 可在浏览器/curl 打开；表头与 §5.2 一致 |
| C | OTA 上传期间不写入（或 `otaSkipped` 增加）；OTA 后环可继续 |
| D | C3：功能关闭，DRAM 无明显 +2MB；`pio run -e esp32-c3` 通过 |
| E | 采样路径无 `WiFi.*`；time 任务不调 WebServer |
| F | 与外部 1 Hz `/status` 长测对照：同窗 freqPpm 均值差与状态占比无明显背离 |

## 10. 风险与对策

| 风险 | 对策 |
|------|------|
| PSRAM 带宽/缓存影响 WiFi | 仅 24B/s 写；导出时限速分块；OTA 停采 |
| CSV 导出占满 net 任务 | 分块 + 每块 `kickNet`/`yield`；可拒绝与 OTA 并发（busy 时 503） |
| `rssi` 口径异常（已知 S3 显示问题） | 如实记录快照值；文档注明与驱动口径有关 |
| 结构体改版 | `version` 字段；旧 CSV 客户端看表头 |

## 11. 明确不做的扩展（记一笔）

- NVS 累计「HLD 次数 / UNS 次数」——可第二期用几个计数器，不阻塞 v1。
- C3 DRAM 15 min 短环——有需求再开。
- Web 图表 / WebSocket 实时流。

## 12. 决策摘要（实现时照此执行）

1. **24 h × 1 Hz × 24 B ≈ 2.1 MB PSRAM 环**，仅 S3。  
2. **task-time 写、task-net 读**；OTA 停采。  
3. **`/history` + `/history.csv`** 免登录只读；CSV 流式。  
4. **C3 关闭**，不降级大 DRAM 环。  
5. 字段对齐现有长测脚本，便于验收。
