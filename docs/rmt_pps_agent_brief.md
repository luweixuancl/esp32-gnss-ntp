# 给执行板测 AI 的任务书（复制即用）

你只负责 **烧录 + 实测 + 回报**，不要改固件源码。仓库分支：`cursor/work-a05e`，固件 **v1.1.33**（`GPS_PPS_RMT_EN=1`）。

## 必读

完整步骤与判定：仓库内 `docs/rmt_pps_retest_v1133.md`

监控脚本：`tools/rmt_pps_monitor.py`

## 最短路径（S3，已在 IDF5）

1. 下载 app（核对 **1095312** 字节）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

2. 烧录（保 NVS）：`esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin`

3. 确认 `fwMark=v1.1.33`；串口须有 ready → armed，以及：

```text
[pps-rmt] dump n=… val0=0x…… d0=… l0=… d1=… l1=…
```

**务必把 dump 行原样贴回**（用于区分全零词 / 位域问题）。

4. 入网后：

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v33.jsonl
```

## 回报格式

```text
RESULT: PASS|FAIL
fwMark:
serial_boot: (ready + armed + dump lines)
monitor_verdict:
idfDataFrames / idfJunkFrames:
idfRawStatus:
samples / active / lastWidthUs:
LCK ratio:
notes:
```
