# ESP32-S3-DevKitC-1 硬件笔记与移植规划（N16R8）

> 状态：**移植完成并转正**；平台 IDF5；当前 tip **v1.1.39**（见 [CURRENT.md](CURRENT.md)、[idf5_adapt_20260919.md](idf5_adapt_20260919.md)）  
> 板卡：乐鑫 **ESP32-S3-DevKitC-1 V1.1**，模组 **ESP32-S3-WROOM-1 N16R8**（16MB QIO flash + 8MB OPI PSRAM）  
> 资料：[`芯片资料/ESP32S3/`](../芯片资料/ESP32S3/)（引脚图 / 原理图 / 数据手册）  
> 相关：[wifi_event_fsm.md](wifi_event_fsm.md)、[s3_deep_dive_roadmap.md](s3_deep_dive_roadmap.md)、[power_save.md](power_save.md)

## 1. 为什么值得移植

- **双核 LX7**：`task-time`（GNSS/NTP/PPS）独占 core 1，`task-net`/`task-ui` 在 core 0。默认运行 **160 MHz**（可编回 240）。
- 512KB SRAM、指令/数据 cache 更大；USB-OTG 原生可用（本工程不用 CDC）。
- 引脚全部走 `config.h` 宏；任务结构可拆分。

## 2. 板卡关键事实

| 项 | 事实 |
|---|---|
| 调试串口 | UART0 = **GPIO43(TXD0)/44(RXD0)**，板载 USB-UART 桥（板上 UART 座） |
| 原生 USB | **GPIO19(D-)/20(D+)**，USB-OTG + JTAG |
| RGB LED | WS2812-class RGB @ **GPIO48（本板实测）**——原装 V1.1 原理图走 GPIO38，本板为兼容走线（2026-09-17 现场确认：38 不亮/48 亮） |
| 按键 | BOOT=GPIO0（strapping），RESET（EN） |
| **Strapping 禁区** | **GPIO0 / GPIO3 / GPIO45 / GPIO46**——启动期有采样用途，外设尽量避开 |
| 32K 晶振脚 | GPIO15/16（未接外部 32k 时可当普通 GPIO） |
| 供电 | 3V3 / 5V 排针；外设（GNSS/OLED/编码器）供电与 C3 方案相同 |

## 3. 移植引脚映射草案（定稿在移植执行时锁）

| 功能 | C3（现行 config.h） | S3 草案 | 备注 |
|---|---|---|---|
| GNSS UART1 RX/TX | 1 / 0 | **1 / 0 沿用** | GPIO 矩阵任意映射，无冲突 |
| GNSS PPS | 4 | **4 沿用** | RTC 域 + `INPUT_PULLDOWN`，理想 |
| OLED I2C SDA/SCL | 8 / 10 | **8 / 10 沿用** | Wire 可任意脚 |
| 编码器 A/B/SW | 2 / 3 / 5 | 2 / **9** / 5 | ⚠️ GPIO3 是 strapping，B 脚必须挪 |
| 状态 LED D4/D5 | 12 / 13 | 12 / 13 排针保留；**状态显示走板载 RGB@48**（D4→R、D5→G 合成，`status_leds.cpp` S3 分支 + `LED_RGB_BRIGHTNESS=30`） |
| 调试串口 RX/TX | 20 / 21 | **44 / 43** | ⚠️ C3 的 20/21 在 S3 是 USB D-/D+，不可复用 |

## 4. 双芯片同存策略（多 env，不开分支）

`platformio.ini` 并列环境（**当前实际**用 pioarduino IDF5，见仓库根 `platformio.ini`）：

```ini
[platformio]
default_envs = esp32-c3

[idf5]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
framework = arduino
; …

[env:esp32-s3]
extends = idf5
board = esp32-s3-devkitc-1
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.f_cpu = 160000000L
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
```

（下文 §4 草案里的 `platform = espressif32` 为移植期草稿，**已过时**。）

`config.h` 侧（移植开始时落地）：

