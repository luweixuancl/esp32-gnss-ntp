# `/status` JSON 非严格 1 Hz 更新 —— 现象分析与实测（2026-09-18）

> **历史归档**：下文口径以当时固件为准。当前 `main` = **v1.1.39** / IDF5，见 [CURRENT.md](CURRENT.md)。


> 面向其他 AI / 后续维护者的自包含分析文档。结论先行：**设备侧 1 Hz 时钟本身准确**，
> 「不严格 1 Hz」是「请求-响应」链路上三层因素叠加的观测效应，固件无缓存、无节流、无时钟失准。
> 所有结论附 `文件:行号` 引用（以 2026-09-18 工作区为准），实测可复刻。

## 1. 现象

以 1 Hz 轮询 `http://192.168.1.24/status` 时，JSON 内的秒级字段
（`uptimeSec` / `gps.ppsCount` / `gps.utcEpoch`）相对本地墙钟呈现：

- **重秒**：相邻两拍返回完全相同的设备秒（+0 增量）；
- **跳秒**：相邻两拍之间设备秒 +2；
- **迟到**：个别响应耗时 >1 s，最差 >2 s（客户端超时）。

测试环境：ESP32-S3（fw v1.1.15，`otaChip:"ESP32-S3"`），STA 连接 `H3C_LuxYang`，
**RSSI −68 dBm（偏弱）**，时钟态 `LCK`、`freqPpm ≈ −11.15`、PPS 正常、10 颗星。
轮询客户端：Android Termux python3（与设备同一 WLAN）。

## 2. 实测方法与证据

### 2.1 方法（两阶段采样）

日志：`/tmp/opencode/status_cadence.log`（66 行，临时文件，可按下方命令重采）。

- **相位 1（考察服务端）**：名义 0.5 s 间隔 ×48 拍（`time.sleep(0.5)` 串行 fetch）。
  周期 = sleep + RTT，故常规节拍 0.56–0.68 s ⇒ **基线 RTT ≈ 60–180 ms**。
- **相位 2（考察 1 Hz 对齐）**：绝对时刻对齐的严格 1.000 s 网格 ×15 拍
  （`tgt = t0 + i; sleep(tgt - now)`），排除客户端节拍误差后观察服务端。

每拍记录：本地接收时刻、`uptimeSec`、`ppsCount`、`utcEpoch`、`ageMs`、`qualityMs`、`tempC`。

复刻命令（只依赖标准库，可直接重跑）：

```bash
python3 -c "
import json,time,urllib.request
url='http://192.168.1.24/status'
def sample():
    t=time.time()
    try:
        d=json.load(urllib.request.urlopen(url,timeout=2))
        g=d.get('gps',{})
        return (t,d.get('uptimeSec'),g.get('ppsCount'),g.get('utcEpoch'),g.get('ageMs'))
    except Exception as e:
        return (t,'ERR',repr(e)[:60],'','')
# 相位1：0.5s x48（串行）
for i in range(48):
    print('%.3f up=%s pps=%s utc=%s age=%s'%sample(),flush=True); time.sleep(0.5)
# 相位2：绝对对齐 1.000s x15
t0=time.time()+5
for i in range(15):
    dt=t0+i-time.time()
    if dt>0: time.sleep(dt)
    print('%.3f up=%s pps=%s utc=%s age=%s'%sample(),flush=True)
"
```

> 也可用 `tools/clock_drift_monitor.py`（1 Hz `/status` 长测）配合本节判读要点。

### 2.2 相位 1 结果（0.5 s 名义间隔，48 拍）

- 46 拍成功，**2 拍 `TimeoutError`（>2 s）**（日志行 43、49，绝对时刻 ≈501.1 / 507.1）；
- 1 次隐性慢请求：487.261 → 489.523 单周期 2.26 s ⇒ 该次请求耗时 ≈1.76 s（日志行 21→22）；
- 其余周期 0.56–0.68 s，基线 RTT 60–180 ms；
- 成功拍里 `up/pps/utc` 与墙钟逐秒一致推进 ⇒ **设备字段本身无失准**。

### 2.3 相位 2 结果（严格 1.000 s 网格，15 拍）

