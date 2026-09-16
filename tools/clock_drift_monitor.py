#!/usr/bin/env python3
"""Long-duration clock drift monitor: 1 Hz /status + periodic NTP checkpoints.

Read-only against the device (HTTP GET + NTP UDP queries). Runs until the
--until local time, then prints a short summary and exits. Designed for
multi-hour unattended runs: per-request failures are logged as gaps and the
loop keeps going; CSV rows are flushed every write so a crash loses nothing.

Outputs:
  --csv     status samples (1 Hz): clock/gps fields + wall-mono skew column
            (skew jumps reveal the phone's own NTP syncs — exclude those
            windows when reading device-side drift)
  --ntp-csv DEV + reference (Aliyun) offset checkpoints every --ntp-every s
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import socket
import struct
import sys
import time
import urllib.request

EPOCH_DELTA = 2208988800

STATUS_FIELDS = ["state", "holdoverMs", "residualMs", "freqPpm",
                 "tempC", "tempRefC", "tempCorrPpm", "tempComp",
                 "qualityMs", "ppsCount", "ppsFresh", "satellites",
                 "fix", "rssi"]


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
                    prec=precision, off=offset_ms, rtt=rtt_ms, err="")
    except Exception as exc:
        return dict(li=-1, st=-1, ref="", disp=0.0, prec=0, off=0.0,
                    rtt=0.0, err=str(exc)[:60])


def fetch_status(host: str, timeout: float = 4.0):
    try:
        with urllib.request.urlopen("http://" + host + "/status",
                                    timeout=timeout) as resp:
            return json_loads(resp.read())
    except Exception:
        return None


def json_loads(b: bytes) -> dict:
    import json
    return json.loads(b)


def iso(ts: float) -> str:
    return dt.datetime.fromtimestamp(ts).isoformat(timespec="seconds")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="10.81.127.143")
    ap.add_argument("--aliyun", default="ntp.aliyun.com")
    ap.add_argument("--until", required=True,
                    help="local time 'YYYY-MM-DD HH:MM' to stop")
    ap.add_argument("--csv", required=True)
    ap.add_argument("--ntp-csv", required=True)
    ap.add_argument("--ntp-every", type=int, default=300)
    ap.add_argument("--progress-every", type=int, default=600)
    args = ap.parse_args()

    until = dt.datetime.strptime(args.until, "%Y-%m-%d %H:%M").timestamp()
    if until <= time.time():
        print("--until is in the past", flush=True)
        return 2

    first = fetch_status(args.host)
    if first is None:
        print("device unreachable: " + args.host, flush=True)
        return 2
    c0 = first.get("clock", {})
    print("start %s until %s host=%s state=%s sat=%s" %
          (iso(time.time()), iso(until), args.host, c0.get("state"),
           first.get("gps", {}).get("satellites")), flush=True)

    sf = open(args.csv, "w", newline="")
    sw = csv.writer(sf)
    sw.writerow(["t_iso", "t_mono", "skew", "gap"] + STATUS_FIELDS)
    nf = open(args.ntp_csv, "w", newline="")
    nw = csv.writer(nf)
    nw.writerow(["t_iso", "target", "li", "st", "refid", "disp",
                 "prec", "off_ms", "rtt_ms", "err"])

    t0 = time.monotonic()
    n = ok = gaps = ntp_runs = 0
    last_progress = t0
    last_ntp = 0.0
    ntp_next = 0  # checkpoint immediately at start

    while True:
        target = t0 + n
        delay = target - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        n += 1
        now_wall = time.time()
        if now_wall >= until:
            break

        j = fetch_status(args.host)
        gap = 0
        if j is None:
            gaps += 1
            gap = 1
            j = {}
        else:
            ok += 1
        c = j.get("clock", {})
        g = j.get("gps", {})
        row = [iso(now_wall), "%.3f" % (time.monotonic() - t0),
               "%.3f" % (now_wall - time.monotonic()), gap]
        for f in STATUS_FIELDS:
            src = g if f in ("qualityMs", "ppsCount", "ppsFresh",
                             "satellites", "fix") else c
            v = src.get(f, j.get(f))
            row.append("" if v is None else v)
        sw.writerow(row)
        if n % 5 == 0:
            sf.flush()
            nf.flush()

        if now_wall - last_ntp >= args.ntp_every or ntp_next == 0:
            last_ntp = now_wall
            ntp_next = 1
            ntp_runs += 1
            for tag, host in (("DEV", args.host), ("ALI", args.aliyun)):
                q = ntp_query(host)
                nw.writerow([iso(time.time()), tag, q["li"], q["st"],
                             q["ref"], "%.6f" % q["disp"], q["prec"],
                             "%.3f" % q["off"], "%.1f" % q["rtt"], q["err"]])
            nf.flush()

        if time.monotonic() - last_progress >= args.progress_every:
            last_progress = time.monotonic()
            c = j.get("clock", {})
            print("progress %s n=%d ok=%d gaps=%d state=%s freqPpm=%s "
                  "tempC=%s corr=%s" %
                  (iso(time.time()), n, ok, gaps, c.get("state"),
                   c.get("freqPpm"), c.get("tempC"), c.get("tempCorrPpm")),
                  flush=True)

    sf.close()
    nf.close()
    span = time.monotonic() - t0
    print("done: span=%.0fs n=%d ok=%d gaps=%d ntp_runs=%d "
          "coverage=%.1f%%" % (span, n, ok, gaps, ntp_runs,
                               100.0 * ok / n if n else 0), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
