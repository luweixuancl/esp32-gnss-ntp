# RMT PPS 复测结果 — v1.1.30 FAIL（2026-09-19）

> 结论先行：**FAIL** —— RMT 首次 `rmt_receive()` 超硬件限制被拒，armed 永不置位。
> 时钟/NTP 服务面无回归（GPIO 兜底路径全程 LCK / stratum 1 GPSS）。

## §7/复测判定

```text
RESULT: FAIL
fwMark: v1.1.30
serial_boot: E (612) rmt: rmt_receive(397): signal_range_max_ns too big, should be less than 32767000 ns
             [pps-rmt] idf5 init failed stage=15 err=0 -> GPIO ISR only
monitor_verdict: 未跑（armed=false 死态，armed/idfOk 硬项不可能达标）
idfDataFrames: 0        idfEmptyFrames: 0        fallbacks: 0
LCK ratio: 100%（GPIO 路径）
ntpdate: stratum=1 refid=GPSS LI=0
```

## 根因（两处缺陷）

1. **配置超限（直接原因）**：`GPS_PPS_RMT_WINDOW_MS=50`（config.h:247）→
   `recvCfg.signal_range_max_ns = 50_000_000`（gps_service.cpp:291-292），
   超过 IDF5 RMT RX 空闲阈值上限 **32_767_000 ns**（15-bit @ 1 µs tick）。
   v1.1.29 的 20 ms（2e7 ns）合法、能 armed；v1.1.30 提到 50 ms 即触发本 FAIL。
2. **错误码被覆盖（诊断掩蔽）**：gps_service.cpp:327-337 ——
   `err == ESP_OK && rmtArmReceive()` 短路后，`rmtArmReceive()` 内部把真实
   esp_err 记入 `gIdf.err`，随后 else 分支 `gIdf.err = err`（外层 ESP_OK=0）
   覆盖之 → 串口与 `/status` 均只见 `stage=15 err=0`，真实错误只能靠
   ESP-IDF 的 `E (612) rmt:` 行。

## 修复建议（供主 AI）

- `GPS_PPS_RMT_WINDOW_MS` 回落 ≤32（如 v1.1.29 的 20），或
  `GPS_PPS_RMT_TICK_NS` 提到 2000（上限变 65 534 µs）再保 50 ms 窗。
- 删除 else 分支对 `gIdf.err` 的覆盖（rmtArmReceive 失败时保留内部 err），
  或打印 `gIdf.err` 而非外层 `err`。

## 证据文件

- `rmt_pps_retest_v1130_20260919.tar.gz`：
  `serial_boot_v1130.log`（用户 PC 串口全文）·
  `status_snapshot_fail_v1130.json`（armed=false / idfStage=15 / idfErr=0 快照）·
  `MANIFEST.txt`
