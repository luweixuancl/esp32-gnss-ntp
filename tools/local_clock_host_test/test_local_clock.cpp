// Host test: LocalClock PPS-restart deadlock regression.
// Scenario 2 reproduces the field bug: after a GPS module power gap (>5 ms),
// the outlier filter compares every new edge against the stale pre-gap
// baseline and rejects forever -> ppsStable_ never rises -> stuck in ACQ.
#include "local_clock.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

uint64_t g_fakeTimeUs = 0;

static const uint32_t BASE = 4100000000u;
static const uint16_t HOLD = 300;
using P = AnomalyPolicy;

int main() {
  // --- Scenario 1: clean bootstrap -> Locked (sanity, must keep passing) ---
  LocalClock c;
  c.reset();
  uint32_t pps = 0;
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs = static_cast<uint64_t>(s) * 1000000ull;
    c.onPpsEdge(g_fakeTimeUs, ++pps);
    c.onNmeaCommit(BASE + s, pps - 1, P::HoldoverLong, HOLD);
  }
  assert(c.state() == ClockState::Locked);
  std::printf("scenario1 lock: OK (state=%s)\n", clockStateLabel(c.state()));

  // --- Scenario 2: 360 s PPS gap (module power pull), then resume ---
  // Mirror firmware: holdover policy keeps HLD 300 s then UNS (anchor dropped).
  for (int s = 0; s < 360; ++s) {
    g_fakeTimeUs += 1000000ull;
    c.tick(false, false, P::HoldoverLong, HOLD);
  }
  assert(c.state() == ClockState::Unsynced);

  uint32_t epoch = BASE + 8 + 360;  // NMEA resumes with module time
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    c.onPpsEdge(g_fakeTimeUs, ++pps);
    c.tick(true, true, P::HoldoverLong, HOLD);
    c.onNmeaCommit(epoch + s, pps - 1, P::HoldoverLong, HOLD);
    std::printf("resume +%ds: state=%s anchor=%d stable=%d\n", s,
                clockStateLabel(c.state()), c.hasAnchor() ? 1 : 0,
                c.ppsStable() ? 1 : 0);
    if (s >= 1) {
      assert(c.hasAnchor());  // anchor must re-set immediately after resume
    }
    if (s >= 4) {
      assert(c.state() == ClockState::Locked);  // re-lock within ~CLK_RELOCK_COUNT+2 s
    }
  }

  // --- Scenario 3: sub-second glitch mid-lock is still rejected (no regress) ---
  g_fakeTimeUs += 1000000ull;
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  c.onNmeaCommit(epoch + 8, pps - 1, P::HoldoverLong, HOLD);
  assert(c.state() == ClockState::Locked);
  g_fakeTimeUs += 20000ull;  // +20 ms double-edge glitch
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  g_fakeTimeUs += 980000ull;  // next real edge completes the second
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  c.onNmeaCommit(epoch + 9, pps - 1, P::HoldoverLong, HOLD);
  assert(c.state() == ClockState::Locked);
  std::printf("scenario3 glitch absorbed: OK\n");

  std::printf("ALL PASS\n");
  return 0;
}
