# RMT PPS 复测任务书 — v1.1.35

> **历史任务书**（RMT 战役已结案 FAIL→搁置）。勿再按本文刷 EN=1 固件。结案见 [rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)；当前 tip [CURRENT.md](CURRENT.md)。

## 相对 v1.1.34

`/debug/log` dump 已证实 raw 词仅为：

```text
0x00000000 / 0x80000000   （时长全 0，仅 level1 交替）
```

→ 非位域错位。v1.1.35 试：

1. **关闭 RGB `rgbLedWrite`**（RMT TX @10 MHz 与 PPS RX 同组）
2. **GPS RMT 先于 LED 初始化**
3. **`rtc_gpio_deinit(PPS)`**
4. **DMA RX**（失败则回退非 DMA）；ISR 仍逐词拷贝

## 烧录（S3）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

确认 `fwMark=v1.1.35`。板载 RGB 在此构建下**不亮**属预期。

## 免串口取 log

```bash
PASS='NTP-9EC4'   # 以设备为准
curl -fsS "http://192.168.1.24/debug/log?pass=$PASS" -o boot_log.txt
```

期望 dump 中 **`val0` 出现非零 duration**（例如 `d0` 或 `d1` ≫ 0），不再只有 `0x0`/`0x80000000`。

## 监控

```bash
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps_v35.jsonl
```

## PASS

| 项 | 期望 |
|---|---|
| dump `d0`/`d1` 非全 0 | 是 |
| `idfDataFrames`↑ | 是 |
| `active`/`samples`/`lastWidthUs` | 精化生效 |
| LCK / NTP S1 | 保持 |

## 回报

```text
RESULT: PASS|FAIL
fwMark:
debug_log dumps: (paste val0 lines)
monitor_verdict:
idfDataFrames / junk:
samples / active / lastWidthUs:
notes:
```
