# RMT PPS 板测结果 — v1.1.29（2026-09-19）

> 原始包：[`rmt_pps_board_test_20260919.tar.gz`](rmt_pps_board_test_20260919.tar.gz)  
> 设备：S3 @ `192.168.1.24` · 固件 **v1.1.29** · `GPS_PPS_RMT_EN=1`  
> 流程：[rmt_pps_board_test_v1129.md](rmt_pps_board_test_v1129.md)

## 判定摘要

| 项 | 结果 |
|---|---|
| §7 硬项 A/C/D/E/F + G（NTP） | **PASS**（执行 AI / monitor `VERDICT: PASS`） |
| 精化路径 `active` / `samples` / `deltaMeanUs` | **未达成**（全程 0 / false） |

### Monitor 数字（180 s）

- `fwMark=v1.1.29`，`armed=1`，`idfOk=1`，`idfStage=31`，`idfErr=0`
- `idfDataFrames` 1320→1680（+360，约 **2/s**），`idfEmptyFrames=0`，`fallbacks` Δ0
- `LCK` 74/75（≈98.7%）；NTP stratum 1 / GPSS
- **`active=false`，`samples=0`，`lastWidthUs=0`，`idfLastSyms=1`，`idfLastD0Us=0`，`idfLastD1Us=0`**

## 根因（主 AI 分析）

`idfDataFrames` 增长只说明 done 回调在跑；**每个“数据帧”只有 1 个符号且 duration 全 0**，`rmtProcessSymbols` 找不到上升沿 → 精化队列为空 → `active` 永不置位。

高度疑似：**DMA RX 下 `on_recv_done` 只把 `edata` 指针入队，回调返回后 DMA 缓冲已失效**，任务稍后读到脏/空符号。GPIO ISR 仍正常（`ppsCount` 递增、LCK 正常），故授时不崩，但 RMT 精化等于空转。

## 后续

v1.1.30：ISR 内拷贝符号；S3 默认非 DMA；仅真正解析成功才计 `idfDataFrames`；时间窗匹配兜底。见工作分支后续提交与重测 brief。
