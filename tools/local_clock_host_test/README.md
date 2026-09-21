# LocalClock host test (no hardware)

Regression for the PPS-restart deadlock (2026-09-21 field): after a GPS module
power gap, the outlier filter compared every new edge against the stale
pre-gap baseline and rejected forever → stuck in ACQ, NTP refused.

Stubs in `stubs/` (`Arduino.h`, `esp_timer.h`, `settings.h`) are part of this
tree. Keep them in sync with `include/` when interfaces move.

```bash
cd tools/local_clock_host_test
g++ -std=c++17 -Wall \
    -I stubs -I ../../include \
    ../../src/local_clock.cpp test_local_clock.cpp -o t && ./t
```

Scenarios: clean lock; 360 s gap past holdover then resume; 20 ms glitch
absorbed; resume during HLD; 2 s hole while Locked drops phase; delta>1
resume after 360 s.
