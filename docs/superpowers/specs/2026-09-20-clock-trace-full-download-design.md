# 设计：时钟长测一次性全量下载（v1.1.40）

> 状态：已批准（2026-09-20，用户选定「固件端点一次性全量」+「无参数 = 全量」）
> 背景：2026-09-20 长测 12.65 h 仅存第 1 页事故——`GET /debug/clock/data` 无参数默认
> `limit=8000`，浏览器「下载 BIN」按钮恰好无参 → 永远只拿到第 1/6 页，其余页在断电后丢失。
> 分析：[clock_trace_analysis_20260920.md](../../clock_trace_analysis_20260920.md)

## 目标 / 非目标

**目标**：浏览器一次点击即完整下载全部样本（单 HTTP 响应）。
**非目标**：不做 ZIP、不做设备侧 CSV（v1.1.39 已删）、不改 CLI 分页、不做断点续传/Range。

## 接口语义（`src/web_portal.cpp` `handleClockTraceData`）

| 请求 | 行为 |
|---|---|
| `GET /debug/clock/data`（**无 `from` 无 `limit`**） | **全量单响应**：`nSend = seqNext − max(0, seqFirst)`，无 12000 上限。CTRB 头 `count/seqNext/seqEnd` 为真实全量，`flags=DONE`。S3 满环 ≈3.63 MB，C3 ≈151 KB |
| 显式 `from` / `limit`（任一） | 分页不变：DEFAULT 8000 / MAX 12000。两个 CLI（`ct_fetch_bin.py`、`clock_trace_client.py`）均显式传参 → **零改动** |

- 新增响应头 `Content-Disposition: attachment; filename="clock_trace_<seqFrom>-<seqTo>_<首样本utcEpoch>.bin"`（首样本 UTC 由 `clockTraceRead(fromSeq,&s,1,…)` 取得，取不到则省略 UTC 段）。
- 409（非 STOP）、204（count=0）、鉴权（cookie/`?pass=`）、`format=csv`→410、期间停 NTP（`xferBusy`/KoD `RSTR`/优先级提升）**全部不变**。
- 流式机制原样复用：256 样本/块 `clockTraceRead` → `sendContent`，每块 `esp_task_wdt_reset()` + `ipcKickNet()`；无大块内存拷贝。

### 设计修正记录
原设计 §2 曾假设「环形回卷后 `avail` 未按 `seqFirst` 钳制 → CL 与实发不匹配」。实施前重读代码证实
`fromSeq` 已在 web_portal.cpp:387-393 钳制（默认即 `seqFirst`），**该 bug 不存在，无需修复**。

## 前端（/cfg 页 `ctFetchBin()`）

预检 STOP 保留；`href` 不改（无参=全量自动生效）。新增：
- 大小预估 `count×42+32` 经已有 `fmtBytes()`；
- >1 MB 时 `confirm('共 N 样本（X MB），下载期间停 NTP。开始？')`；
- 提示文案带大小：「下载中（X，停 NTP）… CSV 请用 CLI」。

## NTP 停摆窗口

单窗口 3.63 MB @ ESP32 WiFi ≈ 2–30 s（视信号），比 5 页 5 窗口总扰动更小。PPS 为 ISR 采样，仅 STOP 态可下载，不影响录制。

## 版本与文档

- `FW_VERSION` 1.1.39 → **1.1.40**（include/config.h:11）。
- `docs/clock_trace.md`：无参=全量语义、Content-Disposition、一键按钮、固化操作顺序「stop → 浏览器/ct_fetch_bin 拉全 → 断电」。
- `README.md` / `docs/CURRENT.md`：基线 v1.1.40；`AGENTS.md` 记忆表补一行。

## 测试（P3 板测口径，烧录后人工执行）

1. 录 ~10 min → 停 → 浏览器「下载 BIN」：文件大小 = 32+n×42；`ct_fetch_bin.py --from-bin` 解析 `count/seqEnd/flags=DONE` 一致。
2. 下载中途关标签页 → `/status`：`ntpServing=true`、无 `RSTR` 残留（xferBusy 复位）。
3. CLI 回归：`ct_fetch_bin.py` 在线分页照常。
4. 回卷路径已由现有钳制覆盖（代码审查）；满环 24 h 留下次长测自然验证。
