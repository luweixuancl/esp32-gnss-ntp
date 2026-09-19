# RMT dump 分析 — v1.1.34（2026-09-19）

> 证据：[`rmt_pps_v1134_debuglog_20260919.tar.gz`](rmt_pps_v1134_debuglog_20260919.tar.gz)（`GET /debug/log`）

## 结论

**符号时长仍全 0**；已排除「位域错位 / ISR 未拷贝」——raw `.val` 只有两种：

```text
val0=0x00000000  d0=0 l0=0 d1=0 l1=0
val0=0x80000000  d0=0 l0=0 d1=0 l1=1
```

两种词交替（约 2 帧/PPS），**没有任何非零 duration**。  
armed / realHz=1e6 / mem=48 / filter=0 / LCK·NTP 均正常 → **RMT FSM 在跑，但 RMTMEM 未写入真实脉宽**。

## 仍健康

- `ready → armed after first PPS (count=1)`
- `/debug/log` 免串口路径可用
- GPIO PPS + LocalClock + stratum 1 无回归

## 下一步（v1.1.35）

优先试：

1. **PPS RMT 开启时关掉 RGB `rgbLedWrite`（RMT TX @10 MHz）**，排除同组 RMT 干扰  
2. **`rtc_gpio_deinit(PPS)`** 后再挂 RMT（GPIO4 属 RTC 域）  
3. **DMA RX + ISR 逐词拷贝**（换数据通路）  
4. 若仍零：考虑仅在 GPIO 上升沿后短窗 `rmt_receive`，或暂退回 GPIO 精化并关 EN
