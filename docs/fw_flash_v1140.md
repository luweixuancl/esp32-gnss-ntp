# 交接：辅助 AI 下载 / 烧录 v1.1.40（S3）

> **状态：已完成 PASS**（2026-09-20）— [fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md)  
> 给执行侧（Termux / opencode / 现场辅助 AI）。勿再刷同一镜像。  
> 日期：2026-09-20 · 分支 `cursor/clock-psram-ring-a05e` · 镜像提交 `64ffa66`  
> 编译平台：pioarduino 55.03.311（Arduino 3.3.11 / IDF 5.5.5）  
> 总览：[CURRENT.md](CURRENT.md) · 镜像说明：[../dist/README.md](../dist/README.md)

## 0. 你要做什么

把 **v1.1.40** 刷到现网 **ESP32-S3**（已在 IDF5），确认 `fwMark=v1.1.40`。  
做完写一份结果报告（文末模板），**不要改代码、不要开 RMT、不要整片擦除。**

| 项 | 值 |
|---|---|
| 目标板 | ESP32-S3-DevKitC-1 N16R8（历史 IP `192.168.1.24`，**以现场为准**） |
| 口令 | 默认 `NTP-9EC4`（OLED / SoftAP / `/debug/log` 首行；NVS 改过则以设备为准） |
| 推荐路径 | **Web OTA**（IDF5→IDF5，保 NVS）— 先例 [ota_deploy_v1136_20260919.md](ota_deploy_v1136_20260919.md) |
| 备用路径 | USB `esptool` **只写 app** `@0x10000`（同样保 NVS） |
| 禁止 | C3 的 `firmware.bin` 刷 S3；`merged*_0x0.bin`（会清 WiFi）；`GPS_PPS_RMT_EN=1` |

**`main` 仍是 v1.1.39。** 不要从 `main/dist/` 下镜像。

## 1. 镜像（先核对再刷）

S3 app（OTA / `write_flash 0x10000`）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/clock-psram-ring-a05e/dist/firmware_esp32s3.bin
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `firmware_esp32s3.bin` | **1100704** | `aed6112f681be95e6959c4cbc0d8e88b45e979fae71df2d6e39532523e9670f0` |

```bash
curl -fL -o firmware_esp32s3.bin \
  'https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/clock-psram-ring-a05e/dist/firmware_esp32s3.bin'
wc -c firmware_esp32s3.bin          # 必须 = 1100704
echo 'aed6112f681be95e6959c4cbc0d8e88b45e979fae71df2d6e39532523e9670f0  firmware_esp32s3.bin' | sha256sum -c
```

任一不符 → **停，回报，不要刷**。

整片 `@0x0`（仅当设备还停在 ≤ v1.1.21 / IDF4 时才用，会清 NVS）：见 [upgrade_idf5_from_1120.md](upgrade_idf5_from_1120.md) 与 `dist/merged_firmware_esp32s3_n16r8_0x0.bin`（1166240 B，SHA256 见 `dist/SHA256SUMS`）。现网已是 IDF5 则**不要走这条**。

## 2. 推荐：Web OTA

先读现况，确认已是 IDF5（`fwMark` ≥ v1.1.28）：

```bash
IP=192.168.1.24          # 改成现场 IP
PASS='NTP-9EC4'
curl -fsS "http://$IP/status"
# 记下 fwMark / fwVersion / ota 槽位 / timeValid / ntp.stratum
```

登录 + 上传（字段名必须是 `firmware`）：

```bash
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -X POST -d "password=$PASS" "http://$IP/login" -o /dev/null -w 'login HTTP %{http_code}\n'

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -F "firmware=@firmware_esp32s3.bin" \
  --max-time 180 \
  "http://$IP/ota" -w '\nota HTTP %{http_code} time %{time_total}s\n'
```

期望：`/ota` **HTTP 200**，正文 `OK — rebooting into new firmware`。  
随后 HTTP 会断约 20–40 s。等到 `/status` 再通：

