# GNSS NTP Server

基于 **ESP32 + GNSS 模块（PPS 驯服）** 的局域网一级（Stratum-1）NTP 时间服务器。同一份固件源码支持两个目标，通过 PlatformIO 多环境构建：

| 目标 | 板卡 | 核 | 状态 |
|------|------|----|------|
| **esp32-c3**（默认） | 合宙 CORE ESP32-C3 | 单核，三任务同核分优先级 | 产品线（v1.0.0 起量产验证） |
| **esp32-s3** | 乐鑫 ESP32-S3-DevKitC-1（WROOM-1 N16R8） | 双核，`task-time` 独占 core 1 | 已完成 P1–P4 全阶段验收 |

GNSS：大夏龙雀 DX-GP10（GPS/北斗/GLONASS 多模，9600 8N1，定位后 1PPS）。启动时探测 NMEA 后经 `$PCAS03` 仅保留 **GGA + ZDA**（坐标/星数/HDOP + 时间日期），减轻 UART 负载。

## 功能

1. **诚实 Stratum-1**：锁定/守时/未同步三态元数据如实在 NTP 报头呈现——锁定 `li=0 st=1 refid=GPSS`；守时仍 `st=1` 但色散随时间线性抬高；未同步 `li=3 st=16 refid=INIT` 拒绝授时
2. **本地时钟**：PPS 驯服 + NMEA 交叉检核（residual 告警）+ EMA 频偏估计 + Holdover 守时外推；异常策略可选 Refuse / Holdover 30s / 300s（NVS 持久化）
3. **OLED 状态页**：大字体本地时间、WiFi SSID/IP、星数·时钟态 / RSSI·AP 角标、SYNC/WAIT；无操作自动息屏防烧屏（档位可调，旋钮唤醒）
4. **旋转编码器菜单**：扫 WiFi、网页配网、静态 IP（ARP 冲突探测）、时区、异常策略、NTP ACL、温度补偿、息屏、NTP 统计、重启
5. **网页门户**：`/` 产品态状态页（徽章 S1/HLD/WAIT + 大时间外推；工程细节折叠）；`/setup` `/login` 登录后进 `/cfg`；`/status?view=ui` 瘦包 / `/status` 全量 / `/metrics` 只读开放
6. **WiFi 永久重连**：掉线后 30s 封顶退避无限重试；**绝不自动切 SoftAP**——配网仅在「开机无已存 SSID」或「菜单 Web Setup 手动触发」时开启
7. **NTP B1 限流 + B3 ACL**：每 IP 4 req/s 超限 KoD `RATE`、持续超限 `DENY` 冷却、全局 32 pkt/s 静默丢弃；ACL AllowList（默认 Off，最多 8 条）
8. **状态灯**：C3 双色 LED（D4 网络/D5 GNSS）；S3 板载 RGB 三通道（R=网络 / G=时钟 / B=NTP 授时中），语义见下
9. **B4 可观测**：OLED NTP Stats、串口 60s 摘要、`/metrics` Prometheus 文本
10. **多目标构建**：PlatformIO 多环境，源码 100% 共享，引脚差异集中在 `include/config.h` 目标条件宏
11. **Web OTA**：登录 `/cfg` 上传 `firmware.bin`；升级期间拒绝 NTP（KoD `RSTR`）并让出 CPU/Flash；琥珀/绿/红状态灯；启动确认后取消回滚；NVS 保留；串口升级仍为兜底
12. **外部时钟接口（可选）**：`ExtClock`（DS3231）；默认 `EXT_RTC_EN=0`；使能后改善 Holdover 色散地板；见 [ext_clock_design.md](docs/ext_clock_design.md)

## 硬件连接

### esp32-c3（合宙 CORE ESP32-C3）

| 模块 | 信号 | GPIO |
|------|------|------|
| DX-GP10 | TXD → RX | 1 |
| DX-GP10 | RXD ← TX | 0 |
| DX-GP10 | 1PPS | 4 |
| SH1107/SSD1107 OLED 64×128 | SDA / SCL | 8 / 10 |
| KY-040 编码器 | A / B / SW | 2 / 3 / 5 |
| 板载 LED | D4 / D5（高有效） | 12 / 13 |

调试串口：UART0 GPIO20/21（板载 CH343，115200）。`pio device monitor` 看的是调试口，不是 GPS。

