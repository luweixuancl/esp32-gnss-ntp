# v1.1.40 烧录与验收结果（S3，Web OTA）

> 执行：辅助 AI（Termux/opencode）· 日期：2026-09-20  
> 任务书：[fw_flash_v1140.md](fw_flash_v1140.md) · 镜像来自 `64ffa66`（本地 `dist/` 校验，未走网络下载）

```text
RESULT: PASS
path: OTA
ip: 10.121.95.14（现场 DHCP，非任务书历史 IP 192.168.1.24）
fwMark_before: v1.1.39
fwMark_after:  v1.1.40
sha256_ok: yes  size: 1100704（= 任务书值）
ota_http: 200 / 8.47 s（"OK — rebooting into new firmware"）
back_online_s: ≤10（首轮轮询即通）
lck_s: ≤10（首次 /status 已 LCK，GNSS 热启动）
ntp: stratum 1 / GPSS / li 0 / gps.timeValid=true
nvs_kept: yes（WiFi/口令原样，IP 未变）
one_shot_dump: PASS (count=101  bytes=4274=32+101×42  flags=DONE)
notes:
```

## 冒烟清单（任务书 §4）

| # | 检查 | 结果 |
|---|---|---|
| 1 | `/status` `fwMark` | ✅ `v1.1.40` / `fw=1.1.40` |
| 2 | `clock.state` | ✅ LCK（重启后 ≤10 s） |
| 3 | NTP | ✅ stratum 1 · refId `GPSS` · li 0 |
| 4 | `/debug/log` 首行 | ✅ `fw=v1.1.40`；banner `GNSS NTP Server`；GPS 探到 GGA,RMC,ZDA；`pps-rmt` 行 **0** 条 |
| 5 | `/status` 无 `gps.ppsRmt` | ✅ 全文无该键 |

## 一键全量下载验收（任务书 §5）

1. start → 90 s → stop：`count=101 seqNext=101 dropped=0`（v1.1.40，录制中途 `/debug/clock` JSON `fw` 字段已示 v1.1.40）
2. `ct_fetch_bin.py`（只读）：**单响应** seq 0..100，`done=1`，state mix LCK 101/101
3. bin 校验：magic `CTRB`、`count=101 seqNext=101 seqEnd=101 flags=1(DONE)`、字节数 `4274 = 32+101×42` 精确
4. **`Content-Disposition: attachment; filename="clock_trace_0-100_1789884029.bin"`**（seq 范围 + 首样本 UTC，自描述 ✓）、`Content-Length: 4274` 与实发一致
5. `POST /debug/clock/clear` → IDLE 复位 ✓（顺序：stop → 拉全 → clear）

## 备注

- RSSI −33 dBm，OTA 上传 1 075 708 B 用时 8.5 s
- 烧录前设备 STOP 缓冲中的 83 min 数据已先行拉取归档（`data/clock_trace_full.*`，见 [clock_trace_83min_20260920.md](clock_trace_83min_20260920.md)），OTA 重启清环无损失
- 未整片擦除、未动 RMT、未改源码（符合任务书 §6）
