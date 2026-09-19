# 给执行板测 AI 的任务书（复制即用）

你只负责 **烧录 + 实测 + 回报**，不要改固件源码。仓库分支：`cursor/work-a05e`，固件 **v1.1.35**（`GPS_PPS_RMT_EN=1`）。

## 必读

- [`docs/rmt_pps_retest_v1135.md`](rmt_pps_retest_v1135.md)
- [`docs/debug_log.md`](debug_log.md)（免串口）
- 上轮结论：[`docs/rmt_pps_v1134_dump_analysis.md`](rmt_pps_v1134_dump_analysis.md)

## 最短路径

1. 烧录 `dist/firmware_esp32s3.bin` @ `0x10000`（字节数核对 **1097520** 字节）
2. 入网后：

```bash
PASS='NTP-XXXX'
curl -fsS "http://<IP>/debug/log?pass=$PASS" -o boot_log.txt
```

确认 `fw=v1.1.35`，并把全部 `[pps-rmt] dump` 行贴回。  
**关键：看 `val0`/`d0`/`d1` 是否仍只有 `0x0`/`0x80000000`。**

3. 监控 180 s：`rmt_pps_monitor.py --host <IP> --duration 180`

注：本构建板载 RGB **不亮**（避免 RMT TX 抢组）属预期。

## 回报

```text
RESULT: PASS|FAIL
fwMark:
debug_log dumps:
monitor_verdict:
idfDataFrames / idfJunkFrames:
idfRawStatus:
samples / active / lastWidthUs:
notes:
```
