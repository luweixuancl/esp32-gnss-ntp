# 交接：辅助 AI OTA v1.1.42 + Hold 5m 失效链复测（S3）

> **状态：待执行**  
> 给本地执行侧（Termux / Alpine / opencode）。  
> 日期：2026-09-21 · 分支 `cursor/pps-resume-deadlock-6c51`  
> 计划：[gps_failover_fix_plan_v1142.md](gps_failover_fix_plan_v1142.md)  
> 上一轮：**v1.1.41 第 6 项恢复 FAIL** — [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)

## 0. 你要做什么

1. **Web OTA** 把现网 S3 从 **v1.1.41 → v1.1.42**（保 NVS）。  
2. 用 `tools/pps_failover_monitor.py` 重跑 **拔 GPS 模块电源 / Hold 5m** 全链。  
3. **第 6 项必须回到 LCK**（这是本版唯一要修的现场 bug）。  
4. 时间够再跑 Refuse 对照。  
5. 写结果到 `docs/fw_flash_v1142_result_20260921.md`（文末模板），`git commit` + `push` 本分支。

**不要改代码、不要开 RMT、不要整片擦除、不要从 `main` 下镜像、不要依赖 `ntpdate`。**

| 项 | 值 |
|---|---|
| 目标板 | ESP32-S3 · 上场 IP **`10.121.95.14`**（DHCP，以 `/status` 为准） |
| 口令 | `NTP-9EC4`（NVS 保留则仍是它） |
| 路径 | **只走 Web OTA**；USB 仅当 OTA 失败 |
| 策略 | 主测 **Hold 5m**（`anomalyPolicy=2` / `holdoverSec=300`） |
| 注入 | **拔 GPS 模块 VCC / 电源**（勿拔 PPS 杜邦线当主测） |
| 监测 | `tools/pps_failover_monitor.py`（纯 Python3 标准库） |
| 禁止 | C3 `firmware.bin`；`merged*_0x0.bin`；`erase_flash`；`GPS_PPS_RMT_EN=1` |

## 1. 镜像（先核对再刷）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/pps-resume-deadlock-6c51/dist/firmware_esp32s3.bin
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `firmware_esp32s3.bin` | **1103104** | `586e8ba68bb148621cd418a88b77c24fd97cc1d5bf5cfc30163bfd668ed9e2cf` |

```bash
curl -fL -o firmware_esp32s3.bin \
  'https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/pps-resume-deadlock-6c51/dist/firmware_esp32s3.bin'
wc -c firmware_esp32s3.bin   # 必须 = 1103104
echo '586e8ba68bb148621cd418a88b77c24fd97cc1d5bf5cfc30163bfd668ed9e2cf  firmware_esp32s3.bin' | sha256sum -c
```

不符 → **停，不要刷**。

## 2. Web OTA

```bash
IP=10.121.95.14
PASS='NTP-9EC4'

curl -fsS "http://$IP/status"   # 记下 fwMark，期望 v1.1.41

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -X POST -d "password=$PASS" "http://$IP/login" -o /dev/null -w 'login HTTP %{http_code}\n'

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -F "firmware=@firmware_esp32s3.bin" \
  --max-time 180 \
  "http://$IP/ota" -w '\nota HTTP %{http_code} time %{time_total}s\n'
```

期望：`/ota` **200** + `OK — rebooting into new firmware`。断约 10–40 s 后：

```bash
for i in $(seq 1 30); do
  curl -fsS --max-time 3 "http://$IP/status" && break
  sleep 2
done
```

`fwMark` / `fwVersion` 必须是 **`v1.1.42` / `1.1.42`**。OTA 本身是一次重启：GPS 已插上时，**应自行回到 LCK**（这是对「重启仍 ACQ」那条现场记录的对照）。若 OTA 后 &gt;60 s 仍 ACQ 且 pps 在涨，立刻拉 `/debug/log?pass=`，**不要再重启**，整轮记 FAIL。

