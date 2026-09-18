# GNSS 模块资料

实际使用：**DX-GP22**（评估板/模组）。厂商手册型号多为 **DX-GP10**，同属深圳大夏龙雀 CASIC 协议族，`$PCAS*` 指令通用。

| 文件 | 说明 |
|------|------|
| `DX-GP10串口配置指南.pdf` | `$PCAS01`～`$PCAS10`（波特率、更新率、NMEA 滤波等） |
| `DX-GP10 GPS模块应用手册.pdf` | 硬件引脚、电源、UART、1PPS、天线 |

固件启动后探测 NMEA，再 `$PCAS03` 仅保留 **GGA + ZDA**，`$PCAS00` 写入 Flash（见 `src/gps_service.cpp`）。
