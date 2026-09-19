# 给执行板测 AI 的任务书（复制即用）

你只负责 **烧录 + 实测 + 回报**，不要改固件源码。仓库分支：`cursor/work-a05e`，固件 **v1.1.32**（`GPS_PPS_RMT_EN=1`）。

## 必读

完整步骤与判定：仓库内 `docs/rmt_pps_retest_v1132.md`  
（或：https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/docs/rmt_pps_retest_v1132.md ）

监控脚本：`tools/rmt_pps_monitor.py`

## 最短路径（S3，已在 IDF5）

1. 下载 app（核对 **1094864** 字节）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

2. 烧录（保 NVS）：`esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin`

3. 确认 `fwMark=v1.1.32`；串口须有：

```text
[pps-rmt] idf5 ready pin=4 ... (arm on first PPS)
```

模组开始出 PPS 后：

```text
[pps-rmt] idf5 armed after first PPS (count=…)
```

若 `init failed` → 立即 FAIL 并贴完整串口。建议冷启或断电 GNSS 再上电，验证 ready→armed 顺序。

4. 设备入网后（IP 常为 `192.168.1.24`，以现场为准）：

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v32.jsonl
```

开机尚未 PPS 时 `idfOk` 可为 true 而 `armed` 暂 false；有 PPS 并 LCK 后应 `armed=true`，且 `samples`/`lastWidthUs` 增长。

5. 可选：`ntpdate -q <IP>` 期望 stratum 1 / GPSS。

## 回报格式（原样填）

```text
RESULT: PASS|FAIL
fwMark:
serial_boot: (ready + armed lines)
monitor_verdict:
idfDataFrames / idfJunkFrames:
samples / active / lastWidthUs / deltaMeanUs:
fallbacks:
LCK ratio:
ntpdate:
notes: (冷启是否验证)
```

把 monitor 完整终端输出一并附上。PASS/FAIL 判定以 `docs/rmt_pps_retest_v1132.md` 为准。
