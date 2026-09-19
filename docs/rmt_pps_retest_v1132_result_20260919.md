# RMT PPS 复测结果 — v1.1.32 FAIL（2026-09-19，第 3 轮）

> **历史结果**。结案汇总：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)。当前 tip [CURRENT.md](CURRENT.md)。

> 结论先行：**FAIL** —— v1.1.31/32 修复了 armed 链路（init 全 31 阶段、首个 PPS 后成功
> armed），但 **RMT 收到的每帧符号时长恒为 0**，解析 100% 进 junk，精化合路仍为 0。
> 时钟/NTP 服务面依旧无回归（GPIO 路径全程 LCK / stratum 1 GPSS）。

## §8 回报

```text
RESULT: FAIL
fwMark: v1.1.32
serial_boot: [pps-rmt] idf5 ready pin=4 tick=1000ns win=20ms filter=1000ns dma=0 (arm on first PPS)
             [pps-rmt] idf5 armed after first PPS (count=1)        <- v1.1.29 时期望行为恢复
monitor_verdict: VERDICT: FAIL
                 (armed 82/82, LCK 82/82=100%, errors=0,
                  idfDataFrames 0 -> 0 < need 90,
                  refine samples=0, lastWidthUs=0)
idfDataFrames: 0 -> 0 (整个观测窗 1259 帧全进 idfJunkFrames)
idfEmptyFrames: 0
fallbacks: 0 -> 0
LCK ratio: 82/82 = 100%
ntpdate: stratum=1 refid=GPSS LI=0
notes:
  ★ 关键症状（比上一轮更聚焦）: 每帧恰好 1 个符号(idfFirstSyms=idfLastSyms=1)，
    但符号两个半区时长恒为 0(idfLastD0Us=0, idfLastD1Us=0，终态 1259/1259 帧)。
    解析器 rmtProcessSymbols 走不到任何 0->1 沿 -> !haveEdge -> junk，
    oddPulse=0（脉宽门从未触发，与「时长 0」自洽）。
  - 即：驱动层"零时长符号"老病在 ISR memcpy 拷贝后依然存在 ——
    on_recv_done 里 edata->received_symbols / num_symbols=1 拿到的就是空词。
    嫌疑方向：非 DMA 模式下 done 事件发生时缓冲已被清/重排、
    或 S3 上该 channel 的符号编码与 rmt_symbol_word_t 位域假设不符
    （level 位在 bit15/bit31、duration 15-bit 的解析请对照实际词值 dump）。
  - 建议：在 on_recv_done 里把首帧原始 32-bit 词值原样打到串口一行
    （hex），一次就能定位是「全零词」还是「位域错位」。
  - armed/idfOk/init 链路已完全恢复（v1.1.31/32 的两处修复有效）。
```

## 证据文件（rmt_pps_retest_v1132_20260919.tar.gz）

- `serial_boot_v1132.log` — 用户 PC 串口 boot 全文（含 armed after first PPS）
- `rmt_pps_v32_console.log` — monitor 180 s 完整输出（VERDICT: FAIL）
- `rmt_pps_v32.jsonl` — 82 条 /status 原始采样
- `status_snapshot_fail_v1132.json` — 终态快照（frames=1259 junk=1259 D0=D1=0）
- `rmt_pps_monitor_v32.py` — 实际用脚本（仓库版 + except 子句放宽一行，判定逻辑未动）
- `MANIFEST.txt`
