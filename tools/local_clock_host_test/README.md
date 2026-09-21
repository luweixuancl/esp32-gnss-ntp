# LocalClock host test (no hardware)

Regression test for the PPS-restart deadlock (2026-09-21 field bug): after a
GPS module power gap, the PPS outlier filter compared every new edge against
the stale pre-gap baseline and rejected forever -> stuck in ACQ, NTP refused.

Run (any host with g++; stdlib only):

    g++ -std=c++17 -Wall \
        -I stubs -I ../../include \
        ../../src/local_clock.cpp test_local_clock.cpp -o t && ./t

stubs/ must contain Arduino.h, esp_timer.h, settings.h (copy from a prior
sandbox or regenerate; keep them in sync with include/ when interfaces move).
