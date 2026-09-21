# v1.1.41 OTA 与 /cfg 按钮测试结果（S3）

> 执行：辅助 AI（Termux/opencode，驱动状态 + API 侧验证）+ 用户（浏览器目视按钮灰亮/状态行/下载行为）  
> 日期：2026-09-20 · 任务书：[fw_flash_v1141.md](fw_flash_v1141.md) · 镜像提交 `7d2c47d`（本地 `dist/` 校验，免网络下载）  
> 上一轮：v1.1.40 烧录 PASS — [fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md)

```text
RESULT: PASS
path: OTA
ip: 10.121.95.14
fwMark_before: v1.1.40
fwMark_after:  v1.1.41
sha256_ok: yes  size: 1102992（= 任务书值 f4766d…）
ota_http: 200 / 8.36 s（"OK — rebooting into new firmware"）
back_online_s: ≤10
lck_s: ≤10（首轮 /status 已 LCK，GNSS 热启动）
ntp: stratum 1 / GPSS / li 0 / gps.timeValid=true
nvs_kept: yes（IP/口令原样）
buttons:
  A_IDLE:  Start=en Stop=dis Clear=dis Fetch=dis ✓
  B_REC:   Start=dis Stop=en  Clear=dis Fetch=dis ✓ elapsed=走动 count≈1 Hz（API 核 5.1s→6 样本）
  C_blocked_ok: yes（录制中开始/清空/下载均点不动，/debug/clock 保持 REC）
  D_STOP:  Start=dis Stop=dis Clear=en  Fetch=en ✓（count=132，~132 s）
  E_fetch: page_stayed=yes  filename=clock_trace_0-131_1789885656.bin  bytes=5576  CTRB=yes
  F_CLEAR: back_to_IDLE=yes（「清空」按钮本体点击 → IDLE，API 复核 count=0）
notes: 全程浏览器目视 + curl 双通道一致；下载后 refId 回 GPSS（RSTR 瞬态正常收尾）；无 ppsRmt 键
```

## 服务端交叉验证（E 步）

- `Content-Disposition: attachment; filename="clock_trace_0-131_1789885656.bin"` —— 与浏览器实际保存文件名**逐字一致**（seq 范围 + 首样本 UTC）
- `Content-Length: 5576 = 32 + 132×42` 精确；bin `magic=CTRB count=132 seqNext=132 flags=DONE`
- 下载完成后 `/status`：`refId=GPSS`（NTP 恢复授时），`xferBusy=false` 无残留
- F 后 `/debug/clock`：`IDLE count=0`，卡片状态同 A

## 备注

- OTA 上传 1 102 992 B 用时 8.4 s（RSSI −54 dBm）；重启到 LCK ≤10 s
- 收尾状态：IDLE、LCK、NTP 正常授时；未整片擦除、未动 RMT、未改源码（符合任务书 §4）
