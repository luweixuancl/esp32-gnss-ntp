# Module boundaries (FreeRTOS)

Tight cohesion / loose coupling rules for the GNSS NTP firmware.

## Tasks

| Task | Owns | Must not |
|------|------|----------|
| **task-time** | GPS, local clock, NTP reply path; **HistoryRecorder::push** (1 Hz) | `WiFi.*`, NVS writes, OLED, WebServer |
| **task-net** | `WifiManager`, HTTP/OTA, SoftAP, scans; **HistoryRecorder export** | OLED drawing, long GPS work |
| **task-ui** | OLED + encoder | `WiFi.*`, direct `gStore.save` |

## Shared state

- **`WifiLinkSnapshot`** (`wifi_types.h`): written only by `WifiManager::refreshLinkSnapshot()` on task-net; any task may `linkSnapshot()`.
- **`AppSettings`**: read via `settingsCopy()`; persist via `settingsCommit()` only. Factory reset before `ipcInit` may call `gStore.save` directly.
- **OTA**: observe `ipcOtaBusy()` / `ipcOtaPhase()`; do not call into `OtaService` from time/ui.
- **History**: PSRAM ring owned by `HistoryRecorder` (S3 only); time writes, net reads via `summary()` / export cursor — see [psram_history_design.md](psram_history_design.md).

## Queues

- UI → net: `postNetRequest` (`ConnectWifi`, `ScanWifi`, `ApplyStaticIp`, `UseDhcp`, `StartWebSetup`).
- Net → UI: `postUiText` / scan result `UiMsg`.

## Why

ESP32 Arduino `WiFi.*` is not safe across tasks. Settings NVS writes used to race with lock/copy/unlock patterns. Snapshots + a single commit path keep modules replaceable and OTA/NTP shedding predictable.
