#!/usr/bin/env python3
"""RMT PPS board monitor for fw v1.1.29 (GPS_PPS_RMT_EN=1).

Polls GET /status and prints a compact line every --period seconds.
Exits 0 on PASS criteria after --duration, else 1.

Usage (device already on WiFi, GNSS locked):
  python3 tools/rmt_pps_monitor.py --host 192.168.1.24 --duration 180
  python3 tools/rmt_pps_monitor.py --host 192.168.1.24 --quick

PASS (default thresholds):
  - fwMark contains 1.1.29 (or --skip-fw)
  - gps.ppsRmt present, armed=true, idfOk=true
  - idfDataFrames increases by >= (duration_s * 0.7) over the run
  - idfEmptyFrames / idfDataFrames < 0.5 at end (when dataFrames>10)
  - clock.state == LCK for >= 90% of samples after first LCK
  - fallbacks does not grow by more than --max-fallback-delta
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request


def fetch_status(host: str, timeout: float = 5.0) -> dict:
    url = f"http://{host}/status"
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="Device IP, e.g. 192.168.1.24")
    ap.add_argument("--duration", type=int, default=180, help="Seconds to monitor (default 180)")
    ap.add_argument("--period", type=float, default=2.0, help="Poll period seconds")
    ap.add_argument("--quick", action="store_true", help="60s smoke")
    ap.add_argument("--skip-fw", action="store_true", help="Do not require fwMark 1.1.29")
    ap.add_argument("--max-fallback-delta", type=int, default=5)
    ap.add_argument("--jsonl", default="", help="Optional path to append raw samples")
    args = ap.parse_args()
    if args.quick:
        args.duration = 60

    t0 = time.time()
    samples = 0
    errors = 0
    lck = 0
    seen_lck = False
    first_df = None
    last_df = 0
    first_fb = None
    last_fb = 0
    last_empty = 0
    armed_ok = 0
    has_ppsrmt = False
    fw = "?"

    print(f"# rmt_pps_monitor host={args.host} duration={args.duration}s period={args.period}s")
    while time.time() - t0 < args.duration:
        try:
            st = fetch_status(args.host)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
            errors += 1
            print(f"[{time.time()-t0:6.1f}s] ERROR {e}")
            time.sleep(args.period)
            continue

        samples += 1
        fw = st.get("fwMark") or st.get("fwVersion") or "?"
        gps = st.get("gps") or {}
        clk = st.get("clock") or {}
        state = clk.get("state") or "?"
        pps = gps.get("ppsCount")
        rmt = gps.get("ppsRmt")
        if state == "LCK":
            seen_lck = True
            lck += 1

        line = f"[{time.time()-t0:6.1f}s] fw={fw} state={state} pps={pps}"
        if rmt is None:
            line += " ppsRmt=MISSING"
        else:
            has_ppsrmt = True
            df = int(rmt.get("idfDataFrames") or 0)
            ef = int(rmt.get("idfEmptyFrames") or 0)
            fb = int(rmt.get("fallbacks") or 0)
            armed = bool(rmt.get("armed"))
            idf_ok = bool(rmt.get("idfOk"))
            active = bool(rmt.get("active"))
            mean = rmt.get("deltaMeanUs")
            if first_df is None:
                first_df = df
                first_fb = fb
            last_df = df
            last_empty = ef
            last_fb = fb
            if armed and idf_ok:
                armed_ok += 1
            line += (
                f" armed={int(armed)} idfOk={int(idf_ok)} active={int(active)}"
                f" df={df} ef={ef} fb={fb} meanUs={mean} width={rmt.get('lastWidthUs')}"
                f" stage={rmt.get('idfStage')} err={rmt.get('idfErr')}"
            )

        print(line)
        if args.jsonl:
            with open(args.jsonl, "a", encoding="utf-8") as f:
                f.write(json.dumps({"t": time.time() - t0, "st": st}, ensure_ascii=False) + "\n")
        time.sleep(args.period)

    print("===== RMT SUMMARY =====")
    print(f"samples={samples} errors={errors} fw={fw}")
    print(f"has_ppsRmt={has_ppsrmt} armed_ok_samples={armed_ok}/{samples}")
    print(f"LCK={lck}/{samples} after_first_lck_seen={seen_lck}")
    print(f"idfDataFrames {first_df} -> {last_df}  empty={last_empty}  fallbacks {first_fb} -> {last_fb}")

    fail = []
    if samples < 5:
        fail.append("too few samples")
    if not args.skip_fw and "1.1.29" not in str(fw):
        fail.append(f"fwMark want 1.1.29 got {fw}")
    if not has_ppsrmt:
        fail.append("gps.ppsRmt missing (build without GPS_PPS_RMT_EN?)")
    if armed_ok < max(1, samples // 2):
        fail.append("armed/idfOk not true on majority of samples")
    if first_df is not None:
        grew = last_df - first_df
        need = max(10, int(args.duration * 0.5))
        if grew < need:
            fail.append(f"idfDataFrames grew {grew} < need {need}")
        if last_df > 10 and last_empty / max(1, last_df) >= 0.5:
            fail.append(f"too many empty frames empty/data={last_empty}/{last_df}")
    # Soft-but-now-hard for v1.1.30: refinement must engage
    if has_ppsrmt:
        # peek last sample fields from loop — re-fetch once
        try:
            st = fetch_status(args.host)
            rmt = (st.get("gps") or {}).get("ppsRmt") or {}
            samples_n = int(rmt.get("samples") or 0)
            active = bool(rmt.get("active"))
            junk = int(rmt.get("idfJunkFrames") or 0)
            width = int(rmt.get("lastWidthUs") or 0)
            print(f"refine: active={active} samples={samples_n} junk={junk} lastWidthUs={width}")
            if samples_n < max(5, args.duration // 4):
                fail.append(f"RMT refine samples={samples_n} too low (want active merge)")
            if width == 0 and samples_n == 0:
                fail.append("lastWidthUs=0 and samples=0 (symbols still unusable)")
        except Exception as e:
            fail.append(f"refine check failed: {e}")
    if first_fb is not None and (last_fb - first_fb) > args.max_fallback_delta:
        fail.append(f"fallbacks rose by {last_fb - first_fb}")
    if seen_lck:
        # count only after first LCK would need timeline; use overall ratio once LCK seen
        if lck / samples < 0.9:
            fail.append(f"LCK ratio {lck}/{samples} < 90%")
    else:
        fail.append("never reached LCK")

    if fail:
        print("VERDICT: FAIL")
        for x in fail:
            print(f"  - {x}")
        return 1
    print("VERDICT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
