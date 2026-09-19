# 时钟长测 — PSRAM/RAM 采样环（v1.1.37+，传输优化 v1.1.38）

> 设备侧按 PPS≈1 Hz 写入环形缓冲；**录制中禁止拉取**；停止后下载。  
> 总览：[CURRENT.md](CURRENT.md) · 客户端：[`tools/clock_trace_client.py`](../tools/clock_trace_client.py)

## 为什么

长测若写 Flash 会与授时抢 SPI；本方案只占 **PSRAM（S3）或内部 RAM（C3）**。  
下载默认走 **二进制整页 + Content-Length**，期间 **停 NTP**（KoD `RSTR`）并抬高 task-net 优先级，避免 CSV 逐行慢写导致断联。

## 状态机

```text
IDLE ──start──► REC ──stop──► STOP ──clear──► IDLE
```

| 状态 | 采样 | `GET /debug/clock/data` |
|---|---|---|
| `IDLE` / `REC` | REC 时按 PPS | **409** |
| `STOP` | 否 | **200**（默认 bin） |

## API（登录 cookie 或 `?pass=` / `X-Debug-Pass`）

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/debug/clock` | JSON 状态 |
| `POST` | `/debug/clock/start` \| `stop` \| `clear` | 控制 |
| `GET` | `/debug/clock/data?format=bin&from=&limit=` | **推荐**：二进制页，默认 |
| `GET` | `/debug/clock/data?format=csv&…` | 文本 CSV（慢，仅小包/浏览器） |

下载进行中：`/status` 见 `xferBusy=true`、`ntpServing=false`、`ntp.refId=RSTR`。

### 二进制页（`CTRB`）

| 偏移 | 字段 |
|---|---|
| 0 | magic `CTRB` |
| 4 | `u16 version=1` · `u16 sampleSize=42` |
| 8 | `u32 seqFrom, count, seqNext, seqEnd, dropped, flags`（flags bit0=本会话已拉完） |
| 32 | `count × ClockTraceSample`（小端，与固件结构一致） |

默认 `limit=8000`（≈336 KiB/页），上限 12000。客户端自动翻页并在本地写成 CSV。

### 推荐用法

```bash
PASS='NTP-9EC4'
IP=192.168.1.24

python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" start
# … 录制 …
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" stop
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" fetch -o clock.csv
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" clear

# 或一键：
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" \
  capture --seconds 3600 -o hour.csv
```

`/cfg` 亦可：开始 / 停止 / 清空 / 下载 BIN（或 CSV）。

## CSV 列（客户端写出）

`seq,uptimeMs,utcEpoch,ppsCount,residualMs,holdoverMs,freqPpm,tempC,tempCorrPpm,qualityMs,state,ppsFresh,timeValid,tempComp,satellites`

`state`：`0=ACQ 1=LCK 2=DEG 3=HLD 4=UNS`

## 容量

| 目标 | `CLOCK_TRACE_CAP` | @1 Hz | 内存 |
|---|---|---|---|
| S3 PSRAM | 86400 | ~24 h | ~3.6 MB |
| C3 DRAM | 3600 | ~1 h | 内部 RAM |

## 注意

- 断电丢失；不写 Flash。  
- 采样在 task-time；下载在 task-net（停 NTP）。  
- 大包请用 CLI 二进制路径，勿在浏览器硬拉数万行 CSV。  
- `CLOCK_TRACE_EN=0` 可编译关掉。
