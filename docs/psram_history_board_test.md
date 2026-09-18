# PSRAM `/history` 板测执行单（v1.1.12+）

> 目的：验收 S3 自录环 + C3 关闭路径 + 状态页不停秒。只读 HTTP，不改设备配置。

## 0. 刷机

- 目标 OLED：**`v1.1.12`**（或更新）
- S3 app：`dist/firmware_esp32s3.bin` @ `0x10000`（勿全片擦除）
- 镜像示例：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/web-ota-a05e/dist/firmware_esp32s3.bin
```

串口应见：`[history] enabled capacity=86400 bytes=2073600 (SPIRAM, mutex)`

## 1. 冒烟（上电 ≥ 2 min）

```bash
# 摘要
curl -s http://<IP>/history | python3 -m json.tool

# 最近 60 s CSV
curl -s 'http://<IP>/history.csv?last=60' | head

# 或
python3 tools/history_pull.py --host <IP> --last 60
```

| 检查 | 期望 |
|------|------|
| `enabled` | `true`（S3） |
| `capacity` | `86400` |
| `psramBytes` | `2073600` |
| `count` | 随时间增加（约 1/s） |
| CSV 表头 | 含 `utcEpoch,state,...,gap` |
| C3 `/history` | `enabled:false`，`reason` 含 `no PSRAM` |

## 1b. 状态页不停秒（必测）

打开 `http://<IP>/`，盯 **UTC / 本地** 秒位 ≥ 60 s：

| 检查 | 期望 |
|------|------|
| 秒位 | 连续递增，无卡住 1–2 s 再跳秒 |
| `fwMark`（页底） | `v1.1.12` |
| 同窗拉 CSV | `curl -s 'http://<IP>/history.csv?last=3600' -o /dev/null` 时秒位仍平滑 |

云侧无法访问家庭局域网 `192.168.1.x`，此项需板侧目视或本机浏览器验收。

## 2. OTA 停采

再做一次 Web OTA（可刷同版本）。升级中或刚结束后看：

```bash
curl -s http://<IP>/history | python3 -c "import sys,json;d=json.load(sys.stdin);print('otaSkipped',d.get('otaSkipped'))"
```

`otaSkipped` 应在上传窗口增加。

## 3. 短窗对照（可选，≥10 min）

终端 A：`tools/clock_drift_monitor.py --host <IP> --until ...`  
终端 B：结束后 `tools/history_pull.py --host <IP> --last 600 --csv hist.csv`  

同窗 `freqPpm` 均值与 `state` 以 LCK 为主即可，不要求逐秒 bit 相同。

## 4. 通过标准

对应设计文档 §9：A–F。全部勾选后把路线图 ③ 标为「板测通过」。
