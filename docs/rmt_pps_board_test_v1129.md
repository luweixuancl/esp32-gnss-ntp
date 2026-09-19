# RMT PPS 板测流程（v1.1.29 · `GPS_PPS_RMT_EN=1`）

> 受众：执行板测的 AI / 操作者（只测不改代码，除非文档要求回退）  
> 分支：`cursor/work-a05e` · 固件 **v1.1.29**  
> 目标板：**ESP32-S3**（主测）；C3 可选  
> 设备参考 IP：`192.168.1.24`（以现场为准）  
> 监控脚本：[`tools/rmt_pps_monitor.py`](../tools/rmt_pps_monitor.py)

## 0. 测什么

验证 IDF5 `driver/rmt_rx.h` 路径能否：

1. 启动时 **armed**（串口 `[pps-rmt] idf5 armed ...`）  
2. `/status` 出现 `gps.ppsRmt`，且 **`idfDataFrames` 随秒增长**（有真实符号，不是空帧）  
3. 时钟仍能 **LCK / S1**，不因 RMT 抖动  
4. `fallbacks` 不明显爬升（RMT stale → GPIO 回退次数）

## 1. 固件与烧录

| 文件 | 用途 |
|---|---|
| `dist/firmware_esp32s3.bin` | **推荐**（设备已在 IDF5 v1.1.28）：`@0x10000`，保 NVS |
| `dist/merged_firmware_esp32s3_n16r8_0x0.bin` | 整片 `@0x0`（会清 WiFi） |

核对（仓库内）：

```bash
wc -c dist/firmware_esp32s3.bin          # 期望 1094432
sha256sum dist/firmware_esp32s3.bin      # 见 dist/SHA256SUMS
```

中国镜像（分支 `cursor/work-a05e`）：

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/dist/firmware_esp32s3.bin
```

烧录示例（已在 IDF5，保配置）：

```bash
esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 firmware_esp32s3.bin
```

刷完确认 OLED / 串口 / `/status` 的 **`fwMark` = `v1.1.29`**。

## 2. 前置条件

- GNSS 天线正常，1PPS → **GPIO4**，UART GNSS 正常  
- STA 已入网，记下设备 IP  
- 串口 115200 可读（可选但强烈建议看 boot 行）

## 3. 串口启动检查（必过）

期望出现（约在 GPS UART 行附近）：

```text
FW v1.1.29 (1.1.29)
[pps-rmt] idf5 armed pin=4 tick=1000ns win=20ms filter=1000ns dma=1
```

**FAIL 若出现：**

```text
[pps-rmt] idf5 init failed stage=… err=… -> GPIO ISR only
```

记录完整 `stage` / `err`，停止并汇报（RMT 通道申请失败）。

运行中偶发：

```text
[pps-rmt] stale -> GPIO fallback
```

短时偶发可接受；若频繁刷屏且 `/status` 里 `fallbacks` 快速增加 → FAIL。

## 4. HTTP 字段（`GET /status`）

`gps.ppsRmt` **必须存在**（EN=0 时无此对象）。关键字段：

| 字段 | PASS 期望 |
|---|---|
| `armed` / `idfOk` | `true` |
| `idfDataFrames` | 随时间单调增（约 1/s 量级） |
| `idfEmptyFrames` | 远小于 `idfDataFrames`（空帧占比 < 50%） |
| `active` | 有 PPS 后应为 `true` |
| `deltaMeanUs` | GPIO−RMT 差；通常较小（几十 µs 量级，允许波动） |
| `fallbacks` | 不明显爬升 |
| `idfStage` | 正常武装后为非 0（init 阶段位图） |
| `clock.state` | `LCK` |
| `gps.ppsFresh` / `ppsCount` | 新鲜且递增 |

快速手测：

```bash
curl -s http://<IP>/status | python3 -m json.tool | head -80
# 或
curl -s http://<IP>/status | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('fwMark'),d['clock']['state'],d['gps'].get('ppsRmt'))"
```

## 5. 自动监控（主验收）

在能访问设备局域网的机器上（Termux/PC，仅标准库）：

```bash
# 先拉脚本（或用仓库内 tools/）
curl -L -o rmt_pps_monitor.py \
  https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/work-a05e/tools/rmt_pps_monitor.py

# 冒烟 60s
python3 rmt_pps_monitor.py --host <IP> --quick

# 正式 180s（推荐）
python3 rmt_pps_monitor.py --host <IP> --duration 180 --jsonl rmt_pps.jsonl
```

脚本结束打印 `VERDICT: PASS` 或 `FAIL` 及原因。把 **完整控制台输出** 与（若有）`rmt_pps.jsonl` 交回。

## 6. 可选：NTP 抽检

```bash
ntpdate -q <IP>
# 期望 stratum 1, refid GPSS
```

或沿用既有 `tools/ntp_cmp_termux.py --gps <IP> --quick`。

## 7. PASS / FAIL 汇总

| # | 项 | PASS |
|---|---|---|
| A | `fwMark=v1.1.29` | 必须 |
| B | 串口 `idf5 armed`（非 init failed） | 必须 |
| C | `gps.ppsRmt` 存在且 `armed`/`idfOk` | 必须 |
| D | `idfDataFrames` 明显增长且空帧不多 | 必须 |
| E | 进入并保持 `LCK`（监控窗 ≥90%） | 必须 |
| F | `fallbacks` 不暴涨 | 必须 |
| G | NTP stratum 1（可选） | 建议 |

任一必须项失败 → **FAIL**：建议把 `include/config.h` 中 `GPS_PPS_RMT_EN` 改回 `0` 并重发生产固件；把串口 + monitor 日志归档。

## 8. 回报模板（给主 AI）

```text
RESULT: PASS|FAIL
fwMark:
serial_boot: (armed line or init failed line)
monitor_verdict: (paste VERDICT block)
idfDataFrames: start -> end
idfEmptyFrames:
fallbacks: start -> end
LCK ratio:
notes:
```

## 9. 不要做的事

- 不要改 `GPS_PPS_RMT_*` 宏除非主 AI 要求  
- 不要整片擦除（除非设备不在 IDF5）  
- 不要用 C3 的 `firmware.bin` 刷 S3  
- 不要在 `pps=0` 时继续测 RMT（先修 PPS 线）
