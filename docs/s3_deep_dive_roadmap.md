# S3 特性深挖路线图（PPS 硬件捕获 / PSRAM 历史 / OTA / 外部时钟）

> 状态：立项排序已定（2026-09-18：**① RMT【已封存】→ ② OTA【板测通过 v1.1.7】→ ③ PSRAM 历史【已取消 v1.1.14】→ ④ 外部时钟【接口 v1.1.15，待购件开 EN】**）
> 背景：S3 移植转正后的特性深挖规划，基于 2026-09-18 代码/特性审查；各项目启动前按本档「验收准则」细化
> 修订：③ 已取消；④ 见 [ext_clock_design.md](ext_clock_design.md)
> 相关：[esp32s3_devkitc1_hw.md](esp32s3_devkitc1_hw.md)、[esp32s3_flash_test_20260917.md](esp32s3_flash_test_20260917.md)、[s3_clock_drift_20260917.md](s3_clock_drift_20260917.md)

## 0. 现状基线

| S3 特性 | 现状 |
|---|---|
| 双核 LX7 @240MHz | ✅ task-time 独占 core 1（移植核心收益） |
| 16MB QIO flash | ✅ default_16MB 分区；app1/OTA 槽位经 Web `/ota` 可写 |
| 8MB OPI PSRAM | 闲置（history 已取消）；可留给后续缓冲 |
| 新版温度传感器 | ✅ `temperatureRead()`（tcmp 显示/日志） |
| RMT 4TX | 1 路用于 RGB 状态灯；**RX 4 路全闲置** |
| 3× UART | 2 路在用（调试/GNSS），第 3 路闲置 |
| USB-OTG | 刻意不用（CDC_ON_BOOT=0，走 UART 座） |

## 1. 项目一：RMT RX 硬件捕获 PPS 边沿（第 1 位）——**已尝试、平台级封存（2026-09-18）**

- **结局**：四轮探针后诚实封存。根因不在应用层：Arduino-ESP32 2.0.17 / IDF 4.4.7 的 **legacy RMT 驱动在 S3 上的 RX 数据通路只推送空环形缓冲项**（每边沿精确 2 个空帧、零符号数据），HAL `rmtRead(cb)` 与直驱（显式通道/滤波/阈值/`RMT_MEM_OWNER_RX` 认领/`rmt_set_pin` 补路由）两条路径同样空帧——共同层即驱动本身；`rmt_get_status` 原始寄存器恒 `0x2a8150`。
- **附带发现**：为合并精化时间戳而加的「边沿扣留」机制会扰动 NMEA 交叉检核时序边距（±1000ms → LCK/ACQ 每百秒抖动），即使精化流为零也发生——该机制随 `GPS_PPS_RMT_EN=0` 整体剔除，设备已恢复长测级稳定。
- **重开条件**：迁移 Arduino 3.x / IDF 5（新 `rmt` 驱动 + S3 RX 专属通道模型）后再评估；代码保留（`GPS_PPS_RMT_EN` 置 1 即回实验态），探索过程全部入档。
- **验收准则/工作量**：见下（供重开时引用）

## 2. 项目二：PSRAM 诊断环形缓冲 + `/history`（第 3 位）——**已取消（v1.1.14）**

- 自录环 / `/history` / CSV / 录制开关已从固件移除（用户要求取消）。
- PSRAM 仍可留给后续特性（如外部时钟缓冲）。

## 3. 项目三：OTA 双分区升级——**已实现并板测通过（v1.1.7）**

- **现状**：`POST /ota`（登录会话）+ `/cfg` 上传 UI（进度条 + 完成/重启提示）；`Update` 写下一 app 槽；启动后延迟 `esp_ota_mark_app_valid_cancel_rollback`（任务存活 ≥30s）；串口升级仍为兜底。
- **升级窗口策略**：OTA busy 期间拒绝 NTP（KoD `RSTR`）、暂停 GPS/UI/WiFi 扫描以让出 CPU/Flash；状态灯琥珀快闪→绿常亮→红闪失败。
- **健壮性**：LED stale panic / heap-low 重启豁免；镜像 magic+chip_id 校验拒绝跨 C3/S3；未授权断连；Content-Length 超槽拒绝。
- **方案**：`OtaService`（task-net 独占写路径）经 `app_ipc` 发布 `otaBusy/otaPhase`；`WebPortal` 仅 HTTP 适配；`status_leds`/`task-time` 只读 IPC；升级时 `vTaskPrioritySet` 提升 net 高于 time。
- **验收**：C3/S3 Web OTA 往返已通过（含进度 UI）；NVS 保留。

## 4. 项目四：外部高品质时钟源（最后，硬件依赖）——**接口已落地（v1.1.15），待购件**

- **方案**：[ext_clock_design.md](ext_clock_design.md)（推荐 DS3231）
- **代码**：`ExtClock` + `LocalClock::setExtAssist`；默认 `EXT_RTC_EN=0`（无 I2C）；`/status.extClock`
- **Holdover**：RTC healthy 时用 `EXT_RTC_PPM_FLOOR`（2 ppm）作色散地板并跳过 PHI 垫高
- **开使能**：焊接 DS3231 到 OLED 同总线（`0x68`）后把 `EXT_RTC_EN` 置 1 重编
- **验收**：拔 GNSS 后漂移优于板载基线；EN=0 时行为与今日相同

## 5. 明确不做

- **NTS/加密 NTP**：有 AES/SHA 加速器也不做——局域网场景，阶段 C 界定不变
- **IDF5 / Arduino 3.x 迁移**：迁移成本 >> 收益，当前 2.0.17/IDF4.4 稳定运行
- **触摸 / LCD 外设 / 802.11mc**：与产品无关

## 6. 排序备忘

**① RMT 捕获（已封存）→ ② OTA（已验收）→ ③ PSRAM 历史（已取消）→ ④ 外部时钟（接口已合入，待购件开 EN）**

> ④ 默认关闭；购 DS3231 并置 `EXT_RTC_EN=1` 后做拔天线验收。
