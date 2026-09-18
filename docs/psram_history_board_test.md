# PSRAM `/history` 板测执行单（v1.1.13+）

> 目的：验收 S3 自录环（1 分钟采样）+ 录制开关 + CSV Content-Length + C3 关闭路径。

## 0. 刷机

- 目标 OLED：**`v1.1.13`**（或更新）
- S3 app：`dist/firmware_esp32s3.bin` @ `0x10000`（勿全片擦除）

串口应见：`[history] enabled capacity=10080 bytes=... interval=60s (SPIRAM, mutex)`

## 1. 冒烟

```bash
curl -s http://<IP>/history | python3 -m json.tool
# 期望：intervalSec=60, capacity=10080, recording=true/false

curl -s -D- 'http://<IP>/history.csv?last=3600' -o /tmp/h.csv | head
# 期望：Content-Length: <n>，下载完整结束
wc -c /tmp/h.csv   # 应等于 Content-Length

# 开关
curl -s -X POST http://<IP>/history/ctrl -H 'Content-Type: application/json' \
  -d '{"recording":false}'
curl -s -X POST http://<IP>/history/ctrl -H 'Content-Type: application/json' \
  -d '{"recording":true}'
```

| 检查 | 期望 |
|------|------|
| `intervalSec` | `60` |
| `capacity` | `10080`（约 7 天） |
| 状态页「历史录制」 | 可开始/停止，刷新后保持 |
| CSV | 有 `Content-Length`，浏览器/curl 不中途卡住 |
| C3 `/history` | `enabled:false` |

## 2. 状态页不停秒

打开 `/`，盯秒位 ≥ 60 s；同窗拉 CSV 仍应平滑。
