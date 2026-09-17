# ESP32-S3-DevKitC-1 硬件笔记与移植规划（N16R8）

> 状态：硬件笔记 / 移植未开始（roadmap 项）
> 日期：2026-09-17
> 板卡：乐鑫 **ESP32-S3-DevKitC-1 V1.1**，模组 **ESP32-S3-WROOM-1 N16R8**（16MB QIO flash + 8MB OPI PSRAM）
> 资料：[`芯片资料/ESP32S3/`](../芯片资料/ESP32S3/)（引脚图 / 原理图 / 数据手册；23MB 开发板全文档仅本地保留）
> 相关：`AGENTS.md`（C3 架构与约定）、[wifi_event_fsm.md](wifi_event_fsm.md)

## 1. 为什么值得移植

- **双核 LX7 @240MHz**：`task-time`（GNSS/NTP/PPS）可独占 core 1，`task-net`/`task-ui` 在 core 0——NTP 打戳与 PPS ISR 延迟不再受 WiFi/网页抖动影响（C3 单核只能靠优先级近似）。
- 512KB SRAM、指令/数据 cache 更大；USB-OTG 原生可用。
- 现有代码**引脚全部走 `config.h` 宏、无硬编码**，任务结构按可拆分设计——移植条件已备齐。

## 2. 板卡关键事实

| 项 | 事实 |
|---|---|
| 调试串口 | UART0 = **GPIO43(TXD0)/44(RXD0)**，板载 USB-UART 桥（板上 UART 座） |
| 原生 USB | **GPIO19(D-)/20(D+)**，USB-OTG + JTAG |
| RGB LED | WS2812 @ **GPIO38**（V1.1 实测丝印 `RGB@IO38`；老版资料写 48，以原理图为准） |
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
| 状态 LED D4/D5 | 12 / 13 | **12 / 13 沿用** | 或改用板载 WS2812@38（需 LEDC/RMT 驱动，暂不采用） |
| 调试串口 RX/TX | 20 / 21 | **44 / 43** | ⚠️ C3 的 20/21 在 S3 是 USB D-/D+，不可复用 |

## 4. 双芯片同存策略（多 env，不开分支）

`platformio.ini` 并列环境，**源码 100% 共享**；引脚差异在 `config.h` 用目标宏分支：

```ini
[env:esp32-c3]            ; 现有环境，default_envs 保持不变
; ...

[env:esp32-s3]            ; S3 移植期按需: pio run -e esp32-s3
platform = espressif32
board = esp32-s3-devkitc-1
monitor_speed = 115200
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi   ; N16R8: QIO flash + OPI PSRAM
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv   ; default.csv 按 4MB 划，必须换
board_build.flash_mode = dio                ; (若启动异常再回 dio/qio 二选一实测)
build_flags =
  ${env:esp32-c3.build_flags}
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=0              ; 与 C3 行为一致：Serial=UART0
```

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

## 5. 待办与风险（移植执行时逐项关闭）

1. [ ] `temperatureRead()` 在 S3 Arduino 核上的可用性/精度验证（`tcmp` 温补路径依赖）
2. [ ] PPS ISR 在双核下的延迟实测（预期更好；确认 `esp_timer` 与 ISR 亲和性）
3. [ ] `ARDUINO_USB_MODE=1`（Hardware CDC）与板载桥共存行为实测；upload 口确认（UART 座 vs USB 口）
4. [ ] 16MB 分区表落地（app 分区可放大或维持 1.5MB——OTA/双分区暂无需求，维持 default_16MB.csv）
5. [ ] WS2812@38 是否纳入状态显示（当前结论：不用，保持与 C3 相同的双 LED 语义）
6. [ ] 首块 S3 板建立 `docs/` 现场验收记录（烧录/锁星/NTP 比对三件套）
