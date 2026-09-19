# 工作分支说明（执行 / 辅助 AI）

当前 **`main` = v1.1.39**，`GPS_PPS_RMT_EN=0`（RMT 板测已结案搁置）。

- 总览：[CURRENT.md](CURRENT.md)
- RMT 结案：[rmt_pps_board_test_CLOSED_20260919.md](rmt_pps_board_test_CLOSED_20260919.md)
- 时钟长测环：[clock_trace.md](clock_trace.md) · 板测 PASS：[clock_trace_boardtest_20260919.md](clock_trace_boardtest_20260919.md)
- RAM 调试 log：[debug_log.md](debug_log.md)

## 固件镜像（main）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/firmware_esp32s3.bin
```

S3 app @ `0x10000`（已在 IDF5）。自 IDF4 须整片：见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md)。

## 勿再开 RMT EN=1 整机盲测

需独立最小 sketch / GPIO 回环后再议。日常板测优先时钟环 CLI：

```bash
python3 tools/clock_trace_client.py --host <IP> --pass <PASS> capture --seconds 600 -o out.csv
```
