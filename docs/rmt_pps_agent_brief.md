# 工作分支说明（执行 / 辅助 AI）

**进行中：** 守时精度长测（v1.1.43）

→ 任务书：[holdover_precision_v1143.md](holdover_precision_v1143.md)  
→ 新守时档：30s / 5m / **15m / 30m / 1h / 2h** + Refuse  
→ 用 **时钟长测 (PSRAM)**（HLD 段墙钟 1 Hz 补样）+ **`/debug/log`**  
→ 推荐主测 **Hold 30m**；结果写 `docs/holdover_precision_v1143_result_20260921.md`

| 项 | 值 |
|---|---|
| 现网 | `10.121.95.14` · 口令 `NTP-9EC4` |
| 固件 | **刷 v1.1.43**（本分支） |
| 策略 | `anomalyPolicy=4`（Hold 30m） |
| 注入 | 拔 GPS **模块电源** |

上一件已结：v1.1.42 Hold 5m + Refuse **PASS** — [fw_flash_v1142_result_20260921.md](fw_flash_v1142_result_20260921.md)

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 时钟长测：[clock_trace.md](clock_trace.md)
- debug log：[debug_log.md](debug_log.md)
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
