# Module boundaries (FreeRTOS)

> 与当前代码一致（**v1.1.43**）；细节以 [CURRENT.md](CURRENT.md) 为准（部分章节为历史设计稿）；总览 [CURRENT.md](CURRENT.md)

Tight cohesion / loose coupling rules for the GNSS NTP firmware.

## Tasks

| Task | Owns | Must not |
|------|------|----------|
| **task-time** | GPS, local clock, NTP reply path; **ExtClock::poll** | `WiFi.*`, NVS writes, OLED, WebServer |
| **task-net** | `WifiManager`, HTTP/OTA, SoftAP, scans | OLED drawing, long GPS work |
| **task-ui** | OLED + encoder | `WiFi.*`, direct `gStore.save` |

## Shared state

- **`WifiLinkSnapshot`** (`wifi_types.h`): written only by `WifiManager::refreshLinkSnapshot()` on task-net; any task may `linkSnapshot()`.
- **`AppSettings`**: read via `settingsCopy()`; persist via `settingsCommit()` only. Factory reset before `ipcInit` may call `gStore.save` directly.
- **OTA**: observe `ipcOtaBusy()` / `ipcOtaPhase()`; do not call into `OtaService` from time/ui.
- **ExtClock**: optional DS3231 assist (`EXT_RTC_EN`); task-time polls; feeds `LocalClock::setExtAssist` only.
- **ClockTrace**: task-time appends samples; task-net serves start/stop/clear/data. Fetch allowed only when **Stopped**.

## Queues

- UI → net: `postNetRequest` (`ConnectWifi`, `ScanWifi`, `ApplyStaticIp`, `UseDhcp`, `StartWebSetup`).
- Net → UI: `postUiText` / scan result `UiMsg`.

## Why

ESP32 Arduino `WiFi.*` is not safe across tasks. Settings NVS writes used to race with lock/copy/unlock patterns. Snapshots + a single commit path keep modules replaceable and OTA/NTP shedding predictable.
