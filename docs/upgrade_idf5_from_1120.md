# 从 IDF4（≤ v1.1.21）升到 IDF5（v1.1.28+）

> 适用：设备仍显示 **v1.1.20 / v1.1.21**（Arduino 2 / IDF 4.4）  
> 目标：`main` 上 **v1.1.28**（pioarduino / IDF 5.5.5）  
> 总览：[CURRENT.md](CURRENT.md)

## 现象

Web OTA / 只刷 `0x10000` app 后，设备仍显示旧版本（或短暂新版本后回滚）。

## 原因

| 路径 | 为何停在旧版 |
|------|----------------|
| **Web OTA** | 新 app 常与旧 bootloader 不兼容 → 启动失败 → otadata **自动回滚** |
| **只写 `0x10000`** | 绝对地址是 **app0**；若当前从 **app1** 启动，写入后仍启动旧槽 |
| **下错芯片包** | C3↔S3 会被 OTA 头校验拒绝 |

本仓库改了框架大版本，**首次迁移必须整片烧录**（bootloader + 分区表 + otadata + app）。

## 正确步骤（推荐）

### ESP32-S3（DevKitC-1 N16R8）

1. 下载整片镜像（核对大小与 `dist/SHA256SUMS`）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/merged_firmware_esp32s3_n16r8_0x0.bin
```

2. USB 串口，**开启擦除 Flash**，偏移 **`0x0`** 写入：

```bash
esptool.py --chip esp32s3 -p <PORT> erase_flash
esptool.py --chip esp32s3 -p <PORT> write_flash 0x0 merged_firmware_esp32s3_n16r8_0x0.bin
```

3. 重启后 `fwMark` 应为 **`v1.1.28`**。NVS（WiFi 等）被擦掉，需重配网。

### ESP32-C3

同上，改用 `dist/firmware_merged_0x0.bin`（C3 另换了更大 OTA 分区表，同样必须整片）。

## 迁移成功之后

同 IDF5 线上的小版本升级，可用 Web OTA / `firmware_esp32s3.bin`（或 C3 的 `firmware.bin`）@ `0x10000`（保 NVS）。  
上传前核对：`otaChip` 与文件名一致、字节数与 `SHA256SUMS` 一致。

## 串口自检

```text
FW v1.1.28 (1.1.28)
[pwr] cpu=160 MHz wifi_modem_sleep=1 …
[gps] NMEA filter: … GGA+RMC+ZDA …
[ota] app valid (cancel rollback) …   # 若来自 OTA 槽
```
