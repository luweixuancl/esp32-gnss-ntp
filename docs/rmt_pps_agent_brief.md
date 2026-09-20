# 工作分支说明（执行 / 辅助 AI）

**当前任务：把 v1.1.40 刷到现网 S3。** 完整步骤只看这一份：

→ **[fw_flash_v1140.md](fw_flash_v1140.md)**

- 镜像在分支 `cursor/clock-psram-ring-a05e`（提交 `64ffa66`），**不要从 `main` 下**（`main` 仍是 v1.1.39）
- S3 app：`dist/firmware_esp32s3.bin` · **1100704** B · SHA256 `aed6112f…e9670f0`
- 推荐 Web OTA；禁止整片擦除、禁止 C3 包、禁止 RMT EN=1

## 背景入口

- 总览：[CURRENT.md](CURRENT.md)
- 时钟长测环（v1.1.40 无参=全量）：[clock_trace.md](clock_trace.md)
- 上一轮 OTA 先例：[ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md)
- RMT 已结案，勿再开：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
