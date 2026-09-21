#!/usr/bin/env python3
"""GPS failover monitor: 1 Hz /status polling + NTP checkpoints.

Tracks the full failure chain LCK -> HLD -> (holdoverSec) -> UNS -> recovery.
Total window auto-covers pull delay + holdover + recovery so the UNS
transition is never missed (lesson from the 2026-09-16 power-pull test).

Stdlib only. Read-only against the device (HTTP GET + NTP UDP).
"""
from __future__ import annotations

import argparse
import csv
import json
import socket
import struct
import sys
import time
import urllib.request

EPOCH_DELTA = 2208988800
STATUS_PATH = "/status"
CHECKPOINT_FRACTIONS = (0.10, 0.40, 0.80)  # of holdoverSec, while in HLD


def ntp_query(host: str, timeout: float = 2.0) -> dict:
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        t1 = time.time()
        sock.sendto(b"\x23" + bytes(47), (host, 123))
        data, _ = sock.recvfrom(1024)
        t4 = time.time()
        sock.close()
        li = data[0] >> 6
        stratum = data[1]
        precision = int.from_bytes(data[3:4], "big", signed=True)
        root_disp = struct.unpack("!I", data[8:12])[0] / 65536.0
        refid = data[12:16].decode("latin1").rstrip("\x00 ")
        sec, frac = struct.unpack("!II", data[40:48])
        t3 = (sec - EPOCH_DELTA) + frac / 2**32
        offset_ms = ((t3 - t1) + (t3 - t4)) / 2 * 1000.0
        rtt_ms = (t4 - t1) * 1000.0
        return dict(li=li, st=stratum, ref=refid, disp=root_disp,
                    prec=precision, off=offset_ms, rtt=rtt_ms, err=None)
    except Exception as exc:  # timeout / unreachable
        return dict(li=None, st=None, ref="", disp=0.0, prec=None,
                    off=0.0, rtt=0.0, err=str(exc)[:48])


def fmt_ntp(tag: str, q: dict) -> str:
    if q["err"]:
        return "[%-9s] ERR/timeout %s" % (tag, q["err"])
    return ("[%-9s] li=%d st=%-2d ref=%-4s disp=%.3fs off=%+.2fms rtt=%.1fms"
            % (tag, q["li"], q["st"], q["ref"], q["disp"], q["off"], q["rtt"]))


