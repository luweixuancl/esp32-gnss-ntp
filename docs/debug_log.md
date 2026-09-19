# RAM 调试日志（免串口拉 boot/RMT）

> 固件 **v1.1.34+** · `DEBUG_LOG_EN=1`（默认）· 环缓 **32 KB**

上电早期关键行（`[pps-rmt]` / `[gps]` / `[wifi-evt]` / boot）在写 Serial 的同时写入 RAM 环形缓冲。STA 入网后用 HTTP 拉取，无需一直挂 USB 串口。

## API

| 方法 | 路径 | 认证 |
|---|---|---|
| `GET` | `/debug/log` | 登录 cookie，或 `?pass=` / `X-Debug-Pass`（与 `/cfg` 配置口令相同） |
| `GET` | `/debug/log?clear=1` | 同上；返回后清空环缓 |
| `POST` | `/debug/log/clear` | 同上 |

口令默认与 SoftAP 相同（OLED / 历史串口 `SoftAP default pass=`；环缓里也有一行）。

### curl 示例

```bash
PASS='NTP-9EC4'   # 换成设备口令
IP=192.168.1.24

curl -fsS "http://$IP/debug/log?pass=$PASS" -o boot_log.txt
# 或拉完清空：
curl -fsS "http://$IP/debug/log?pass=$PASS&clear=1"
```

响应为纯文本，首行元数据：

```text
# debug_log fw=v1.1.34 used=1234 dropped=0 uptime_ms=… heap=…
[pps-rmt] idf5 ready …
[pps-rmt] dump n=1 val0=0x…
```

## 注意

- 断电/重启丢失；满环覆盖最旧内容（`dropped` 递增）。
- 勿在 ISR 里调用 `debugLogf`（当前接入点均在任务上下文）。
- 砖机、起不来 WiFi 时仍需串口。
- `DEBUG_LOG_EN=0` 可编译关掉。