很快回 LCK 后：NTP `stratum=1` `GPSS` `li=0`，WiFi/口令/IP 仍在。

## 3. Hold 5m 失效链（本版重点）

策略必须是 Hold 5m：

```bash
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -H 'Content-Type: application/json' \
  -d '{"anomalyPolicy":2}' "http://$IP/save"
```

```bash
mkdir -p /tmp/gps_failover && cd /tmp/gps_failover

python3 pps_failover_monitor.py \
  --host "$IP" \
  --mode failover \
  --pull-delay 60 \
  --recovery 240 \
  --csv failover_hold5m_v1142.csv \
  2>&1 | tee failover_hold5m_v1142.log
```

脚本若仓库里已有则：`python3 tools/pps_failover_monitor.py ...`。  
**没有 `ntpdate` 没关系。**

节奏：脚本打出 LCK 后 **60 s 内拔模块电源** → 等到打印 `>>> replug now` → **装回电源** → 等重捕星。

| # | 项 | 期望 |
|---|---|---|
| 1 | 进 HLD | 断电后数秒内 `LCK→HLD` |
| 2 | HLD NTP | `li=0 st=1 GPSS`；disp 缓升 |
| 3 | 守时 | 检查点 offset 相对基线漂移应小（史档 2 min &lt;0.5 ms；上轮 &lt;2～4 ms） |
| 4 | 超时 UNS | `holdoverMs≈300000` 转 `UNS` |
| 5 | UNS 拒绝 | `li=3 st=16` |
| 6 | **恢复** | 装回后回到 **LCK**；NTP 再为 `GPSS`；offset 回基线带。`anchor=0 stable=0` 不得持续 |

第 6 项 FAIL → 整轮 FAIL。立刻：

```bash
curl -fsS "http://$IP/status" | tee status_stuck.json
curl -fsS "http://$IP/debug/log?pass=$PASS" | tee debug_stuck.log
```

不要先重启。log/CSV 仍须提交。

## 4. 可选：Refuse 对照

时间够：`anomalyPolicy=0`，同样拔模块电源，期望 **`LCK→UNS`（无 HLD）**，装回后仍须重锁。  
另存 `failover_refuse_v1142.log` / `.csv`。

改回 Hold 5m（现网需要守时）：

```bash
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -H 'Content-Type: application/json' \
  -d '{"anomalyPolicy":2}' "http://$IP/save"
```

## 5. 不要做

- 不要装/调用 `ntpdate` / `sntp` / `chronyd`  
- 不要把拔 PPS 线当 Holdover 主测  
- 不要在 HLD 中途改 `anomalyPolicy`  
- 不要开 RMT、不要清 NVS  
- 不要只贴结论不贴 `transitions:` 与关键 NTP 行

## 6. 回报模板

写入 `docs/fw_flash_v1142_result_20260921.md` 后 **git commit + push**（分支 `cursor/pps-resume-deadlock-6c51`）。  
把本轮 log/CSV 放进 `docs/`。

```text
RESULT: PASS|FAIL
date:
ip:
fwMark:          # 必须 v1.1.42
fwVersion:
ota_http:
ota_relock_s:    # OTA 重启后回到 LCK 的秒数；未回则 FAIL 并附 debug log
anomalyPolicy_during: 2
holdoverSec: 300
inject: module_vcc_pull
ntpdate_used: no
monitor: pps_failover_monitor.py

baseline_ntp:
transitions:
hld_enter_s:
hld_ntp_checkpoints:
uns_at_holdoverMs:
uns_ntp:
recovery_s_after_uns:
lck_ntp_after:
hold_offset_drift_ms:
refuse_variant: skip|PASS|FAIL
notes:
```

## 7. 一句话

**先 OTA 到 v1.1.42，再拔模块电源跑完 Hold 5m；第 6 项必须重锁，否则带 `/debug/log` 回来。**
