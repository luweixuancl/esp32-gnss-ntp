# 从 IDF4（≤ v1.1.21）升到 IDF5（v1.1.28+ / 当前 v1.1.39）

> 适用：设备仍显示 **v1.1.20 / v1.1.21**（Arduino 2 / IDF 4.4）  
> 目标：`main` 上 **v1.1.39**（pioarduino / IDF 5.5.5）  
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

3. 重启后 `fwMark` 应为 **`v1.1.39`**（或当前 `main` 尖端版本）。NVS（WiFi 等）被擦掉，需重配网。

### ESP32-C3

整片：`firmware_merged_0x0.bin` @ `0x0`（同上 erase + write）。镜像见 `dist/` / [CURRENT.md](CURRENT.md)。

## 已在 IDF5（≥ v1.1.28）时

只需 app-only 或 Web OTA：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/main/dist/firmware_esp32s3.bin
```

`@0x10000`，保留 NVS。

## 串口确认

```text
FW v1.1.39 (1.1.39)
```

OLED / `/status` 的 `fwMark` 同值。
