# 工作分支说明（执行 / 辅助 AI）

**当前任务：OTA v1.1.41 + 测 `/cfg` 时钟长测按钮。** 完整步骤只看这一份：

→ **[fw_flash_v1141.md](fw_flash_v1141.md)**

- 镜像在分支 `cursor/clock-psram-ring-a05e`（提交 `7d2c47d`），**不要从 `main` 下**
- S3 app：`dist/firmware_esp32s3.bin` · **1102992** B · SHA256 `f4766d90…7124dba`
- 上场设备：`10.121.95.14`（v1.1.40）→ 刷到 **v1.1.41**
- 重点：浏览器打开 `/cfg`，四键必须跟 IDLE/REC/STOP 走；不要只 curl
- 禁止整片擦除、禁止 C3 包、禁止 RMT EN=1

上一件（v1.1.40 OTA）已 PASS — [fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md)

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 时钟长测环：[clock_trace.md](clock_trace.md)
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
