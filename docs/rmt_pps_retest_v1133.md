# RMT PPS 复测任务书 — v1.1.33

> **历史任务书**（RMT 战役已结案 FAIL→搁置）。勿再按本文刷 EN=1 固件。结案见 [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)；当前 tip [CURRENT.md](CURRENT.md)。

## 相对 v1.1.32

v1.1.32 **armed 链路 PASS**，但 1259 帧全是 **1×零时长符号** → 100% junk，精化仍为 0。

v1.1.33 针对该症状：

1. **串口 dump 首 16 帧** raw `val0=0x……` + `d0/l0/d1/l1`（定位全零词 vs 位域错位）
2. `/status` `idfRawStatus` 改为末符号 `.val`
3. `mem_block_symbols=48`（S3 单通道原生深度；先前 64 会侵占邻道）
4. glitch filter **关**（`FILTER_NS=0`）
5. **先建 RMT 再 `attachInterrupt`**；RMT 默认 pull-up 后改回 pulldown
6. 双缓冲 `rmt_receive`；ISR 逐词拷贝

仍含：defer-arm、WINDOW=20、非 DMA。

## 烧录（S3）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

`write_flash 0x10000`。确认 **`fwMark=v1.1.33`**。

## 串口关键行

```text
[pps-rmt] idf5 ready pin=4 ... mem=48 ch=… realHz=1000000 (arm on first PPS)
[pps-rmt] idf5 armed after first PPS (count=…)
[pps-rmt] dump n=… val0=0x…… d0=… l0=… d1=… l1=…
```

**请把至少前几行 `dump` 原样贴回**（即使仍 FAIL）。

## 监控

```bash
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v33.jsonl
```

## PASS

| 项 | 期望 |
|---|---|
| dump `val0` 非 `0x00000000`（或能解释的非零时长） | 是 |
| `armed`/`idfOk` | true |
| `idfDataFrames`↑，`junk` 不大 | 是 |
| `active`/`samples`/`lastWidthUs` | 精化生效 |
| LCK / NTP S1 | 保持 |

## 回报

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
