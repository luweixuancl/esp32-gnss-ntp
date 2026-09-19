# RMT PPS 复测任务书 — v1.1.31

v1.1.30 **FAIL**：`GPS_PPS_RMT_WINDOW_MS=50` → `signal_range_max_ns` 超 IDF5 上限 32 767 000 ns，`rmt_receive` 拒收；且 init else 用 `ESP_OK` 盖掉真实 `gIdf.err`。

v1.1.31 修复：
- `WINDOW_MS` 回 **20**（并在代码里按 tick 钳位 ≤32767×tick_ns）
- arm 失败时**保留** `rmtArmReceive` 写入的 `gIdf.err`
- 仍保留 v1.1.30 的 ISR 符号拷贝 + 非 DMA + 时间窗匹配

## 烧录（S3）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

`write_flash 0x10000`。确认 **`fwMark=v1.1.31`**。

串口必须出现：

```text
[pps-rmt] idf5 armed pin=4 tick=1000ns win=20ms filter=1000ns dma=0
```

若仍 `init failed`：记录 `stage`/`err` 与任何 `E (...) rmt:` 行。

## 监控

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v31.jsonl
```

## PASS

| 项 | 期望 |
|---|---|
| armed 串口行 | 有 |
| `armed`/`idfOk` | true |
| `idfDataFrames` | 增长；`idfJunkFrames` ≪ data |
| `active` / `samples` / `lastWidthUs` | 精化合路生效 |
| LCK / NTP S1 | 保持 |

## 回报

```text
RESULT: PASS|FAIL
fwMark:
serial_boot:
monitor_verdict:
idfDataFrames / idfJunkFrames:
samples / active / lastWidthUs / deltaMeanUs:
fallbacks:
LCK ratio:
notes:
```
