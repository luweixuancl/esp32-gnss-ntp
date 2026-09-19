# 时钟长测 — PSRAM/RAM 采样环（v1.1.37+，传输 v1.1.38，去 CSV v1.1.39）

> 设备侧按 PPS≈1 Hz 写入环形缓冲；**录制中禁止拉取**；停止后下**二进制**。  
> **板测 PASS**（10 min / 647 样本）— [clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md)  
> 总览：[CURRENT.md](CURRENT.md) · 客户端：[`tools/clock_trace_client.py`](../tools/clock_trace_client.py)

## 为什么

长测不写 Flash。下载仅 **`CTRB` 二进制页 + Content-Length**；期间 **停 NTP**（KoD `RSTR`）。  
设备端 **不再提供 CSV**（v1.1.39 起 `format=csv` → HTTP 410）；CSV 由 CLI 本地解码写出。

## 状态机

```text
IDLE ──start──► REC ──stop──► STOP ──clear──► IDLE
```

| 状态 | 采样 | `GET /debug/clock/data` |
|---|---|---|
| `IDLE` / `REC` | REC 时按 PPS | **409** |
| `STOP` | 否 | **200** 二进制 |

## API（登录 cookie 或 `?pass=` / `X-Debug-Pass`）

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/debug/clock` | JSON 状态 |
| `POST` | `/debug/clock/start` \| `stop` \| `clear` | 控制 |
| `GET` | `/debug/clock/data?from=&limit=` | 二进制页（唯一下载格式） |

下载中：`/status` 见 `xferBusy=true`、`ntpServing=false`、`ntp.refId=RSTR`。

### 二进制页（`CTRB`）

| 偏移 | 字段 |
|---|---|
| 0 | magic `CTRB` |
| 4 | `u16 version=1` · `u16 sampleSize=42` |
| 8 | `u32 seqFrom, count, seqNext, seqEnd, dropped, flags`（flags bit0=已拉完） |
| 32 | `count × ClockTraceSample`（小端） |

默认 `limit=8000`（≈336 KiB/页），上限 12000。

### 推荐用法

```bash
PASS='NTP-9EC4'
IP=192.168.1.24

python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" start
# … 录制 …
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" stop
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" fetch -o clock.csv
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" clear
```

`/cfg`：开始 / 停止 / 清空 / 下载 BIN（CSV 请用 CLI）。

## CSV 列（仅客户端写出）

`seq,uptimeMs,utcEpoch,ppsCount,residualMs,holdoverMs,freqPpm,tempC,tempCorrPpm,qualityMs,state,ppsFresh,timeValid,tempComp,satellites`

`state`：`0=ACQ 1=LCK 2=DEG 3=HLD 4=UNS`

## 容量

| 目标 | `CLOCK_TRACE_CAP` | @1 Hz | 内存 |
|---|---|---|---|
| S3 PSRAM | 86400 | ~24 h | ~3.6 MB |
| C3 DRAM | 3600 | ~1 h | 内部 RAM |

## 注意

- 断电丢失；不写 Flash。  
- `format=csv` 已移除（410）。  
- `CLOCK_TRACE_EN=0` 可编译关掉。
