# 工作分支说明（执行 / 辅助 AI）

**进行中：** v1.1.42 烧录 + Hold 5m 失效链复测（恢复项）

→ 上一轮现场：[gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)  
  — 项 1–5 **PASS**；项 6 恢复 **FAIL**（PPS 重启死锁）；根因已修于 **v1.1.42**（`a2fcf55`）  
→ 原任务书：[gps_failover_hold5m_20260921.md](gps_failover_hold5m_20260921.md)（仍适用；监测仍用 `pps_failover_monitor.py`，无 ntpdate）

| 项 | 值 |
|---|---|
| 现网 | `10.121.95.14` · 口令 `NTP-9EC4` |
| 待刷 | **v1.1.42**（本分支 `cursor/gps-failover-brief-a05e`） |
| 复测 | 拔模块电源全链须到 **LCK**；可选 Refuse 对照 |
| 禁止 | 勿再以 v1.1.41 复测恢复项（已知死锁） |

更早已结：v1.1.41 OTA + `/cfg` 按钮 — [fw_flash_v1141_result_20260920.md](fw_flash_v1141_result_20260920.md)

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 史档 Hold 全链：[pps_pull_test_20260916.md](pps_pull_test_20260916.md)
- 宿主回归：`tools/local_clock_host_test/`（`g++` → `ALL PASS`）
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
