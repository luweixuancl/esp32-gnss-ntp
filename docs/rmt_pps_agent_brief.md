# 工作分支说明（执行 / 辅助 AI）

**进行中：** GPS 失效链 Hold 5m（拔模块电源）

→ 任务书：[gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md)  
→ **Termux Alpine / 无 `ntpdate`**：只用 `tools/pps_failover_monitor.py`（Python 标准库）  
→ 结果写：`docs/gps_failover_hold5m_result_20260921.md`

| 项 | 值 |
|---|---|
| 现网 | `10.121.95.14` · 口令 `NTP-9EC4` |
| 固件 | 勿刷；现网多为 **v1.1.41**（`/status` 为准） |
| 策略 | 测前设 **Hold 5m**（`anomalyPolicy=2`） |
| 注入 | **拔 GPS 模块电源**（勿拔 PPS 杜邦线当主测） |

上一件已结：

→ v1.1.41 S3 OTA + `/cfg` 按钮 A–F **PASS** — [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md)

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 史档 Hold 全链：[pps_pull_test_20260916.md](pps_pull_test_20260916.md)
- 时钟长测环：[clock_trace.md](clock_trace.md)
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