```bash
for i in $(seq 1 30); do
  curl -fsS --max-time 3 "http://$IP/status" && break
  sleep 2
done
curl -fsS "http://$IP/status"
```

`fwMark` / `fwVersion` 必须是 **`v1.1.40` / `1.1.40`**。  
OTA 后 GNSS 热启动，约 30–45 s 回 **LCK**，NTP `stratum=1` `refId=GPSS` `li=0`。  
NVS（WiFi / 口令 / 静态 IP）必须还在。

## 3. 备用：USB app-only

只在 OTA 失败或设备不在网上时用。插 **UART 座**（GPIO43/44 板载桥），**不要插 OTG 口**。

```bash
esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin
```

不要 `erase_flash`，不要写 `@0x0`。

## 4. 烧完冒烟（必做）

| # | 检查 | 期望 |
|---|---|---|
| 1 | `/status` `fwMark` | `v1.1.40` |
| 2 | `/status` `clock.state` | 很快回到 `LCK`（热启动） |
| 3 | NTP | `stratum=1` `refId=GPSS` `li=0` `timeValid=true` |
| 4 | `/debug/log?pass=` 首行 | `fw=v1.1.40`；**无** `[pps-rmt]` 活跃行（EN=0） |
| 5 | `/status` 无 `gps.ppsRmt` | 与 v1.1.36 收摊一致 |

免串口 log：

```bash
curl -fsS "http://$IP/debug/log?pass=$PASS" -o boot_log.txt
head -20 boot_log.txt
```

## 5. 可选：v1.1.40 一键全量下载（新功能）

本版相对 v1.1.39：**无参 `GET /debug/clock/data` = 一次返回全量**（不再只给第 1 页）。  
若时间够，短录几分钟验收即可，**不要做 12 h、不要录完直接断电**。

```bash
# 人类或你：start → 等 ≥60 s → stop（ct_fetch_bin.py 是只读的，不会帮你 start/stop）
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" start
sleep 90
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" stop

# 只读拉全量（工具会先 GET /debug/clock，确认 STOP 且 count>0）
python3 tools/ct_fetch_bin.py --host "$IP" --pass "$PASS" --save-bin dump.bin -o dump.csv

# 或浏览器：/cfg → 停止 →「下载 BIN」（无 from/limit；>1 MB 有确认框）
```

PASS：`dump.bin` 以 `CTRB` 开头；`flags` 含 DONE；`count` 与 stop 时 `/debug/clock` 一致；文件大小 ≈ `32 + count×42`。  
然后 `POST /debug/clock/clear` 复位。顺序：**stop → 拉全 → 再 clear/断电**。

API：[clock_trace.md](clock_trace.md)。旧事故（只下第 1 页丢 5/6）：[clock_trace_analysis_20260920.md](clock_trace_analysis_20260920.md)。

## 6. 不要做

- 不要从 `main` 下固件（那是 v1.1.39，S3 app 1099856 B）
- 不要把 C3 `firmware.bin`（1209632 B）刷到 S3
- 不要 Web OTA 上传 `merged*_0x0.bin`
- 不要 `erase_flash` / 整片 `@0x0`（除非确认还是 IDF4）
- 不要改 `GPS_PPS_RMT_EN`、不要再开 RMT 整机盲测
- 不要在 REC 态拉 clock-trace；不要拉完不确认就断电
- 不要改仓库源码；结果写成报告即可

## 7. 回报模板（原样填回）

```text
RESULT: PASS|FAIL
path: OTA | esptool
ip:
fwMark_before:
fwMark_after:
sha256_ok: yes|no  size:
ota_http:     (200 / 秒)
back_online_s:
lck_s:
ntp: stratum / refId / li / timeValid
nvs_kept: yes|no
one_shot_dump: skip | PASS (count=  bytes=  flags=) | FAIL
notes:
```

结果文档建议落在 `docs/fw_flash_v1140_result_20260920.md`（或当日日期）。
