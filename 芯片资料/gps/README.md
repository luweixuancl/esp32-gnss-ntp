# GNSS 模块资料（实际使用：DX-GP22）

厂商：深圳大夏龙雀。协议族与 DX-GP10 相同（CASIC `$PCAS*`）。

- 默认串口：9600 8N1
- NMEA 输出滤波：`$PCAS03,nGGA,nGLL,nGSA,nGSV,nRMC,nVTG,nZDA,...`
- 本固件启动后探测语句，再配置为 **仅 GGA + ZDA**（坐标/星数/HDOP + 时间日期）

参考文档见同目录 PDF（DX-GP10 串口应用指导；GP22 指令兼容）。