### esp32-s3（DevKitC-1，WROOM-1 N16R8）

| 功能 | GPIO | 与 C3 差异说明 |
|------|------|----------------|
| GNSS UART RX/TX | 1 / 0 | 相同；GPIO0 为 strapping，仅输出驱动安全（异常可改 GPIO18） |
| PPS | 4 | 相同（RTC 域） |
| OLED I2C | 8 / 10 | 相同 |
| 编码器 A/B/SW | 2 / **9** / 5 | GPIO3 在 S3 是 strapping，B 脚移到 9 |
| 调试串口 RX/TX | **44 / 43**（UART 座） | 20/21 在 S3 是原生 USB；烧录/监控走板载 CP2102N 桥 |
| 状态灯 | 板载 RGB **@48** | C3 的 12/13 在 S3 为普通排针 |

16MB flash（default_16MB 分区表）+ 8MB OPI PSRAM。其余供电/接线与 C3 一致。引脚均可在 `include/config.h` 目标条件宏中修改。

### 状态灯语义

| 状态 | C3：D4（网络） | C3：D5（GNSS） | S3：RGB 合成 |
|------|----------------|----------------|--------------|
| WiFi 已连 | 心跳 | — | R 心跳 |
| 配网 AP | 快闪 ~4 Hz | — | R 快闪 |
| 未连任何网 | 慢闪 ~1 Hz | — | R 慢闪 |
| ACQ 搜星 | — | 慢闪 | G 慢闪 |
| **LCK/DEG 锁定** | — | 心跳（与 D4 反相） | G 心跳 + **B 常亮（NTP 授时中）**；OLED/网页徽章 **S1**（纯 LCK+PPS）或 **HLD**（DEG） |
| HLD 守时 | — | 快闪 | G 快闪 + B 常亮（降级仍授时）；徽章 **HLD** |
| UNS/无星 | — | 灭 | 灭；徽章 **WAIT** |
| **OTA 上传中** | 同步快闪 | 同步快闪 | **琥珀（R+G）快闪**；NTP 拒绝授时 |
| **OTA 成功待重启** | 灭 | 常亮 | **绿灯常亮** |
| **OTA 失败** | 红闪 ~2s | 灭 | **红闪 ~2s** |
| 任务卡死 | D4/D5 交替狂闪 ~5 Hz（→自动重启） | 〃 | R/G 交替狂闪 |

## 快速开始

```bash
# 1. 构建（默认 esp32-c3；S3 用 -e）
pio run
pio run -e esp32-s3

# 2. 烧录（升级用 app-only，保留 NVS 配置）
pio run -t upload                       # C3
pio run -e esp32-s3 -t upload           # S3（UART 座）

# 3. 监控
pio device monitor
```

或直接使用 `dist/` 现成固件：`firmware.bin`（C3 app @0x10000）、`firmware_esp32s3.bin`（S3 app @0x10000）、`merged_firmware_esp32s3_n16r8_0x0.bin`（S3 整片 @0x0，**仅首次烧录用，会清 NVS**）。esptool 示例：

```bash
esptool --chip esp32s3 --port COM5 --baud 921600 write_flash 0x0 merged_firmware_esp32s3_n16r8_0x0.bin
```

首次上电：无已存 WiFi → 自动开 `NTP-Setup-XXXX` 配网热点（密码默认 `NTP-`+MAC 后 4 位，串口打印）；或编码器「WiFi Scan」直连。**仅 2.4 GHz**。

## 操作说明

短按编码器进菜单，长按返回：

- **WiFi Scan**：扫描 → 选热点 → 旋转选字符、短按追加、长按连接
- **Web Setup**：打开配网热点，手机连上后访问 `http://192.168.4.1`
- **Set Static IP**：逐字节编辑，长按保存并做 ARP 冲突检测（IP 未变则不重连）
- **Use DHCP** / **Timezone**（默认 +8）
- **Anomaly Mode**：Refuse / Holdover 30s / Holdover 300s
- **NTP ACL**：Off / AllowList（名单在 `/cfg` 编辑）
- **Temp Comp**：片上温度一阶 ppm 补偿（默认 Off，**默认系数 0**；实测耦合仅 ≈ −0.05~−0.13 ppm/°C，受控标定前保持 0；修正上限 ±2 ppm）
- **Screen Off**：息屏档位 Always / 1 / 5 / 10 / 30 分钟
- **NTP Stats** / **Restart**

