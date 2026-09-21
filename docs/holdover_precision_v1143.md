# 交接：守时精度长测（v1.1.43）— 时钟长测 + debug log

> **状态：待执行**  
> Termux Alpine / 无 `ntpdate`。测 **GPS 失效后外推守时精度**（不测短链恢复）。  
> 日期：2026-09-21 · 分支 `cursor/holdover-options-a05e` · 固件 **v1.1.43**  
> 对照：5 min 链已 PASS 但 NTP 噪声淹没真实误差 — [fw_flash_v1142_result_20260921.md](fw_flash_v1142_result_20260921.md)  
> 总览：[CURRENT.md](CURRENT.md) · 入口：[rmt_pps_agent_brief.md](rmt_pps_agent_brief.md)

## 0. 你要做什么

1. **OTA** 到 **v1.1.43**（保 NVS）。  
2. 设守时档为 **Hold 30m**（推荐）或 **Hold 1h**（更深）。  
3. **开时钟长测 (PSRAM)** 录全段（基线 + HLD + 恢复）。  
4. 清空 **RAM debug log**，拔 **GPS 模块电源**，跑完守时窗，装回，恢复 LCK。  
5. 停录 → 拉 BIN/CSV + `/debug/log` + failover 监测 log，写结果 md。

| 项 | 值 |
|---|---|
| 目标板 | ESP32-S3 · IP **`10.121.95.14`**（以 `/status` 为准） |
| 口令 | `NTP-9EC4` |
| 推荐策略 | **`anomalyPolicy=4` / Hold 30m / `holdoverSec=1800`** |
| 可选加深 | `anomalyPolicy=5`（1h）或 `6`（2h） |
| 注入 | **拔模块电源**（干净；勿拔 PPS 杜邦线） |
| 监测 | `pps_failover_monitor.py`（stdlib）+ `clock_trace_client.py` + `/debug/log` |
| 禁止 | `ntpdate`；中途改策略；REC 态拉 clock-trace |

### 为何加长档

5 min HLD 的 NTP offset 抖动 ±2~5 ms，真实外推误差估 **0.1~0.3 ms/5 min**，测不出斜率。  
≥30 min 才能让线性外推误差冒出测量噪声；时钟长测在 **HLD 段仍 1 Hz 记样**（v1.1.43：PPS 停则墙钟补样）。

## 1. 镜像

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/holdover-options-a05e/dist/firmware_esp32s3.bin
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `firmware_esp32s3.bin` | **1104096** | `784a63c9375078dc0a42f2e56ea45286a557f5c5bdb4816d8bb2662e625b1dc7` |

```bash
curl -fL -o firmware_esp32s3.bin \
  'https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/holdover-options-a05e/dist/firmware_esp32s3.bin'
wc -c firmware_esp32s3.bin   # 必须 = 1104096
echo '784a63c9375078dc0a42f2e56ea45286a557f5c5bdb4816d8bb2662e625b1dc7  firmware_esp32s3.bin' | sha256sum -c
```

不符 → **停，不要刷**。本地 `dist/` 已有同文件也可直接用，仍须核对大小+SHA256。

## 2. OTA

```bash
IP=10.121.95.14
PASS='NTP-9EC4'

curl -fsS "http://$IP/status"   # 记下 fwMark

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -X POST -d "password=$PASS" "http://$IP/login" -o /dev/null

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -F "firmware=@firmware_esp32s3.bin" --max-time 180 \
  "http://$IP/ota" -w '\nota HTTP %{http_code}\n'

# 回线后须 fwMark=v1.1.42→v1.1.43，并回 LCK
for i in $(seq 1 30); do curl -fsS --max-time 3 "http://$IP/status" && break; sleep 2; done
```

## 3. 设守时档 + 开录 + 清 log

