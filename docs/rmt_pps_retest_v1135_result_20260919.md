# RMT PPS 复测结果 — v1.1.35 FAIL（2026-09-19，第 4 轮）

> 结论先行：**FAIL** —— DMA 尝试、跳过 RGB RMT TX、`rtc_gpio_deinit` 三项排除后，
> 零时长符号**原样存在**（dump 仍全 `0x00000000`）。armed/init 链路稳定正常；
> 时钟/NTP 服务面持续无回归。

## §8 回报

```text
RESULT: FAIL
fwMark: v1.1.35
serial_boot: [pps-rmt] idf5 ready pin=4 tick=1000ns win=20ms filter=0ns dma=1 mem=48 ch=3
             realHz=1000000 (arm on first PPS)
             [pps-rmt] idf5 armed after first PPS (count=2)
             （本机无串口；行取自 /debug/log，验证一致）
monitor_verdict: VERDICT: FAIL
                 (armed 78/78, LCK 78/78=100%, errors=0,
                  idfDataFrames 0 -> 0 < need 90,
                  refine samples=0, lastWidthUs=0)
idfDataFrames: 0 -> 0 (终态 1033 帧全 junk)
idfEmptyFrames: 0
fallbacks: 0 -> 0
LCK ratio: 78/78 = 100%
ntpdate: stratum=1 refid=GPSS LI=0
notes:
  ★ v1.1.35 排除项(均无效): RGB RMT TX 已让位、rtc_gpio_deinit 已做、DMA=1(ch=3)
    —— dump 仍 100% val0=0x00000000，与 v1.1.34 非 DMA 时一致。
  ★ 关键不变量: done 事件节奏仍随 PPS (~2 帧/s)，说明边沿中断/会话终结在发生，
    但 RAM 词内容恒零 -> RX 采样写入路径(RX RAM/DMA 缓冲)死，
    而非事件路径死。
  - 下一层定位建议(供主 AI):
    a) GPIO 回环自测: 本板一个 GPIO TX 输出已知波形 -> gpio_matrix 环回
       到 RX 通道, 若词值仍零则与外部引脚/信号无关, 纯通道/驱动问题;
    b) dump RMT 外设寄存器(RX 状态/时钟门控)一行 hex, 对照 TRM;
    c) 核对 GPIO4 的 matrix-in 路由与 IOMUX 状态(rtc_gpio_deinit 之外,
       确认无 Strapping/USB-JTAG 复用抢占);
    d) 对照 pioarduino/IDF 5.5.5 的 rmt_rx 例程最小复现(独立 sketch)
       以剥离本项目其余代码。
  - armed/idfOk/init 链路三连版稳定正常; GPS/NTP/时钟服务面健康。
```

## 证据文件（rmt_pps_retest_v1135_20260919.tar.gz）

- `debug_log_v1135.txt` — /debug/log 全文（ready dma=1 行 + armed + 全零 dump）
- `rmt_pps_v35_console.log` — monitor 180 s 完整输出（VERDICT: FAIL）
- `rmt_pps_v35.jsonl` — 78 条 /status 原始采样
- `status_snapshot_fail_v1135.json` — 终态快照（frames=1033 junk=1033）
- `rmt_pps_monitor_v35.py` — 实际用脚本（仓库版 + except 放宽一行，判定未动）
- `MANIFEST.txt`