| 本地接收时刻 | up | pps | utc | 判读 |
|---|---|---|---|---|
| …515.606 | 455 | 454 | 515 | 基准 |
| …516.606 | **457** | **456** | **517** | **跳秒 +2**：该请求被设备延迟 ~1 s 处理，落进设备第 457 秒才打戳 |
| …517.751 | 457 | 456 | 517 | 本拍响应迟到 +1.145 s（fetch 于 516.606 发出） |
| …519.606 | 460 | 459 | 520 | 基准 |
| …520.679 | **460** | **459** | **520** | **重秒 +0**：响应迟到 +1.073 s，仍落在同一设备秒 |

15 拍中 2 拍迟到 >1 s，直接制造了一对「跳秒 + 重秒」。相位 1、2 合计 ~40 s 内
出现 3–4 次亚秒~2 s 级服务端延迟毛刺，间隔无 obvious 周期（4–15 s 不等）。

## 3. 代码链路（打戳点定位）

数据流：`task-time` 持续刷新 `published_` 快照 → HTTP 请求到达 → `task-net` 执行
`handleStatus()` 现场组装 JSON → **打戳时刻 = 请求被处理的时刻**。

1. **路由**：`src/web_portal.cpp:91` 注册 `/status` → `handleStatus()`。
2. **每请求现算，无缓存/节流**：`handleStatus()` 整段（`src/web_portal.cpp:1060-1205`）：
   - `uptimeSec = millis()/1000`（:1083）——请求处理瞬间取值；
   - `const GpsStatus st = gps_->snapshot()`（:1130）——拷贝 `published_`；
   - `utcEpoch/ageMs/timeValid/qualityMs` 写入 JSON（:1164-1167）；
   - `serializeJson` + `server_.send`（:1202-1205），body ≈1.6 KB。
3. **快照刷新机制**：`task-time` 循环每拍 `gGps.loop()`（`src/main.cpp:548`）；
   循环头 `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1))`（`src/main.cpp:528`）——
   PPS 通知或 1 ms 超时唤醒，故 `published_` 新鲜度为 **毫秒级**；
   `work.utcEpoch` 来自 `nowUtc()` 的 PPS 锚定连续外推（`src/gps_service.cpp:563-566`），
   `publishStatus()` 临界区整体发布（`src/gps_service.cpp:601-605`），
   `snapshot()` 临界区整体拷出（`src/gps_service.cpp:618-624`）——**无撕裂风险**；
   `ageMs = gps_.time.age()` 是「最近一条 NMEA 时间句龄」的 0→1000 ms 锯齿
   （`src/gps_service.cpp:550`），不是 PPS 龄，判读时勿混淆。
4. **task-net 服务路径**：单循环串行执行 扫描驱动 / NetRequest 队列 / 连接请求 /
   `gPortal.loop()`(→`handleClient`) / `gOta.poll()` / 链路快照刷新 / 堆检查，
   尾部 `vTaskDelay(pdMS_TO_TICKS(5))`（`src/main.cpp:645-678`）；
   任务优先级 `task-time` 5 > `task-net` 2 > `task-ui` 1，全部钉在 core 0
   （`src/main.cpp:745-748`；S3 单核用法，见 `docs/wifi_event_fsm.md`）。
   OTA 期间 task-time 主动让路（`src/main.cpp:520-526`）、task-net 提权——与本现象无关。
5. **网页端调度**：`/` 页 JS 为 `tick(); setInterval(tick,1000); setInterval(paintTime,250)`
   （`src/web_portal.cpp:454`）；`tick()` 异步 fetch，`tickBusy` 防重入
   （:361, :386-387, :452）——**上次 fetch 未完成时下一拍整拍跳过**；
   页面显示的 UTC/本地时间由 `paintTime` 每 250 ms 用**手机本地钟**外推
   （`baseEpoch + (Date.now()-baseMono)`，:379-385, :411），与设备秒沿无对齐关系。

## 4. 根因分层

**第 1 层（原理性，不可消除）：异步时钟采样混叠。**
打戳时刻是「请求被处理的瞬间」，而 `uptimeSec/ppsCount/utcEpoch` 的值在设备秒网格上
阶跃。客户端 1 Hz 定时器与设备 PPS/millis 是两个自由运行时钟（手机晶振 ppm 级偏差 +
RTT 抖动），相位必然缓慢漂移；当轮询相位贴近秒沿时，RTT 的 ±百 ms 抖动就足以让
相邻两拍踩到同格（重秒）或跨格（跳秒）。**这是采样定理层面的效应，不是 bug。**

**第 2 层（主犯，可缓解）：服务端延迟毛刺。** 实测基线 RTT 60–180 ms，
毛刺 1–2 s+，~40 s 内 3–4 次。来源（按嫌疑排序）：

