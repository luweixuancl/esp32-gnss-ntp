# 从 v1.1.20（IDF4）升到本分支（IDF5）

## 现象

Web OTA / 只刷 `0x10000` app 后，设备仍显示 **v1.1.20**（或短暂新版本后回滚）。

## 原因

| 路径 | 为何会停在 1.1.20 |
|------|-------------------|
| **Web OTA** | 新 app（Arduino 3 / IDF 5.5）常与旧 bootloader 不兼容，启动失败 → otadata **自动回滚**到上一槽 |
| **只写 `0x10000`** | 绝对地址是 **app0**；若当前从 **app1** 启动，写入后仍启动旧槽 |
| **下错芯片包** | C3↔S3 镜像会被 OTA 头校验拒绝（应有明确错误，不会“静默成功”） |

本分支改了框架大版本，**首次迁移必须整片烧录**（bootloader + 分区表 + otadata + app）。

## 正确步骤（推荐）

### ESP32-S3（DevKitC-1 N16R8）

1. 下载整片镜像（核对大小 **1154928** B，见分支 `dist/SHA256SUMS`）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/idf5-adapt-a05e/dist/merged_firmware_esp32s3_n16r8_0x0.bin
```

2. USB 串口，**开启擦除 Flash**，偏移 **`0x0`** 写入上述文件（不要只写 `0x10000`）。

```bash
esptool.py --chip esp32s3 -p <PORT> -b 921600 erase_flash
esptool.py --chip esp32s3 -p <PORT> -b 921600 write_flash 0x0 merged_firmware_esp32s3_n16r8_0x0.bin
```

3. 重启后串口 / OLED / `/status` 的 `fwMark` 应为 **`v1.1.28`**（或当前分支标记）。  
   NVS（WiFi 等）会被擦掉，需重新配网。

### ESP32-C3

同上，改用：

```text
…/dist/firmware_merged_0x0.bin
```

（大小约 **1262720** B；C3 还换了更大 OTA 分区表，同样必须整片。）

## 迁移成功之后

同 IDF5 线上的小版本升级，可用 Web OTA / `firmware_esp32s3.bin` @ `0x10000`（保 NVS）。  
上传前核对：`otaChip` 与文件名一致、字节数与 `SHA256SUMS` 一致。

## 串口自检（升级后 30 s 内）

```text
FW v1.1.28 (1.1.28)
[ota] running=ota_0 state=…   # 或 ota_1；不应再回 1.1.20
[ota] app valid (cancel rollback) …
```
