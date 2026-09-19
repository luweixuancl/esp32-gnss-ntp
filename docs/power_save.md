# S3 / 常开 NTP 的功耗与散热

> 固件：**v1.1.28**（自 v1.1.26 起落地）  
> 场景：必须常连 WiFi、听 UDP/123、跟 PPS —— **不能**深睡。  
> 总览：[CURRENT.md](CURRENT.md)

## 已落地（代码默认）

| 手段 | 默认 | 作用 | 代价 |
|---|---|---|---|
| `CPU_FREQ_MHZ=160` | 开（S3；板级曾 240） | 空闲发热明显下降 | 峰值算力下降（本固件足够） |
| `WIFI_MODEM_SLEEP=1` → `WIFI_PS_MIN_MODEM` | 开 | STA 空闲电流下降 | NTP RTT 可能多若干 ms 抖动 |
| `TASK_TIME_IDLE_MS=5` | 开（原 1 ms） | time 任务少空转 | LAN NTP 仍够用 |
| RGB 亮度 8、同色跳过 `rgbLedWrite` | 开 | 少 SK6812 / RMT 电流 | 指示略暗 |
| OLED 闲置熄屏 | 已有 | 面板功耗 | — |

串口：`[pwr] cpu=160 MHz wifi_modem_sleep=1 time_idle=5ms`。  
S3 `platformio.ini`：`board_build.f_cpu = 160000000L`。

## 编译期覆盖

```ini
build_flags =
  -DCPU_FREQ_MHZ=240
  -DWIFI_MODEM_SLEEP=0
  -DTASK_TIME_IDLE_MS=1
```

精密 NTP 比对建议临时 `-DWIFI_MODEM_SLEEP=0`（见 [s3_smoke_ntp_soak_20260919.md](s3_smoke_ntp_soak_20260919.md) 备注）。

## 不做

- Light/Deep sleep、关 WiFi、激进 `WIFI_PS_MAX_MODEM`。

## 验收（已完成）

S3 v1.1.28：用户确认壳温下降；13 min soak 中 Die 温约 37–40 °C。见 [board_test_s3_idf5_20260919.md](board_test_s3_idf5_20260919.md)。
