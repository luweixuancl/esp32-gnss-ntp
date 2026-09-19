# RMT PPS 复测任务书 — v1.1.32

> **历史任务书**（RMT 战役已结案 FAIL→搁置）。勿再按本文刷 EN=1 固件。结案见 [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)；当前 tip [CURRENT.md](CURRENT.md)。

## 相对 v1.1.31 的优化

**等 GPIO 见到第一发 1PPS 后再 `rmt_receive()`**（GNSS 冷启动 / 模组刚上电无 PPS 时，不再每 20 ms 空收 junk）。

开机串口期望：

```text
[pps-rmt] idf5 ready pin=4 ... (arm on first PPS)
```

模组开始出 PPS 后不久：

```text
[pps-rmt] idf5 armed after first PPS (count=…)
```

此前授时仍走 GPIO ISR → LocalClock（ACQ→LCK 行为不变）。

仍包含：WINDOW=20 + 钳位、ISR 符号拷贝、非 DMA、时间窗匹配、arm 失败保留 `gIdf.err`。

## 烧录（S3）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

`write_flash 0x10000`。确认 **`fwMark=v1.1.32`**。

建议：**冷启或断电 GNSS 再上电**，观察 ready→armed 两行顺序。

## 监控

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v32.jsonl
```

开机后若尚未 PPS：`idfOk` 可为 true 而 `armed` 暂 false；有 PPS 并 LCK 后应 `armed=true`，且 `samples`/`lastWidthUs` 增长。

## PASS

| 项 | 期望 |
|---|---|
| 串口 `ready … arm on first PPS` | 有 |
| 串口 `armed after first PPS` | 有（PPS 出现后） |
| `armed`/`idfOk`（监控窗后段） | true |
| `idfDataFrames`↑，`junk` 不大 | 是 |
| `active`/`samples`/`lastWidthUs` | 精化生效 |
| LCK / NTP S1 | 保持 |

## 回报

```text
RESULT: PASS|FAIL
fwMark:
serial_boot: (ready + armed lines)
monitor_verdict:
idfDataFrames / idfJunkFrames:
samples / active / lastWidthUs / deltaMeanUs:
fallbacks:
LCK ratio:
notes: (冷启是否验证)
```
