# 外部高品质时钟源（方案草案）

> 状态：**草案 / 未立项采购**（2026-09-18）  
> 路线图项：④（① RMT 封存 · ② OTA 验收 · ③ PSRAM history 已取消 → 本项）  
> 相关：[s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md)、[local_clock_gps_check.md](local_clock_gps_check.md)

## 1. 为什么要做

Holdover 上限 ≈ 板载晶振：

| 板 | 典型 freqPpm | 300 s 守时粗界 |
|----|--------------|----------------|
| C3 | −1～−3 ppm | ~0.3–0.9 ms |
| S3 | ≈ −12.5 ppm | ~3.8 ms |

产品若要「拔天线后仍可用 NTP 数十分钟～数小时」，必须换更好的自由运转参考，而不是再拧软件 PLL。

## 2. 候选路径（定案前三选一）

| 方案 | 硬件 | 改动面 | 预期 Holdover | 成本/难度 |
|------|------|--------|---------------|-----------|
| **A. DS3231 类 TCXO RTC（推荐首研）** | I2C RTC，±2 ppm 级 | 新 `ExtClock` 模块 + 可选 PPS/方波 | 300 s ≈ 0.6 ms | 低：模块现成、3V3 I2C |
| **B. 外部 OCXO** | 恒温槽晶振 + 驯服/计数 | 计数器或第二 PPS | 小时级 | 高：供电、体积、EMI |
| **C. 第二 PPS 输入** | 外部 1PPS（另一 GNSS/原子钟） | GPIO + ISR 交叉校验 | 取决于外源 | 中：要第二路线 |

**建议首研 A**：与现有「PPS 走相 + 色散诚实」模型最贴合——Holdover 时用 RTC 秒尺替代板载 `esp_timer` 斜率，或对 `freqPpm` 做 RTC 辅助估频。

## 3. 软件接入点（不绑定具体芯片前的接口）

```text
LocalClock
  ├─ onPpsEdge / NMEA          （现状 GNSS）
  └─ ExtClockSource (new)      （可选）
        nowUs() / ppmHint() / healthy()
```

原则：

- **有 GNSS PPS 且 Locked**：仍以 GNSS 为准（不降级为 RTC 授时）。
- **Holdover**：`effectivePpm` 可融合 ExtClock 提示；或直接用 ExtClock 单调轴外推 UTC。
- **Unsynced / 无 ExtClock**：行为与今日相同。
- C3/S3 共用接口；无焊接时 `enabled=false`。

## 4. 硬件草图（方案 A）

| 信号 | ESP32 | 备注 |
|------|-------|------|
| 3V3 / GND | 共地 | 与 GNSS 同电源域需注意掉电策略 |
| SDA/SCL | 空闲 I2C（避开 OLED 总线或共享 + 地址） | DS3231 默认 `0x68`；OLED `0x3C` |
| SQW（可选） | GPIO 输入 | 1 Hz 方波作辅 PPS；首版可不接 |

引脚进 `config.h` 目标宏；默认可 `#define EXT_RTC_EN 0`。

## 5. 验收（采购后）

1. Locked 时 NTP 行为与无 RTC 固件无回归。  
2. 拔 GNSS 电源进入 HLD：`freqPpm`/色散曲线明显优于板载晶振基线（同窗对比）。  
3. DS3231：300 s Holdover 设备侧质量/外部比对符合 ±2 ppm 量级。  
4. 未焊接 / `EXT_RTC_EN=0`：C3/S3 均正常启动。

## 6. 明确不做（本项范围外）

- 用 RTC 在有 GNSS 时抢 stratum-1（禁止）。  
- NTS / 公网授时。  
- 未选型前写死某品牌驱动进主干（先接口 + 一种参考实现）。

## 7. 下一步（需你确认采购后再写代码）

1. 选定模块（推荐 DS3231 模块带电池）。  
2. 定 S3/C3 的 I2C 引脚与是否用 SQW。  
3. 实现 `ExtClock` + `LocalClock` Holdover 挂钩 + `/status` 字段。  
4. 拔天线对比长测。
