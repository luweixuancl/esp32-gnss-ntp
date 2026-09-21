# 交接：辅助 AI GPS 失效链（Hold 5m）— Termux Alpine

> **状态：已执行 FAIL**（第 6 项恢复死锁）— [gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md)  
> 后续改走 v1.1.42 复测：[fw_flash_v1142.md](fw_flash_v1142.md)  
> 给本地执行侧（Termux / Alpine / opencode）。**不要假设有 `ntpdate`/`chrony`。**  
> 日期：2026-09-21 · 对照史档：[pps_pull_test_20260916.md](pps_pull_test_20260916.md)  
> 总览：[CURRENT.md](CURRENT.md) · 入口：[rmt_pps_agent_brief.md](rmt_pps_agent_brief.md)

## 0. 你要做什么

1. 确认现网 S3 已锁星、NTP 可服务（`fwMark` 以 `/status` 为准，期望 **≥ v1.1.39**，现网多为 **v1.1.41**）。  
2. 把异常策略设为 **Holdover 5min**（`anomalyPolicy=2` / `holdoverSec=300`）。  
3. 用仓库自带 **stdlib-only** 监测脚本跑完整失效链：**拔 GPS 模块电源** → `LCK→HLD→UNS` → 装回电源 → 重锁。  
4. 写结果到 `docs/gps_failover_hold5m_result_20260921.md`（文末模板），`git commit` + `push` 本分支。

**不要改固件、不要开 RMT、不要整片擦除、不要拔 PPS 杜邦线当主路径、不要依赖 `ntpdate`。**

| 项 | 值 |
|---|---|
| 目标板 | ESP32-S3 · 上场 IP **`10.121.95.14`**（DHCP，以 `/status` 为准） |
| 口令 | `NTP-9EC4`（NVS 保留则仍是它） |
| 策略 | **Hold 5m**（`apol=2`，`ahold=300`） |
| 失效注入 | **拔 GPS 模块 VCC / 电源**（干净断开；见 §3） |
| 监测 | `tools/pps_failover_monitor.py`（纯 Python3 标准库：HTTP `/status` + NTP UDP :123） |
| 禁止 | `ntpdate` / `chronyc` / 第三方 NTP 客户端；拔 PPS 线当主测；改 `anomalyPolicy` 后不恢复 |

## 1. 环境（Termux Alpine）

脚本 **只依赖 Python3 标准库**（`urllib` / `socket` / `json` / `csv` / `argparse`）。  
**不需要** `ntpdate`、`ntp`、`chrony`、`bc`、`jq`。

```bash
# Alpine 缺 python3 时：
apk add --no-cache python3 curl ca-certificates

python3 -V          # ≥ 3.8 即可
command -v ntpdate || true   # 不存在也正常，本任务不用它
```

拉脚本（任选；仓库已在本地可直接用 `tools/`）：

```bash
REPO=https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp
# 优先本任务分支；若 404 再试 main（main 上也有同名脚本）
BR=cursor/gps-failover-brief-a05e
curl -fL -o pps_failover_monitor.py \
  "$REPO/$BR/tools/pps_failover_monitor.py" \
|| curl -fL -o pps_failover_monitor.py \
  "$REPO/main/tools/pps_failover_monitor.py"
```

## 2. 开测前核对

```bash
IP=10.121.95.14          # 以现场为准
PASS='NTP-9EC4'

curl -fsS "http://$IP/status" | tee /tmp/pre_status.json
```

期望字段（名称以 JSON 为准）：

| 字段 | 期望 |
|---|---|
| `fwMark` / `fwVersion` | 现网版本（记入结果） |
| `clock.state` | `LCK` |
| `ntpServing` / stratum 相关 | 可服务；NTP 查见下 |
| `anomalyPolicy` | 最终须为 **`2`** |
| `holdoverSec` | 最终须为 **`300`** |
| `gps.fix` | true；`ppsCount` 在涨 |

若策略不是 Hold 5m，登录后改（**NVS 会持久化**；测完可按结果模板决定是否改回）：

```bash
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -X POST -d "password=$PASS" "http://$IP/login" -o /dev/null -w 'login HTTP %{http_code}\n'

curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -H 'Content-Type: application/json' \
  -d '{"anomalyPolicy":2}' \
  "http://$IP/save" -w '\nsave HTTP %{http_code}\n'

curl -fsS "http://$IP/status" | python3 -c \
  "import sys,json; j=json.load(sys.stdin); print('apol',j.get('anomalyPolicy'),'ahold',j.get('holdoverSec'),'label',j.get('anomalyLabel'))"
# 期望: apol 2  ahold 300
```

**NTP 基线（无 ntpdate）** — 脚本 `baseline` 模式已内置三方查询（设备 + 阿里云）：

```bash
python3 pps_failover_monitor.py --host "$IP" --mode baseline
# 或仓库内: python3 tools/pps_failover_monitor.py --host "$IP" --mode baseline
```

期望 DEV：`li=0 st=1 ref=GPSS`（offset/rtt 记入结果即可）。  
ALI 仅作对照路径抖动，不作为 pass/fail。

## 3. 失效注入（物理）

