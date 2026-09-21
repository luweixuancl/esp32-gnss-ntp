# 工作分支说明（执行 / 辅助 AI）

**无进行中任务。** 上一件已结：

→ v1.1.42 OTA + Hold 5m 全链 **PASS** + Refuse 对照 **PASS** — [fw_flash_v1142_result_20260921.md](fw_flash_v1142_result_20260921.md)

- 现网 `10.121.95.14`：`fwMark=v1.1.42`，Hold 5m，LCK / NTP S1 GPSS
- ACQ→LCK **2.9 s**（修复 v1.1.41 断电恢复死锁）；Refuse：`LCK→UNS` 无 HLD
- 任务书已完成，勿再刷：[fw_flash_v1142.md](fw_flash_v1142.md)

更早：v1.1.41 恢复 FAIL（已修）— [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 史档 Hold 全链：[pps_pull_test_20260916.md](pps_pull_test_20260916.md)
- 宿主回归：`tools/local_clock_host_test/`（6 场景 `ALL PASS`）
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
