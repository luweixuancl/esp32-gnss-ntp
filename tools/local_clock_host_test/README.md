# LocalClock host test (no hardware)

Regression for the PPS-restart deadlock (2026-09-21 field bug): after a GPS
module power gap, the outlier filter compared every new edge against the stale
pre-gap baseline and rejected forever → stuck in ACQ, NTP refused.

`#include "settings.h"` resolves to `include/settings.h` (quote-include of the
header's own directory). Stubs only need to satisfy that file's Arduino-side
deps (`Arduino.h`, `IPAddress.h`, `Preferences.h`, `esp_timer.h`).

```bash
cd tools/local_clock_host_test
g++ -std=c++17 -Wall -Wextra \
    -I stubs -I ../../include \
    ../../src/local_clock.cpp test_local_clock.cpp -o t && ./t
```

Expect `ALL PASS` (bootstrap lock, 360 s gap → re-anchor/+3 s LCK, glitch absorb).