**主路径：拔 GPS 模块电源（VCC）**，不是拔天线、不是拔 PPS 杜邦线。

| 做法 | 期望链 | 说明 |
|---|---|---|
| ✅ 拔模块电源 | `LCK → HLD →（≈300 s）→ UNS` | UART+PPS 同时静默；1.5–2.5 s 内进 HLD |
| ❌ 拔 PPS 杜邦线 | 常直接 `UNS`（假边沿） | 脏拔，**不要当 Holdover 主测** |
| ⏸ 只拔天线 | `/status` 上难与断电区分 | 本轮不做 |

操作节奏：

1. 先开监测脚本（§4），确认已在打 `LCK` 行。  
2. 脚本启动后 **60 s 内**拔掉模块电源（`--pull-delay` 默认 60）。  
3. 看到 `UNS+2s` NTP 检查点打印 `>>> replug now` 后，**装回模块电源**。  
4. 等重捕星 → `ACQ→LCK`；脚本会在恢复后 NTP 检查点结束。

全程设备侧只读（除开测前改策略）。

## 4. 监测命令（主测）

窗口自动 = `pull-delay + holdoverSec + recovery`（默认约 60+300+240 ≈ 10 min，硬顶 `--max-window 1200`）。

```bash
mkdir -p /tmp/gps_failover && cd /tmp/gps_failover

python3 pps_failover_monitor.py \
  --host "$IP" \
  --mode failover \
  --pull-delay 60 \
  --recovery 240 \
  --csv failover_hold5m.csv \
  2>&1 | tee failover_hold5m.log
```

脚本在 HLD 内按 `holdoverSec` 的 10%/40%/80% 打 NTP 检查点，UNS+2s / 重锁 LCK+3s 再各打一次。  
**全程不要用 `ntpdate`。** UNS 时 DEV 可能出现巨大负 offset（未同步时间基字面值）——属预期，记为「拒绝授时」即可。

## 5. 验收清单（PASS 条件）

对照 2026-09-16 史档；以本轮 log/CSV 为准：

| # | 项 | 期望 |
|---|---|---|
| 1 | 进 HLD | 断电后数秒内 `LCK→HLD`；`holdoverMs` 开始累加 |
| 2 | HLD NTP | `li=0 st=1 ref=GPSS`；`disp` 随 age 缓升 |
| 3 | 守时 | HLD 检查点 DEV offset 相对基线漂移应小（史档 2 min <0.5 ms；本轮记实测） |
| 4 | 超时 UNS | `holdoverMs≈300000` 时转 `UNS`；`q` 极大 / 无效 |
| 5 | UNS 拒绝 | `li=3 st=16`（或等价拒绝元数据）；客户端不可用 |
| 6 | 恢复 | 装回电源后回到 `LCK`；NTP 再为 `GPSS`；无整秒跳变感（offset 回基线带） |

任一关键项 FAIL → 整轮 FAIL，log/CSV 仍须提交。

## 6. 不要做

- 不要安装或调用 `ntpdate` / `sntp` / `chronyd`（环境没有就对了）  
- 不要把拔 PPS 线当成 Holdover 主测  
- 不要在 HLD 中途改 `anomalyPolicy`  
- 不要刷固件、不要动 RMT、不要清 NVS  
- 不要只贴结论不贴 `transitions:` 行与关键 NTP 行

## 7. 可选第二轮（Refuse）

时间够再跑：把策略改 `anomalyPolicy=0`，同样拔模块电源，期望 **`LCK→UNS`（无 HLD）**。  
另存 `failover_refuse.log` / `.csv`，在结果文档加一节。时间不够可只写「未测」。

改回 Hold 5m（若现网需要守时）：

```bash
curl -fsS -c /tmp/ntp.cj -b /tmp/ntp.cj \
  -H 'Content-Type: application/json' \
  -d '{"anomalyPolicy":2}' "http://$IP/save"
```

## 8. 回报模板

写入 `docs/gps_failover_hold5m_result_20260921.md` 后 **git commit + push**（建议仍推本任务分支 `cursor/gps-failover-brief-a05e`）。  
把 `failover_hold5m.log` 与 `failover_hold5m.csv` 放进同目录或打成 tar 一并提交。

```text
RESULT: PASS|FAIL
date:
ip:
fwMark:
fwVersion:
anomalyPolicy_before:
anomalyPolicy_during: 2
holdoverSec: 300
inject: module_vcc_pull
env: termux-alpine
ntpdate_used: no
python: 
monitor: pps_failover_monitor.py

baseline_ntp: li= st= ref= off= rtt=
transitions:
# 粘贴脚本末尾 transitions: 行

hld_enter_s:
hld_ntp_checkpoints:
# HLD+30 / +120 / +240 等关键行

uns_at_holdoverMs:
uns_ntp:
recovery_s_after_uns:
lck_ntp_after:

hold_offset_drift_ms:
refuse_variant: skip|PASS|FAIL
notes:
```

## 9. 一句话

**Alpine 无 `ntpdate` 没关系**：用 `pps_failover_monitor.py` 只读跑完「拔模块电源」Hold 5m 全链，交 log+CSV+结果 md。