- **单核竞争**：`task-net`(prio 2) 被 `task-time`(prio 5) 的 NMEA 突发解析、NTP UDP、
  LocalClock tick 抢占；task-net 自身循环内 HTTP 与 OTA poll/扫描/ARP 串行
  （`src/main.cpp:645-678`），单请求最坏排队 ~若干十 ms；
- **无线链路**：RSSI −68 dBm 偏弱 → 802.11 重传/丢 beacon，airtime 抖动可达秒级；
- **同步 WebServer**：ESP32 `WebServer` 单线程同步处理，慢连接阻塞后续；
- 小项：`settingsCopy(pdMS_TO_TICKS(20))` 互斥上限（`src/web_portal.cpp:1096`）。

**第 3 层（客户端，可修正）：节拍本身不严格。**
- 网页 JS `setInterval(tick,1000)` + `tickBusy`：fetch 超 1 s 即整拍跳过，页面冻一拍；
  浏览器对后台标签还会节流 setInterval；
- 脚本若写 `sleep(1)+fetch` 串行，实际周期 = 1 s + RTT（相位 1 即此形态）。

## 5. 已排除项（避免其他 AI 重查）

| 假设 | 结论 | 依据 |
|---|---|---|
| WiFi modem 省电引入延迟 | **已排除** | `WiFi.setSleep(false)`（`src/wifi_manager.cpp:178`） |
| 服务端 JSON 缓存/节流 | **不存在** | `handleStatus` 每请求现算（`src/web_portal.cpp:1060`），仅 NTP 有速率限制，HTTP 无 |
| 设备 1 Hz 时钟失准 | **排除** | 相位 1 成功拍 `up/pps/utc` 与墙钟逐秒一致；LCK、PPS fresh、residual 0 |
| 快照撕裂/读到旧值 | **排除** | `publishStatus`/`snapshot` 均临界区整体拷贝（`src/gps_service.cpp:601-624`） |
| NVS/设置读阻塞（20 ms 上限） | 非主因 | 有毛刺远超 20 ms；且 settings 失败时也有兜底默认值（:1111-1113） |

## 6. 改进选项（未实施，按需选用）

- **A. 观测侧（零改动，推荐先用）**：轮询脚本用绝对时刻对齐调度（§2.1 相位 2 写法）；
  节奏判读用 `ppsCount`/`uptimeSec` 增量而非响应到达时刻；容忍偶发重秒/跳秒；
  需要设备秒沿精对齐时改用 NTP 查询而非 HTTP。
- **B. 网页侧**：`tick()` 完成后自校正调度 `setTimeout(tick, max(0, 1000-elapsed))`
  替代 `setInterval`；`tickBusy` 加超时续命（如 >3 s 视为僵死放行）而非整拍跳过；
  可选：显示时间改由 `utcEpoch + ageMs`（设备侧信息）锚定。仅改
  `src/web_portal.cpp` 内嵌 JS 字符串（:361-454）。
- **C. 固件侧**：`/status` 增发设备秒沿信息（如 `utcFracMs`，`nowUtc()` 已有 frac；
  或 `srvMs` 打戳耗时）供客户端对齐混叠判读；根治延迟毛刺靠改善 RSSI（当前 −68 dBm）
  与 WiFi 链路质量，单核任务结构调整（task-net 拆分/提权）收益有限且风险高。

## 7. 一句话结论

`/status` 的「非严格 1 Hz」= **请求时打戳（web_portal.cpp:1060,1083）× 服务端
1–2 s 级延迟毛刺（单核抢占 + 弱信号重传）× 客户端 setInterval/串行 sleep 的节拍误差**
三者叠加的采样混叠；设备 UTC/PPS 1 Hz 时钟经实测逐秒准确，固件无需修时钟，
按 §6 A→B→C 顺序处理即可。

— 2026-09-18，基于 fw v1.1.15 @ 192.168.1.24 实测与当时工作区代码行号。

## 8. 固件跟进（v1.1.16）

已按 §6 B/C 落地网页侧与轻量固件侧：

- 状态页 `tick()` 改为自校正 `setTimeout`（目标周期 1 s − elapsed）
- `tickBusy` 超过 3 s 允许重试，避免僵死整拍跳过
- 迟到 `/status` 若 `utcEpoch` 落后已绘制秒则不回拨
- `gps.utcFracMs` 用于锚定秒内相位
- RMT 诊断字段仅在 `GPS_PPS_RMT_EN=1` 时进入 JSON（缩小 body）
