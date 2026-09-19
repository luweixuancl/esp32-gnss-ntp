# RMT PPS 板测结案 — v1.1.35 FAIL → 搁置（2026-09-19）

> 证据：[`rmt_pps_retest_v1135_result_20260919.md`](rmt_pps_retest_v1135_result_20260919.md) · [`rmt_pps_retest_v1135_20260919.tar.gz`](rmt_pps_retest_v1135_20260919.tar.gz)

## 判定

**FAIL（结案搁置）**。授时主路径（GPIO ISR → LocalClock → Stratum-1）全程健康；RMT 精化不可用。

| 轮次 | 版本 | 结果 |
|---|---|---|
| 1 | v1.1.29 | armed OK，符号零（当时误判 DMA 指针） |
| 2 | v1.1.30–31 | WINDOW/err 修复；armed 恢复 |
| 3 | v1.1.32 | defer-arm OK；仍 100% junk |
| 4 | v1.1.33–34 | hex dump：`0x0` / `0x80000000`，时长全 0 |
| 5 | **v1.1.35** | **DMA=1 + 关 RGB TX + rtc_gpio_deinit 后仍全 `0x0`** |

## 已排除

- 位域错位（raw `.val` 已 dump）
- ISR 未拷贝 / 非 DMA 专用病（DMA ch=3 同样零）
- RGB RMT TX @10 MHz 同组干扰（已关）
- RTC 域占脚（已 `rtc_gpio_deinit`）
- armed / filter / WINDOW / realHz（均正常）

## 残留现象

done 回调仍约 **2 帧/PPS**，但词内容恒零 → **事件路径活、采样写入死**。

## 产品决策（v1.1.36，仍为现行策略）

- **`GPS_PPS_RMT_EN=0`**：生产默认 GPIO 精化（`main` tip v1.1.39 仍如此）
- RMT 源码与 `/debug/log` **保留**，供日后独立最小复现 / MCPWM 等替代方案
- 后续若再开 EN：优先 GPIO 回环自测或独立 sketch，勿再在整机上盲迭代

## 收摊验收（辅助 AI，2026-09-19）

Web OTA **v1.1.35→36 PASS**：`/status` 无 `gps.ppsRmt`；`/debug/log` 无 `[pps-rmt]`；S1 / LCK / 160 MHz modem sleep 全绿 — [ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md)。  
其后 tip 已至 **v1.1.39**（时钟环等），RMT 仍保持 EN=0。
