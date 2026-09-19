# 寄存器级 RMT RX PPS 驱动（开创性路径）

> 状态：**已实现 v1.1.22（观察态）** · 分支 `cursor/rmt-reg-pps-a05e`  
> 相关：TRM Ch.37、[s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md) §1、`include/rmt_pps_reg.h` / `src/rmt_pps_reg.cpp`

## 1. 为什么绕开 `driver/rmt.h`

Arduino-ESP32 2.0.17 / IDF 4.4.7 的 **legacy RMT RX** 在 S3 上只推送空环形缓冲项（已板测封存）。本驱动不调用该 API，直接按 **数据手册 / TRM 寄存器** 编程：

- `soc/rmt_struct.h` / `RMT` / `RMTMEM`
- `hal/rmt_ll.h`（寄存器级内联，非高层驱动）
- GPIO Matrix：`RMT_SIG_IN0..3` → LL RX 通道 0..3（HW ch4..7）

## 2. 实现范围（「完整」= PPS 捕获闭环）

| 步骤 | 内容 |
|------|------|
| 时钟 | `PERIPH_RMT` 使能；`sclk`=APB；通道分频 → 1 µs tick |
| 引脚 | `INPUT_PULLDOWN` + `gpio_matrix_in` → `RMT_SIG_IN3`（默认） |
| 通道 | LL RX **3** = HW **ch7**（手册唯一 RX-DMA 通道，为后续 DMA 预留） |
| 滤波 | `RX_FILTER_EN` + APB 周期阈值（`GPS_PPS_RMT_FILTER_NS`） |
| 结束条件 | `IDLE_THRES` ≈ `GPS_PPS_RMT_WINDOW_MS` |
| 中断 | `ETS_RMT_INTR` → `RX_END`：切 `MEM_OWNER`→APB，读 `RMTMEM`，重装 RX |
| 输出 | 边沿队列 + `/status.gps.ppsRmt.reg*` 诊断 |

**不做（本阶段）**：红外载波编解码、TX、多通道同步、用 RMT 边沿替换 GPIO 驯服主路径（避免未板测前扰动授时）。

## 3. 开关

```c
// include/config.h — S3 默认 1；C3 为 0
GPS_PPS_RMT_REG_EN
GPS_PPS_RMT_REG_RX_CH   // 默认 3
```

关闭：`build_flags = -DGPS_PPS_RMT_REG_EN=0`

## 4. 板测验收（本地）

1. 烧录 S3 v1.1.22，串口应见 `[pps-rmt-reg] armed gpio=4 llRx=3 hwCh=7 ...`
2. 有 PPS 后 `/status`：`regOk=true`，`regDataFrames` 递增，`regEmptyFrames` ≈0，`regLastSymbols`≥1
3. `regLastWidthUs` 应接近模块 PPS 高脉宽（通常百 µs～数 ms）
4. OLED RGB 仍正常（共享 RMT；驱动在 `sclk_active` 时不改写组时钟）
5. GPIO 驯服路径仍为主：LCK/S1 不依赖本驱动

通过后下一步：用 `reg` 边沿与 GPIO `edgeUs` 做差统计，再决定是否切入驯服主路径。

## 5. 手册对照摘要

- 8 通道共享 384×32-bit RAM；每通道默认 48 word  
- TX=ch0–3，RX=ch4–7；DMA：TX@3 / RX@7  
- 脉冲码：`level` + `period`；`period=0` 为结束标记  
- 配置变更需 `CONF_UPDATE`
