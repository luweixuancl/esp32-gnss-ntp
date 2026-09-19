# 时钟长测 — PSRAM/RAM 采样环（v1.1.37+）

> 设备侧按 PPS≈1 Hz 写入环形缓冲；**录制中禁止拉取**；停止后增量 CSV 下载。  
> 总览：[CURRENT.md](CURRENT.md) · 客户端：[`tools/clock_trace_client.py`](../tools/clock_trace_client.py)

## 为什么

长测若写 Flash / 文件系统会与授时、WiFi 抢 SPI；本方案只占 **PSRAM（S3）或内部 RAM（C3 小容量）**，HTTP 只在 **Stopped** 时读冻结缓冲。

## 状态机

```text
IDLE ──start──► REC ──stop──► STOP ──clear──► IDLE
                  ▲              │
                  └──── start（会先丢弃旧缓冲）─┘
```

| 状态 | 采样 | `GET /debug/clock/data` |
|---|---|---|
| `IDLE` | 否 | **409** |
| `REC` | 是（PPS 推进时） | **409** |
| `STOP` | 否（冻结） | **200** CSV（可增量） |

## API（均需登录 cookie 或 `?pass=` / `X-Debug-Pass`）

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/debug/clock` | JSON 状态 |
| `POST` | `/debug/clock/start` | 开始（已有 STOP 数据会被丢弃重开） |
| `POST` | `/debug/clock/stop` | 停止 → 允许下载 |
| `POST` | `/debug/clock/clear` | 清空（**REC 中拒绝**，须先 stop） |
| `GET` | `/debug/clock/data?from=&limit=` | **仅 STOP**；CSV 增量，默认 limit≈4000（单次上限 2000） |

`/status` 只读摘要字段 `clockTrace.{state,count,capacity,dropped,psram}`（无需口令）。

### 操作途径

1. **Web**：`/cfg` →「时钟长测」卡片（开始 / 停止 / 清空 / 下载）  
2. **CLI**：`tools/clock_trace_client.py`（推荐自动化）  
3. **curl**：见下

### curl 示例

```bash
PASS='NTP-9EC4'
IP=192.168.1.24
AUTH="pass=$PASS"

curl -fsS -X POST "http://$IP/debug/clock/start?$AUTH"
# … 跑若干小时 …
curl -fsS -X POST "http://$IP/debug/clock/stop?$AUTH"
# 增量拉全量到文件：
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" fetch -o clock.csv
curl -fsS -X POST "http://$IP/debug/clock/clear?$AUTH"
```

## CSV 列

`seq,uptimeMs,utcEpoch,ppsCount,residualMs,holdoverMs,freqPpm,tempC,tempCorrPpm,qualityMs,state,ppsFresh,timeValid,tempComp,satellites`

- `state`：`0=ACQ 1=LCK 2=DEG 3=HLD 4=UNS`  
- 首包含 `# clock_trace …` 元数据行；续包用 `?from=<next>`（见响应末尾 `# next=… done=0|1`）

## 容量

| 目标 | `CLOCK_TRACE_CAP` | 约时长 @1 Hz | 内存 |
|---|---|---|---|
| S3（`BOARD_HAS_PSRAM`） | 86400 | ~24 h | ~3.8 MB PSRAM |
| C3 | 3600 | ~1 h | 内部 RAM |

满环覆盖最旧样本，`dropped` 递增。

## 注意

- 断电丢失；不写 Flash。  
- 采样在 **task-time**（`GpsService::loop`），不进 ISR。  
- `CLOCK_TRACE_EN=0` 可编译关掉。  
- 与文本 `/debug/log` 分环，互不挤占。
