# RMT PPS 复测任务书 — v1.1.30（相对 v1.1.29）

上一轮（v1.1.29）§7 硬项 PASS，但 **`active=false` / `samples=0` / 符号 1×零宽**：DMA 回调指针失效。  
v1.1.30：**ISR 拷贝符号 + 非 DMA + 时间窗匹配**；`idfDataFrames` 仅计解析成功；新增 `idfJunkFrames`。

## 烧录（S3，已在 IDF5）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

核对大小见分支 `dist/SHA256SUMS`。`write_flash 0x10000`。确认 **`fwMark=v1.1.30`**。

串口期望：`[pps-rmt] idf5 armed pin=4 ... dma=0`

## 监控

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v30.jsonl
```

## PASS 新增要求（相对 v1.1.29）

| 字段 | 期望 |
|---|---|
| `active` | 最终为 true（或 samples 明显增长） |
| `samples` | 随秒增加（约 ≥ duration/4） |
| `lastWidthUs` | 典型 1e4–5e5（如 ~1e5） |
| `idfJunkFrames` | 远小于 `idfDataFrames` |
| `deltaMeanUs` | 非永久 0（有 GPIO−RMT 差统计） |

仍要：LCK 稳定、NTP stratum 1。

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

附 monitor 全文。原始结果可打 tar 放 `docs/`。
