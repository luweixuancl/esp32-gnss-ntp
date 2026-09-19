# 工作分支说明（执行 AI）

当前固件 **v1.1.36**，`GPS_PPS_RMT_EN=0`。

RMT PPS 板测已结案 FAIL，见 [`docs/rmt_pps_board_test_CLOSED_20260919.md`](rmt_pps_board_test_CLOSED_20260919.md)。  
无需再跑 RMT monitor；若需确认服务面，烧录后 `ntpdate -q <IP>` / `GET /status` 即可。

免串口 log 仍可用：[`docs/debug_log.md`](debug_log.md)。

S3 固件：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```
