# 交接：辅助 AI OTA v1.1.41 + `/cfg` 按钮测试（S3）

> **给本地执行侧（Termux / opencode / 现场辅助 AI）**。编译侧已完成，本档是你的唯一任务书。  
> 日期：2026-09-20 · 分支 `cursor/clock-psram-ring-a05e` · 镜像提交 `7d2c47d`  
> 总览：[CURRENT.md](CURRENT.md) · 上一轮 OTA PASS：[fw_flash_v1140_result_20260920.md](fw_flash_v1140_result_20260920.md)

## 0. 你要做什么

1. **Web OTA** 把现网 S3 从 **v1.1.40 → v1.1.41**（保 NVS）。  
2. **测 `/cfg` 时钟长测四个按钮**是否按真实状态启用（这是本版唯一新功能）。  
3. 写结果到 `docs/fw_flash_v1141_result_20260920.md`（文末模板）。  

**不要改代码、不要开 RMT、不要整片擦除、不要从 `main` 下镜像。**

| 项 | 值 |
|---|---|
| 目标板 | ESP32-S3 · 上场 IP **`10.121.95.14`**（DHCP，以 `/status` 为准） |
| 口令 | 上场用过 `NTP-9EC4`（NVS 保留则仍是它） |
| 路径 | **只走 Web OTA**；USB 仅当 OTA 失败 |
| 禁止 | C3 `firmware.bin`；`merged*_0x0.bin`；`erase_flash`；`GPS_PPS_RMT_EN=1` |

## 1. 镜像（先核对再刷）

```text
https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/clock-psram-ring-a05e/dist/firmware_esp32s3.bin
```

| 文件 | 大小 | SHA256 |
|---|---|---|
| `firmware_esp32s3.bin` | **1102992** | `f4766d90a6d7e21ce2ecc391ae63212993fce208367800be7a4ea30b57124dba` |

```bash
curl -fL -o firmware_esp32s3.bin \
  'https://gh-proxy.com/https://raw.githubusercontent.com/luweixuancl/esp32-gnss-ntp/cursor/clock-psram-ring-a05e/dist/firmware_esp32s3.bin'
wc -c firmware_esp32s3.bin   # 必须 = 1102992
echo 'f4766d90a6d7e21ce2ecc391ae63212993fce208367800be7a4ea30b57124dba  firmware_esp32s3.bin' | sha256sum -c
```

不符 → **停，不要刷**。本地 `dist/` 已有同文件也可直接用，仍须核对大小+SHA256。

OTA 前若设备仍在 **STOP 且 count>0**，先拉走再刷（OTA 重启清环）：

```bash
python3 tools/ct_fetch_bin.py --host "$IP" --pass "$PASS" --save-bin pre_ota.bin -o pre_ota.csv || true
```

（该工具只读；若 state≠STOP 会拒绝，属正常。）

## 2. Web OTA

```bash
IP=10.121.95.14          # 以现场为准
PASS='NTP-9EC4'

curl -fsS "http://$IP/status"   # 记下 fwMark，期望 v1.1.40

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

`fwMark` / `fwVersion` 必须是 **`v1.1.41` / `1.1.41`**。很快回 LCK，NTP `stratum=1` `GPSS` `li=0`，WiFi/口令/IP 仍在。

## 3. 按钮测试（本版重点，必须用浏览器打开 `/cfg`）

登录 `http://$IP/cfg`（或 `/login` 后进设置）。卡片 **「时钟长测 (PSRAM)」**。  
不要只靠 curl：按钮 `disabled` 只能在页面上看到。

每步在页面控制台跑一次（或目视记录）：

```javascript
[...document.querySelectorAll('#ctBtnStart,#ctBtnStop,#ctBtnClear,#ctBtnFetch')]
  .map(b => ({id:b.id, disabled:b.disabled, text:b.textContent}))
```

状态行应显示中文（空闲 / 录制中 / 已停止）+ **已录** 时长，不是只显示 `IDLE/REC/STOP`。

| 步 | 操作 | 期望（disabled=true 表示灰掉） |
|---|---|---|
| A 空闲 | OTA 后若仍 STOP：先点「清空」。应到 **空闲** | Start **可用**；Stop/Clear/Fetch **禁用** |
| B 开始 | 点「开始录制」 | 立刻变 **录制中**；仅 Stop 可用；样本数约 1 Hz 往上加；「已录」走动 |
| C 误点 | 录制中再点 Start / 清空 / 下载 | **点不了**（disabled）；`/debug/clock` 仍 `REC` |
| D 停止 | 点「停止」（录 ≥30 s） | **已停止**；Clear + Fetch 可用；**Start 禁用**（必须先清空） |
| E 下载 | 点「下载 BIN」 | 页不跳走；下载中四键全禁用；完成后仍 STOP，提示「已保存 …」；bin 以 `CTRB` 开头，大小 = `32+count×42` |
| F 清空 | 点「清空」 | 回到 **空闲**，同 A；样本 0 |

短录即可（30–90 s），不要开 12 h。  
CLI 回归可选：`ct_fetch_bin.py` 在 STOP 后仍能拉（只读、显式分页）。

### API 对照（curl，辅助）

```bash
curl -fsS "http://$IP/debug/clock?pass=$PASS"
# IDLE: state=IDLE  → 仅 Start
# REC:  state=REC   elapsedMs 增加
# STOP: state=STOP  count>0
```

`xferBusy` 仅在下载瞬间为 true，结束后应 false。`/status` 下载期 `ntpServing=false` / `refId=RSTR`，结束后恢复 `GPSS`。

## 4. 不要做

- 不要从 `main` 下固件（那是 v1.1.39）
- 不要刷 v1.1.40 的 1100704 B 包（本任务是 **1102992**）
- 不要整片擦除、不要 RMT、不要改仓库源码
- 不要在 REC 态拉数据；不要拉完不清就报完成

## 5. 回报模板

写入 `docs/fw_flash_v1141_result_20260920.md` 后 **git commit + push 本分支**。

```text
RESULT: PASS|FAIL
path: OTA
ip:
fwMark_before:
fwMark_after:
sha256_ok: yes|no  size:
ota_http:
back_online_s:
lck_s:
ntp: stratum / refId / li / timeValid
nvs_kept: yes|no
buttons:
  A_IDLE:  Start=en Stop=dis Clear=dis Fetch=dis
  B_REC:   Start=dis Stop=en  Clear=dis Fetch=dis  elapsed=  count=
  C_blocked_ok: yes|no
  D_STOP:  Start=dis Stop=dis Clear=en  Fetch=en
  E_fetch: page_stayed=yes|no  filename=  bytes=  CTRB=yes|no
  F_CLEAR: back_to_IDLE=yes|no
notes:
```
