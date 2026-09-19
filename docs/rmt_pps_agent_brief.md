# 给执行板测 AI 的任务书（复制即用）

你只负责 **烧录 + 实测 + 回报**，不要改固件源码。仓库分支：`cursor/work-a05e`，固件 **v1.1.29**（`GPS_PPS_RMT_EN=1`）。

## 必读

完整步骤与判定：仓库内 `docs/rmt_pps_board_test_v1129.md`  
（或：https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/docs/rmt_pps_board_test_v1129.md ）

监控脚本：`tools/rmt_pps_monitor.py`

## 最短路径（S3，已在 IDF5）

1. 下载 app（核对 **1094432** 字节）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

2. 烧录（保 NVS）：`esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin`

3. 确认 `fwMark=v1.1.29`；串口须有：

```text
[pps-rmt] idf5 armed pin=4 ...
```

若 `init failed` → 立即 FAIL 并贴完整串口。

4. 设备入网后（IP 常为 `192.168.1.24`，以现场为准）：

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps.jsonl
```

5. 可选：`ntpdate -q <IP>` 期望 stratum 1 / GPSS。

## 回报格式（原样填）

```text
RESULT: PASS|FAIL
fwMark:
serial_boot:
monitor_verdict:
idfDataFrames: start -> end
idfEmptyFrames:
fallbacks: start -> end
LCK ratio:
ntpdate:
notes:
```

把 monitor 完整终端输出一并附上。PASS/FAIL 判定以 `docs/rmt_pps_board_test_v1129.md` §7 为准。
