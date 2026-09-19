# 给执行板测 AI 的任务书（复制即用）

你只负责 **烧录 + 实测 + 回报**，不要改固件源码。仓库分支：`cursor/work-a05e`，固件 **v1.1.34**（`GPS_PPS_RMT_EN=1`）。

## 必读

- RMT 判定：`docs/rmt_pps_retest_v1133.md`（符号 dump 等硬项仍适用；fw 以本页 v1.1.34 为准）
- **免串口拉 boot log**：`docs/debug_log.md`

## 最短路径（S3，已在 IDF5）

1. 下载 app（核对 **1097520** 字节 / `dist/SHA256SUMS`）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

2. 烧录（保 NVS）：`esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin`

3. **可不挂串口**：设备入网后（IP 常为 `192.168.1.24`）：

```bash
# 口令默认 SoftAP 同款（OLED 可见；或上次串口 SoftAP default pass=）
PASS='NTP-XXXX'
curl -fsS "http://192.168.1.24/debug/log?pass=$PASS" -o boot_log.txt
# 确认 fw=v1.1.34，并保留 [pps-rmt] ready/armed/dump 行
```

若仍接串口：确认 `fwMark=v1.1.34` 与 dump 行。

4. 监控：

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v34.jsonl
```

## 回报格式

```text
RESULT: PASS|FAIL
fwMark:
debug_log: (paste [pps-rmt] ready/armed/dump from /debug/log)
monitor_verdict:
idfDataFrames / idfJunkFrames:
idfRawStatus:
samples / active / lastWidthUs:
LCK ratio:
notes:
```