### 恢复出厂

冷开机按住编码器 3 秒（OLED 提示 `Hold 3s = reset`）：擦除全部配置并重启进配网；每次上电周期只复位一次（RTC 闩锁），3 秒内松开取消。

## 内网部署

面向局域网 Stratum-1，**不要**把 UDP/123 暴露到公网。

1. **限流（B1）**：每 IP 4 req/s → KoD `RATE`；持续超限 → `DENY` 冷却 ~60s；全局 ~32 pkt/s 静默丢弃
2. **ACL（B3）**：默认 Off；AllowList 只放行可信 IP（≤8 条），空名单=拒绝全部
3. **管理面**：`/` `/status` `/metrics` 只读；`/cfg` `/save` `/scan` `/ota` 需登录会话 Cookie；`/setup` 仅登录页
4. **升级保配置**：优先 Web OTA（`/cfg` 上传 app `firmware.bin`）或串口只刷 app `@0x10000`；勿全片擦除（NVS 里的 WiFi/口令/ACL 会丢）
5. **观测**：OLED NTP Stats、串口 `[ntp]` 摘要（60s）、`/metrics`；`/status` 字段含 `served` / `rateLimited` / `denied` / `dropped` / `clients` / `ntpAclMode` / `clock.tempRefC` / `fwVersion` / `otaRunning`

## 实测表现

| 场景 | 结果 |
|------|------|
| 锁定态 NTP 比对（C3，10 min） | 配对差 stdev 1.3–2.6 ms |
| 锁定态 NTP 比对（S3，10 min） | median −2.41 ms / stdev 4.34 ms / 异常 0/60 |
| Holdover 守时（拔模块电源 2 min，两芯） | 相位漂移 <0.5 ms |
| 失效链（两芯） | 断电 1.5–2.5s 进 HLD → 300s 准时 UNS 诚实拒绝 → 恢复 3–20s 无跳秒 |
| C3 6.7h / S3 6.1h 长测 | LCK ≈100%、residual 零漏、无老化漂移 |

详细数据：[docs/](docs/)——模块边界 [module_boundaries.md](docs/module_boundaries.md)、外部时钟 [ext_clock_design.md](docs/ext_clock_design.md)、状态显示方案 [status_display_product_design.md](docs/status_display_product_design.md)、时钟设计 [local_clock_gps_check.md](docs/local_clock_gps_check.md)、WiFi FSM [wifi_event_fsm.md](docs/wifi_event_fsm.md)、两次 NTP 比对评价 [clock_eval_two_ntp_cmp.md](docs/clock_eval_two_ntp_cmp.md)、C3 长测 [clock_drift_20260916.md](docs/clock_drift_20260916.md)、S3 验收 [esp32s3_flash_test_20260917.md](docs/esp32s3_flash_test_20260917.md)、S3 长测 [s3_clock_drift_20260917.md](docs/s3_clock_drift_20260917.md)。

## 客户端测试

```bash
ntpdate -q <设备IP>     # 期望 stratum 1, refid GPSS
chronyc sources
```

两机比对（Termux/PC，仅 Python 标准库）：

```bash
curl -L -o ntp_cmp_termux.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/tools/ntp_cmp_termux.py
python ntp_cmp_termux.py --gps <设备IP>     # 默认 10 min；--quick 冒烟 1 min
```

## 编译环境

PlatformIO + Arduino（espressif32）。注意：工程路径含非 ASCII 时 Windows `ld` 可能失败，用 ASCII junction（如 `C:\acode_leds`）再构建。

## 范围界定

**不做阶段 C**：NTS、HTTPS、PTP、硬件时间戳、TCXO 等机架级能力对 ESP32 + 无线平台成本过高或得不偿失，产品止于阶段 A+B 的内网轻量 Stratum-1。

## 目录结构

```
include/     配置与头文件（引脚目标条件宏；app_ipc 跨任务快照）
src/         固件源码（FreeRTOS 任务：time / net / ui；OtaService 等模块）
docs/        设计方案与测试报告（C3/S3 全系列）
tools/       辅助脚本（NTP 比对、失效链/长时段监测）
dist/        固件产物（C3 app / S3 app / S3 整片合并）
platformio.ini
```
