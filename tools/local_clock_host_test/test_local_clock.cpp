// Host test: LocalClock PPS-restart deadlock regression (2026-09-21 field).
// After a GPS module power gap the outlier filter compared every new edge
// against the stale pre-gap baseline and rejected forever -> stuck in ACQ.
#include "local_clock.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

uint64_t g_fakeTimeUs = 0;

static const uint32_t BASE = 4100000000u;
static const uint16_t HOLD = 300;
using P = AnomalyPolicy;

static void lockFromReset(LocalClock& c, uint32_t& pps, uint32_t epoch0) {
  c.reset();
  pps = 0;
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    c.onPpsEdge(g_fakeTimeUs, ++pps);
    c.onNmeaCommit(epoch0 + static_cast<uint32_t>(s), pps - 1, P::HoldoverLong, HOLD);
  }
  assert(c.state() == ClockState::Locked);
}

int main() {
  // --- Scenario 1: clean bootstrap -> Locked ---
  LocalClock c;
  uint32_t pps = 0;
  g_fakeTimeUs = 0;
  lockFromReset(c, pps, BASE);
  std::printf("scenario1 lock: OK (state=%s)\n", clockStateLabel(c.state()));

  // --- Scenario 2: 360 s PPS gap (module power pull past holdover), then resume ---
  for (int s = 0; s < 360; ++s) {
    g_fakeTimeUs += 1000000ull;
    c.tick(false, false, P::HoldoverLong, HOLD);
  }
  assert(c.state() == ClockState::Unsynced);

  uint32_t epoch = BASE + 8 + 360;
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    c.onPpsEdge(g_fakeTimeUs, ++pps);
    c.tick(true, true, P::HoldoverLong, HOLD);
    c.onNmeaCommit(epoch + static_cast<uint32_t>(s), pps - 1, P::HoldoverLong, HOLD);
    std::printf("resume +%ds: state=%s anchor=%d stable=%d\n", s,
                clockStateLabel(c.state()), c.hasAnchor() ? 1 : 0,
                c.ppsStable() ? 1 : 0);
    if (s >= 1) {
      assert(c.hasAnchor());
    }
    if (s >= 4) {
      assert(c.state() == ClockState::Locked);
    }
  }

  // --- Scenario 3: sub-second glitch mid-lock is still rejected ---
  g_fakeTimeUs += 1000000ull;
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  c.onNmeaCommit(epoch + 8, pps - 1, P::HoldoverLong, HOLD);
  assert(c.state() == ClockState::Locked);
  g_fakeTimeUs += 20000ull;
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  g_fakeTimeUs += 980000ull;
  c.onPpsEdge(g_fakeTimeUs, ++pps);
  c.onNmeaCommit(epoch + 9, pps - 1, P::HoldoverLong, HOLD);
  assert(c.state() == ClockState::Locked);
  std::printf("scenario3 glitch absorbed: OK\n");

  // --- Scenario 4: resume during HLD (30 s gap, still inside 300 s window) ---
  LocalClock h;
  g_fakeTimeUs = 0;
  uint32_t hpps = 0;
  lockFromReset(h, hpps, BASE);
  for (int s = 0; s < 30; ++s) {
    g_fakeTimeUs += 1000000ull;
    h.tick(false, false, P::HoldoverLong, HOLD);
  }
  assert(h.state() == ClockState::Holdover);
  uint32_t hepoch = BASE + 8 + 30;
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    h.onPpsEdge(g_fakeTimeUs, ++hpps);
    h.tick(true, true, P::HoldoverLong, HOLD);
    h.onNmeaCommit(hepoch + static_cast<uint32_t>(s), hpps - 1, P::HoldoverLong, HOLD);
    if (s >= 4) {
      assert(h.state() == ClockState::Locked);
    }
  }
  std::printf("scenario4 HLD-resume re-lock: OK\n");

  // --- Scenario 5: 2 s hole while Locked must drop phase, not UTC+=1 ---
  LocalClock m;
  g_fakeTimeUs = 0;
  uint32_t mpps = 0;
  lockFromReset(m, mpps, BASE);
  const uint32_t utcBefore = BASE + 7;
  g_fakeTimeUs += 2000000ull;  // missed one 1 Hz pulse
  m.onPpsEdge(g_fakeTimeUs, ++mpps);
  assert(m.state() == ClockState::Unsynced || m.state() == ClockState::Acquiring);
  assert(!m.hasAnchor());
  uint32_t mepoch = utcBefore + 2;
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    m.onPpsEdge(g_fakeTimeUs, ++mpps);
    m.tick(true, true, P::HoldoverLong, HOLD);
    m.onNmeaCommit(mepoch + static_cast<uint32_t>(s), mpps - 1, P::HoldoverLong, HOLD);
    if (s >= 4) {
      assert(m.state() == ClockState::Locked);
    }
  }
  std::printf("scenario5 2s-hole unsync then re-lock: OK\n");

  // --- Scenario 6: first resume edge has delta>1 (count jump) after 360 s ---
  LocalClock j;
  g_fakeTimeUs = 0;
  uint32_t jpps = 0;
  lockFromReset(j, jpps, BASE);
  for (int s = 0; s < 360; ++s) {
    g_fakeTimeUs += 1000000ull;
    j.tick(false, false, P::HoldoverLong, HOLD);
  }
  assert(j.state() == ClockState::Unsynced);
  uint32_t jepoch = BASE + 8 + 360;
  jpps += 3;  // hardware/software count jumped
  for (int s = 0; s < 8; ++s) {
    g_fakeTimeUs += 1000000ull;
    j.onPpsEdge(g_fakeTimeUs, ++jpps);
    j.tick(true, true, P::HoldoverLong, HOLD);
    j.onNmeaCommit(jepoch + static_cast<uint32_t>(s), jpps - 1, P::HoldoverLong, HOLD);
    if (s >= 1) {
      assert(j.hasAnchor());
    }
    if (s >= 4) {
      assert(j.state() == ClockState::Locked);
    }
  }
  std::printf("scenario6 delta>1 resume: OK\n");

  std::printf("ALL PASS\n");
  return 0;
}
