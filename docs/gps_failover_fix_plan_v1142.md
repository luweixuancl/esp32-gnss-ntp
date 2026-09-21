# 修正计划：PPS 重启死锁（v1.1.41 失效链第 6 项）

> 状态：**代码已加固，待现网 OTA 复测**  
> 日期：2026-09-21  
> 依据：[gps_failover_hold5m_result_20260921.md](gps_failover_hold5m_result_20260921.md) · log/csv 同目录  
> 固件目标：**v1.1.42** · 现网仍是 v1.1.41  
> 烧录复测任务书：[fw_flash_v1142.md](fw_flash_v1142.md)

## 0. 报告结论（先定性）

2026-09-21 Hold 5m 拔模块电源（`10.121.95.14` / v1.1.41）：

| # | 项 | 结果 |
|---|---|---|
| 1 | 进 HLD | **PASS** — 断电后 ~2 s，`LCK→HLD` |
| 2 | HLD NTP | **PASS** — `li=0 st=1 GPSS`，disp 随 age 升 |
| 3 | 守时 | **PASS** — HLD+120/−240 相对基线漂移 **&lt;2 ms**（+240 约 4.3 ms 含 RTT，仍远好于 50 ppm 标称） |
| 4 | 超时 UNS | **PASS** — `holdoverMs=300000` 整点转 UNS |
| 5 | UNS 拒绝 | **PASS** — `li=3 st=16 INIT` |
| 6 | 恢复 | **FAIL** — 装回后 GNSS 全健康，时钟恒驻 **ACQ**，`anchor=0 stable=0` 25+ min |

Refuse 对照轮被本 bug 阻塞，未测。

transitions: `LCK → HLD@64s → UNS@364s → ACQ@400s`，窗口 600 s 内未回 LCK。CSV 尾段 `ppsCount` 约 1 Hz 递增、`fix=True`、`sat=16`、`freqPpm` 冻在 −10.64，与「边沿进了 GPS 计数、没进 LocalClock 环」一致。

## 1. 根因（独立复核，成立）

`LocalClock::onPpsEdge` 把新边沿与**最后一条被接受**的环内边沿比间隔。`|interval−1 s| > 5 ms` 则丢弃该边沿、**不推进环**，但会更新 `lastPpsCount_`。

模块断电 300 s：环里仍是断电前最后一条边沿。`ppsCount` 是软件计数（`gps_service` ISR +1），断电期间不加，装回后第一条 `delta==1`、`interval≈300 s` → 判离群 → 丢弃。之后每一条都仍在和那条**停机前**边沿比，间隔只更大 → **基线永不前进** → `ppsBadStreak_≥3`、`ppsStable_=0` → `onNmeaCommit` 的 `!haveAnchor_` 引导永不执行 → 永久 ACQ。NTP 保持 `INIT`。

这是相对 2026-09-16 拔电史档的**纯回归**：当时还没有这道为 RMT 双边沿准备的离群门；OTA 冷启动走 `reset()` 清环，所以日常上电看不出来。

诊断指纹（现网已见到）：`fresh=1 nmea=1 zda=1 rmc=1 pps +1/s`，唯 `anchor=0 stable=0`。

## 2. 初版补丁不够的地方（`a2fcf55`）

方向对：`interval ≥ 1.5 s` 视为 PPS 流重启，清环并接受该边沿。宿主场景 2（360 s 后 resume）能重锁。

漏洞：

1. **只在 `delta==1` 时看长间隔。** 计数跳变（`delta>1`）会绕过重启分支，把新边沿推进**未清的旧环**。
2. **锁定/守时时走重启分支仍会 `UTC += 1`。** 漏一个 PPS（真间隔 ~2 s ≥ 1.5 s）会被当成重启，相位只加 1 秒 → 相对 GNSS 慢约 1 s，直到残差把门打穿。
3. **未覆盖 HLD 期内装回**（产品路径：模块闪断 &lt;300 s）。
4. 结果里「整机重启后依旧 ACQ」与「只有 `reset()` 清环才能恢复」互相矛盾——若根因只是脏环，OTA/重启后 GPS 已插上应能锁。复测必须单独确认这一点（过早看 ACQ、或还有第二根因）。

## 3. 本分支加固（仍标 v1.1.42）

`onPpsEdge` 分类：

| 间隔 | 判定 | 动作 |
|------|------|------|
| `\|Δ−1 s\| ≤ 5 ms` | 正常 1 Hz | 进环、清 badStreak |
| `≥ 1.5 s` | **流重启**（与 delta 无关） | 清环 + badStreak；若当时 LCK/DEG/HLD → `enterUnsynced()`（禁止跨洞 `+1 s`）；再接受该边沿 |
| `&lt; 1.5 s` 且不是 1 Hz | EMI 双边沿 | 原吸收路径（丢边沿、不进环） |

`enterUnsynced()` 本身仍保留环（短毛刺后还能用 ppm）；**只有长间隔这条路径清环**。

宿主测试 `tools/local_clock_host_test/`（g++ 即可）：

```bash
cd tools/local_clock_host_test
g++ -std=c++17 -Wall -I stubs -I ../../include \
    ../../src/local_clock.cpp test_local_clock.cpp -o t && ./t
```

| 场景 | 期望 |
|------|------|
| 1 冷启动 | LCK |
| 2 360 s 过守时再 resume | +1 s anchor/stable，+3 s LCK |
| 3 20 ms 毛刺 | 仍 LCK |
| 4 HLD 30 s 内 resume | 重锁 |
| 5 锁定中 2 s 空洞 | 先 UNS（不得带着 +1 s 继续 LCK），再重锁 |
| 6 360 s 后首边沿 `delta>1` | 仍能重锁 |

## 4. 执行顺序

1. ~~定位根因~~ 现场 log/CSV + 代码路径已闭环  
2. ~~加固 `onPpsEdge` + 宿主回归~~ 本分支  
3. 编译 C3/S3，写入 `dist/`（**只 OTA `firmware_esp32s3.bin`**）  
4. 辅助 AI：**Web OTA v1.1.41 → 1.1.42**（保 NVS，不开 RMT，不擦除）  
5. 重跑 Hold 5m 全链，**第 6 项必须 PASS**（装回后回到 LCK，NTP `GPSS`，offset 回基线带）  
6. 时间够：Refuse 对照（`anomalyPolicy=0`，期望 `LCK→UNS` 无 HLD，装回重锁）  
7. 若第 6 项仍 FAIL：立刻拉 `/debug/log` + `/status`，不要先重启；对照是否仍是 `anchor=0 stable=0`（第二根因）还是别的

## 5. 验收（复测 PASS 才算结）

- `fwMark=v1.1.42`  
- Hold 5m 第 1–6 项全 PASS；恢复后 `ACQ→LCK` 建议 &lt;15 s（捕星另计）  
- 指纹 `anchor=0 stable=0` 在 PPS 恢复后不得持续 &gt;5 s  
- 20 ms 级毛刺不得把锁定打成 UNS（宿主场景 3；现场不专门打毛刺）  
- 不改 RMT、不擦 NVS、不用 C3 镜像刷 S3  

## 6. 不在本计划内

- 温补系数 / 守时产品档（已评估，与本 bug 无关）  
- 开 RMT  
- 把 50 ppm 色散地板改成真实 0.3 ppm（诚实标称，另议）  
- 合并进 `main`（现 `main` 已是 v1.1.41；42 待复测 PASS 后再谈）
