# GNSS 模块资料

实际使用：**DX-GP10**（深圳大夏龙雀，CASIC `$PCAS*` 协议）。

| 文件 | 说明 |
|------|------|
| `DX-GP10串口配置指南.pdf` | `$PCAS01`～`$PCAS10`（波特率、更新率、NMEA 滤波等） |
| `DX-GP10 GPS模块应用手册.pdf` | 硬件引脚、电源、UART、1PPS、天线 |

固件启动后探测 NMEA：已是 **GGA + RMC + ZDA** 则跳过 `$PCAS*`；否则 `$PCAS03` 精简为该三者，仅在句型不对时 `$PCAS00` 写 Flash（见 `src/gps_service.cpp`）。当前基线见 [`docs/CURRENT.md`](../docs/CURRENT.md)。