def fetch_status(host: str, timeout: float = 4.0):
    try:
        with urllib.request.urlopen("http://" + host + STATUS_PATH,
                                    timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="10.81.127.143")
    ap.add_argument("--aliyun", default="ntp.aliyun.com")
    ap.add_argument("--mode", choices=("baseline", "failover"),
                    default="failover")
    ap.add_argument("--pull-delay", type=int, default=60,
                    help="seconds reserved for the physical pull (failover)")
    ap.add_argument("--recovery", type=int, default=240,
                    help="seconds to watch recovery after UNS")
    ap.add_argument("--max-window", type=int, default=14400,
                    help="hard cap on total monitoring seconds (default 4h for long Hold)")
    ap.add_argument("--csv", default="", help="optional per-second CSV path")
    args = ap.parse_args()

    first = fetch_status(args.host)
    if first is None:
        print("device unreachable: http://%s%s" % (args.host, STATUS_PATH))
        return 2
    holdover_sec = first.get("holdoverSec") or 300
    window = args.pull_delay + holdover_sec + args.recovery
    if args.mode == "baseline":
        window = min(window, 60)
    window = min(window, args.max_window)
    print("mode=%s holdoverSec=%u window=%us host=%s"
          % (args.mode, holdover_sec, window, args.host))

    csv_file = None
    csv_writer = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["t", "state", "holdoverMs", "qualityMs",
                             "residualMs", "freqPpm", "tempC", "tempRefC",
                             "tempCorrPpm", "ppsCount", "sat", "fix",
                             "rssi", "holdoverSec"])

    def finish(code: int, transitions) -> int:
        if csv_file:
            csv_file.close()
        print("transitions:", transitions)
        return code

    t0 = time.monotonic()
    n = 0
    last_state = None
    transitions = []
    t_hld = None
    t_uns = None
    t_lck_after_uns = None
    ck_done = [False] * len(CHECKPOINT_FRACTIONS)
    uns_done = False
    rec_done = False
    baseline_done = 0

    while time.monotonic() - t0 < window:
        target = t0 + n
        delay = target - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        n += 1
        j = fetch_status(args.host)
        if j is None:
            continue
        c = j.get("clock", {})
        g = j.get("gps", {})
        now = time.monotonic() - t0
        state = c.get("state")

        if csv_writer:
            csv_writer.writerow([
                round(now, 2), state, c.get("holdoverMs"), g.get("qualityMs"),
                c.get("residualMs"), c.get("freqPpm"), c.get("tempC"),
                c.get("tempRefC"), c.get("tempCorrPpm"), g.get("ppsCount"),
                g.get("satellites"), g.get("fix"), j.get("rssi"),
                j.get("holdoverSec")])

        if state != last_state:
            transitions.append((round(now, 2), state))
            print("[%7.2fs] state -> %s (ppsCount=%s fix=%s sat=%s "
                  "holdoverMs=%s q=%s)"
                  % (now, state, g.get("ppsCount"), g.get("fix"),
                     g.get("satellites"), c.get("holdoverMs"),
                     g.get("qualityMs")))
            last_state = state
            if state == "HLD" and t_hld is None:
                t_hld = time.monotonic()
            if state == "UNS" and t_uns is None:
                t_uns = time.monotonic()
            if state == "LCK" and t_uns is not None:
                t_lck_after_uns = time.monotonic()

        if args.mode == "baseline":
            if baseline_done < 2 and now >= (10, 50)[baseline_done]:
                baseline_done += 1
                print(fmt_ntp("DEV", ntp_query(args.host)))
                print(fmt_ntp("ALI", ntp_query(args.aliyun)))
            if baseline_done >= 2:
                return finish(0, transitions)
            continue

        if t_hld is not None and t_uns is None:
            age = time.monotonic() - t_hld
            if n % 15 == 0:
                print("[%7.2fs] HLD age=%5.1fs holdoverMs=%6.1f q=%s "
                      "residual=%s ppsCount=%s sat=%s tempRefC=%.2f"
                      % (now, age, c.get("holdoverMs"), g.get("qualityMs"),
                         c.get("residualMs"), g.get("ppsCount"),
                         g.get("satellites"), c.get("tempRefC") or 0))
            for i, frac in enumerate(CHECKPOINT_FRACTIONS):
                thr = holdover_sec * frac
                if not ck_done[i] and age >= thr:
                    ck_done[i] = True
                    print(fmt_ntp("HLD+%ds" % round(thr),
                                  ntp_query(args.host)))
                    print(fmt_ntp("ALI", ntp_query(args.aliyun)))
        if t_uns is not None and not uns_done and \
                time.monotonic() - t_uns >= 2:
            uns_done = True
            print(fmt_ntp("UNS+2s", ntp_query(args.host)))
            print(fmt_ntp("ALI", ntp_query(args.aliyun)))
            print(">>> replug now (watch recovery, %us window)"
                  % args.recovery)
        if t_uns is not None and t_lck_after_uns is not None and not rec_done:
            if time.monotonic() - t_lck_after_uns >= 3:
                rec_done = True
                print(fmt_ntp("LCK+3s", ntp_query(args.host)))
                print(fmt_ntp("ALI", ntp_query(args.aliyun)))
                print("recovered %.1fs after UNS"
                      % (t_lck_after_uns - t_uns))
                return finish(0, transitions)

    print("window expired (%us)" % window)
    return finish(1, transitions)


if __name__ == "__main__":
    sys.exit(main())
