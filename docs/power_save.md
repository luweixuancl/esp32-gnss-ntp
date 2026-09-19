# S3 / 常开 NTP 的功耗与散热（v1.1.26）

> 场景：GNSS NTP 时间服务器必须常连 WiFi、听 UDP/123、跟 PPS —— **不能**深睡 / 定时唤醒。  
> 目标：在不牺牲授时可用性的前提下降低 S3 空闲发热。

## 已落地的代码侧手段

| 手段 | 默认 | 作用 | 代价 |
|---|---|---|---|
| CPU 主频 `CPU_FREQ_MHZ=160` | 开（S3 原板级默认 240） | 空闲发热明显下降；PPS 用 `esp_timer`/GPIO ISR，不依赖 CPU 周期计数 | 峰值算力下降（对本固件足够） |
| WiFi modem sleep `WIFI_MODEM_SLEEP=1` → `WIFI_PS_MIN_MODEM` | 开（此前 `setSleep(false)`） | STA 空闲电流下降（DTIM 唤醒） | NTP RTT 可能多若干 ms 抖动 |
| `TASK_TIME_IDLE_MS=5` | 开（原 1 ms） | time 任务少空转 | LAN NTP 仍够用 |
| RGB 亮度 8、同色跳过 `rgbLedWrite` | 开 | 少 SK6812 / RMT 电流与打扰 | 指示略暗 |
| OLED 闲置熄屏 | 已有（默认 10 min） | 面板功耗 | — |

串口启动可见：`[pwr] cpu=160 MHz wifi_modem_sleep=1 time_idle=5ms`。

## 编译期覆盖

```ini
build_flags =
  ...
  -DCPU_FREQ_MHZ=240          ; 退回满速
  -DWIFI_MODEM_SLEEP=0        ; 射频常醒（最低 NTP 抖动）
  -DTASK_TIME_IDLE_MS=1
```

S3 `platformio.ini` 另设 `board_build.f_cpu = 160000000L`，与运行时 `setCpuFrequencyMhz` 一致。

## 不建议 / 无效的方向

- **Light/Deep sleep**：会丢 PPS 节拍与 NTP 在线服务。  
- **关 WiFi**：产品不可用。  
- **只靠 `lib_ignore`**：几乎不降运行功耗（那是 Flash 体积问题）。  
- **激进 `WIFI_PS_MAX_MODEM`**：省电更多，但易丢 beacon / NTP 尾延迟变差，本固件不用。

## 硬件侧（代码外，往往更有效）

- 散热片 / 通风（你已在做）。  
- 5 V 供电与 LDO 热耗：USB 口供电时板载稳压也发热，尽量用质量好的供电。  
- GNSS 模块、OLED 各自有静态电流；OLED 闲置熄屏已覆盖面板。

## 验收建议

1. 刷 v1.1.26 后摸壳温 / 看串口 `[pwr]` 行。  
2. 对同一 NTP 客户端比 RTT（modem sleep 开/关）。若抖动不可接受，编译加 `-DWIFI_MODEM_SLEEP=0`。  
3. 确认 Locked 长测与 PPS 计数正常。