```cpp
#if CONFIG_IDF_TARGET_ESP32S3 || defined(ARDUINO_ESP32S3_DEV)
#define GNSS_RX_PIN 1
#define GNSS_TX_PIN 0
#define PPS_PIN 4
#define OLED_SDA_PIN 8
#define OLED_SCL_PIN 10
#define ENC_A_PIN 2
#define ENC_B_PIN 9      // C3=3, S3 避 strapping
#define ENC_SW_PIN 5
#define LED_D4_PIN 12
#define LED_D5_PIN 13
#define DBG_RX_PIN 44    // C3=20/21(USB 冲突), S3=UART0 43/44
#define DBG_TX_PIN 43
#else
/* 现行 C3 引脚 */
#endif
```

- 双核拆分：`xTaskCreatePinnedToCore(task-time → 1)`，`task-net`/`task-ui` → 0；`loop()` 不变。
- TWDT、LED-stale 重启、`app_ipc` 队列等机制与芯片无关，直接沿用。

## 5. 待办与风险（2026-09-18 全部关闭）

1. [x] `temperatureRead()` S3 路径 ✔（P2/P3：tsens=1、tempC 41.8→53.8°C 合理，长测复核）
2. [x] PPS ISR 双核延迟：未做显式测量，由 residual 全 0 + 10 min 比对（stdev 4.34 ms）+ 6.12h 长测间接覆盖（充分）
3. [x] USB/CDC/桥共存 ✔（烧录/监控走 UART 座 = GPIO43/44 板载 CP2102N 桥，OTG 口闲置）
4. [x] 16MB 分区表 ✔（default_16MB.csv，app0/app1 各 6.25MB + coredump）
5. [x] WS2812 状态显示：**已启用**（D4→R / D5→G / B=NTP 服务常亮；引脚实测 48，38 为原装 V1.1 走线；亮度 12）
6. [x] 首块 S3 板现场验收记录 ✔（[esp32s3_flash_test_20260917.md](esp32s3_flash_test_20260917.md)）

## 6. 移植实施计划（2026-09-17 定稿，四阶段）

### 触点清单（全量盘点，实测于代码）

| 触点 | 位置 | S3 处理 |
|---|---|---|
| 引脚宏 | `config.h:10-38` | 按目标宏分支（C3 值逐字节不变） |
| 任务绑核 | `main.cpp:686-688` 硬编码 core 0 | `TASK_TIME_CORE` 宏：C3=0 / S3=1，其余任务恒 0 |
| 温度采样 | `gps_service.cpp:67` legacy `temp_sensor_read_celsius()` | ⚠️ 该 API 不支持 S3（新 tsens 硬件）→ 封装 `board_temp_celsius()`：C3 走现路径（已验证），S3 走 `temperatureRead()` HAL |
| GNSS UART | `gps_service.cpp:42` `Serial1.begin(baud, 8N1, rx, tx)` | 宏驱动 ✔ 零改动 |
| OLED I2C | `display_ui.cpp:118` `Wire.begin(sda, scl)` | 宏驱动 ✔ 零改动 |
| TWDT/PPS ISR/esp_timer/WebServer/Preferences/app_ipc | — | 与芯片无关 ✔ 直接沿用 |

### P1 代码/环境 ✅ 已完成（2026-09-17）

| 改动 | 文件 | 内容 |
|---|---|---|
| env | `platformio.ini` | `[platformio] default_envs = esp32-c3` + `[env:esp32-s3]`（`${env:esp32-c3.build_flags}` / `lib_deps` 跨 env 继承实测可用） |
| 引脚宏 | `include/config.h` | `#if defined(ARDUINO_ESP32S3_DEV)` 分支 + `TASK_TIME_CORE`（S3=1/C3=0） |
| 绑核 | `src/main.cpp` | task-time → `TASK_TIME_CORE`，net/ui 恒 0 |
| 温度 | `src/gps_service.cpp` | `boardTempBegin()/boardTempRead()` 目标条件封装；legacy 头文件条件包含 |

P1 实施修正与实测记录：