```bash
# Hold 30m（apol=4）；改 5/6 即 1h/2h
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -H 'Content-Type: application/json' \
  -d '{"anomalyPolicy":4}' "http://$IP/save"

curl -fsS "http://$IP/status" | python3 -c \
  "import sys,json;j=json.load(sys.stdin);print(j.get('fwMark'),j.get('anomalyPolicy'),j.get('anomalyLabel'),j.get('holdoverSec'))"
# 期望: v1.1.43  4  Hold 30m  1800

# 清空 debug 环
curl -fsS "http://$IP/debug/log?pass=$PASS&clear=1" >/dev/null

# 时钟长测：有旧 STOP 缓冲先拉走/清空
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" clear || true
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" start
curl -fsS "http://$IP/debug/clock?pass=$PASS"   # state=REC
```

## 4. 失效链监测（与录制并行）

窗口 ≈ `60 + holdoverSec + 240`（30m → ~35 min；`--max-window` 默认 14400）。

```bash
mkdir -p /tmp/hold_prec && cd /tmp/hold_prec
# 仓库内或 raw 拉脚本
python3 pps_failover_monitor.py \
  --host "$IP" --mode failover --pull-delay 60 --recovery 300 \
  --csv hold30m_mon.csv \
  2>&1 | tee hold30m_mon.log
```

节奏：脚本已打 LCK → **60 s 内拔模块电源** → 见 `>>> replug` 后装回 → 等 LCK。

## 5. 收束产物

```bash
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" stop
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" fetch \
  -o hold30m_trace.csv --save-bin hold30m_trace.bin
curl -fsS "http://$IP/debug/log?pass=$PASS" -o hold30m_debug.log
# 可选再 clear 环
python3 tools/clock_trace_client.py --host "$IP" --pass "$PASS" clear || true
```

把 `hold30m_*` 与结果 md 提交到本分支 `docs/`。

## 6. 验收

| # | 项 | 期望 |
|---|---|---|
| 1 | 进 HLD | 断电数秒内 `LCK→HLD`；trace `state=3` 连续 |
| 2 | HLD 时长 | ≈ `holdoverSec`（30m→1800 s）后 `UNS`；不被 quality 提前砍（v1.1.43 MAX_Q=2000） |
| 3 | 长测覆盖 HLD | HLD 段样本 ≈ 守时秒数（±少许）；`ppsCount` 冻结、`holdoverMs` 爬升 |
| 4 | debug log | 有 `state LCK→HLD` / 周期性 `[clk] HLD age_s=…` / `→UNS` / 恢复 |
| 5 | 守时精度 | 用 monitor NTP 检查点 + 恢复后 LCK offset 相对基线；报告漂移斜率（ms/min）与恢复净差 |
| 6 | 恢复 | 装回后 `ACQ→LCK`，NTP `GPSS`，无跳秒感 |

分析提示（本地）：

```bash
python3 - <<'PY'
import csv
rows=list(csv.DictReader(open('hold30m_trace.csv')))
h=[r for r in rows if r['state']=='3']  # HLD
print('HLD samples',len(h),'holdoverMs',h[0]['holdoverMs'],'→',h[-1]['holdoverMs'])
print('q',h[0]['qualityMs'],'→',h[-1]['qualityMs'],'ppm',h[0]['freqPpm'],'→',h[-1]['freqPpm'])
PY
```

## 7. 回报模板

写入 `docs/holdover_precision_v1143_result_20260921.md`：

```text
RESULT: PASS|FAIL
fwMark: v1.1.43
anomalyPolicy: 4
holdoverSec: 1800
inject: module_vcc_pull
trace_hld_samples:
trace_hld_holdoverMs_first/last:
debug_log_has_HLD_lines: yes|no
baseline_ntp_off:
hld_checkpoints:   # 10%/40%/80% 或 monitor 打印
recovery_net_offset_ms:
drift_ms_per_min:  # 自估
notes:
```

## 8. 一句话

**OTA v1.1.43 → Hold 30m → 开 clock-trace + 清 debug log → 拔模块电源跑满窗 → 收 CSV+log，量化外推斜率。**