- **调试串口零改动**：`Serial.begin()` 走各目标默认 UART0 引脚（C3=20/21，S3=43/44），无需任何 DBG 宏（草案中的 `DBG_RX/TX` 取消）
- **目标宏选 `ARDUINO_ESP32S3_DEV`**（PlatformIO 按 board 注入的编译器 `-D`，不依赖 sdkconfig.h 包含顺序），非 `CONFIG_IDF_TARGET_*`
- **S3 工具链**：registry 无 `linux_aarch64`（404 同 riscv），已按 riscv 同款流程手动安装——espressif/crosstool-NG `esp-2021r2-patch5` 的 `xtensa-esp32s3-elf-gcc8_4_0-linux-arm64.tar.gz`（61MB，gh-proxy 直连 release 资产 8s 下载）→ 解压平铺至 `~/.platformio/packages/toolchain-xtensa-esp32s3` + 手写 `package.json`/`.piopm`（pio 即认，不触发重装）；重 IO 一律后台守护跑（前台复合命令会拖死控制台，本日两次实证）
- **构建实测**：c3 回归守卫 SUCCESS 3m09s（增量）；`piod start -e esp32-s3` 全量 SUCCESS 4m58s；partitions.bin = default_16MB 布局（app0/app1 各 6.25MB @0x10000/0x650000 + coredump）
- 出口准则达成：**双环境全绿**，C3 路径零行为变化（条件分支对 C3 编译恒等），S3 固件 866KB 就绪待 P2 烧录

### P2 上板点亮（挪线 + 烧录）

挪线对照表（DevKitC-1 V1.1 排针位置已按引脚图核对）：

| 信号 | S3 GPIO | 板上位置 | 备注 |
|---|---|---|---|
| GNSS RX ← TXD | **1** | 右排针 | 非 strapping ✔ |
| GNSS TX → RXD | **0** | 右排针（BOOT 位） | 仅输出驱动模块高阻输入，复位期不影响 strapping；**如遇启动异常备用 GPIO18**（一行改动） |
| PPS | **4** | 左排针 | RTC 域 ✔ |
| OLED SDA/SCL | **8/10** | 左排针 | 沿用 |
| 编码器 A/B/SW | **2/9/5** | 2=右、9/5=左 | B 避 GPIO3 strapping ✔ |
| LED D4/D5 | **12/13** | 左排针 | 沿用 |
| 调试 RX/TX | **44/43** | 右排针（UART 座旁） | 烧录/监控走此口 |

- 烧录：`piod start -e esp32-s3 -t upload --upload-port <口>`（piod 参数透传）；或你侧 esptool `write_flash 0x10000`（app-only 保 NVS）
- 点亮顺序：串口（`MAC=` / SoftAP pass / `GPS UART` 行）→ OLED → 编码器 → WiFi 入网（用 `da9417f` 修复版行为验收：**复位不误入配网**）

### P3 现场验收（单套外设，串行策略）

- 外设仅一套（挪线制）：S3 独立测，对照**已冻结的 C3 基线**（配对差 −23.5 ms / 守时 2 min <0.5 ms / 长测零老化，见 docs）；精比用白天阿里云窗口（夜间拥堵不可作参照，见 clock_drift 文档）
- 测试项与复用工具：
  1. 锁星 + PPS → `ntpdate -q <ip>`：stratum 1 / `GPSS`
  2. 10 min NTP 比对：`tools/ntp_cmp_termux.py`
  3. 拔模块电源失效链：`tools/pps_failover_monitor.py`（复用 2026-09-16 流程）
  4. tempC 合理性（S3 tsens 量程/精度与 C3 不同；tcmp 关，无实害）
  5. PPS ISR→打戳延迟粗测（日志时间差；双核预期优于 C3）
- 如需 A/B：线挪回 C3 重烧即可（固件在仓库、配置在各自 NVS，互换 ~10 分钟）

### P4 转正决策

- 准入：P3 全绿 + 一次 ≥6.7h 级长测干净（`tools/clock_drift_monitor.py` 复用）
- 达标前 `default_envs` 保持 esp32-c3；C3 仍是产品主环境，S3 为双核增强路线

## 7. 风险表

| 风险 | 缓解 |
|---|---|
| S3 tsens 量程/精度与 C3 不同 | tcmp 默认关；tempC 仅展示/日志用途 |
| GPIO0 作 GNSS TX 的 strapping 顾虑 | 输出-only 高阻安全；备用 GPIO18 一行改动 |
| 首次 s3 构建工具链下载失败 | registry CDN 已验证可用（库直连成功）；失败重试 |
| 双 USB 座烧录口混淆 | 烧录/监控固定 = UART 座（43/44 板载桥）；OTG 口暂不用 |
| 双核下 PPS ISR 延迟/亲和性 | P3 专项粗测；预期双核更优 |
