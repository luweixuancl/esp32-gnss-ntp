#!/usr/bin/env python3
"""ct_fetch_bin.py — READ-ONLY clock-trace bin downloader + CSV converter.

Safety contract (v2, after the 2026-09-19 data-loss incident):
  * This tool issues ONLY HTTP GET requests.
  * It NEVER calls /debug/clock/start, /stop or /clear — the strings that
    build those URLs do not exist in this file, and any state-changing
    action must be done by a human, on purpose.
  * Before downloading it ALWAYS checks the buffer (GET /debug/clock):
    downloads happen only when state==STOP and count>0.

Flow:  check buffer -> (if STOP & count>0) fetch CTRB pages -> write .bin
       (optional) -> convert to CSV (same columns as tools/clock_trace_client.py).

Offline mode: --from-bin FILE converts a previously saved CTRB file to CSV
without touching the device at all.

Usage:
  python3 tools/ct_fetch_bin.py --host 192.168.1.24 --pass NTP-9EC4 -o out.csv
  python3 tools/ct_fetch_bin.py --from-bin dump.bin -o out.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
import time
import urllib.error
import urllib.request

MAGIC = b"CTRB"
HDR = struct.Struct("<4sHHIIIIII")  # magic, ver, sampleSize, seqFrom, count, seqNext, seqEnd, dropped, flags
SMPL = struct.Struct("<IIIIiIfffHBBBB")  # 42 bytes, little-endian, packed
assert SMPL.size == 42
FLAG_DONE = 0x1

STATE_NAMES = {0: "ACQ", 1: "LCK", 2: "DEG", 3: "HLD", 4: "UNS"}
CSV_COLS = ["seq", "uptimeMs", "utcEpoch", "ppsCount", "residualMs", "holdoverMs",
            "freqPpm", "tempC", "tempCorrPpm", "qualityMs", "state",
            "ppsFresh", "timeValid", "tempComp", "satellites"]


def http_get(url: str, timeout: float, retries: int = 4) -> bytes:
    last = None
    for i in range(retries):
        try:
            req = urllib.request.Request(url)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read()
        except Exception as e:  # transient Termux/WiFi aborts — retry read-only GET
            last = e
            time.sleep(2 + 2 * i)
    raise RuntimeError(f"GET failed after {retries} tries: {last}")


def check_buffer(host: str, pw: str, timeout: float) -> dict:
    """STEP 1 — mandatory buffer check. Read-only."""
    raw = http_get(f"http://{host}/debug/clock?pass={pw}", timeout)
    info = json.loads(raw.decode("utf-8", "replace"))
    if not info.get("ok"):
        raise RuntimeError(f"device rejected status query: {info}")
    return info


def fetch_bin(host: str, pw: str, timeout: float, limit: int,
              save_bin: str | None) -> tuple[list[dict], dict]:
    """STEP 2 — paginate CTRB pages. Read-only GETs only."""
    if limit > 12000:
        raise SystemExit("limit max is 12000 (device cap)")
    samples: list[dict] = []
    seq = 0
    meta: dict = {}
    page = 0
    while True:
        url = (f"http://{host}/debug/clock/data?pass={pw}"
               f"&format=bin&from={seq}&limit={limit}")
        blob = http_get(url, timeout)
        if blob[:4] != MAGIC:
            raise RuntimeError(f"page {page}: bad magic {blob[:4]!r}")
        magic, ver, ssz, seq_from, cnt, seq_next, seq_end, dropped, flags = HDR.unpack_from(blob, 0)
        if ver != 1 or ssz != SMPL.size:
            raise RuntimeError(f"page {page}: unsupported ver={ver} sampleSize={ssz}")
        if seq_from != seq:
            raise RuntimeError(f"page {page}: device seqFrom={seq_from} != expected {seq}")
        body = blob[HDR.size:HDR.size + cnt * ssz]
        if len(body) < cnt * ssz:
            raise RuntimeError(f"page {page}: truncated body ({len(body)} < {cnt * ssz})")
        for off in range(0, cnt * ssz, ssz):
            (s_seq, up, utc, pps, res, ho, ppm, tc, tcorr, q,
             state, flg, sats, _rsv) = SMPL.unpack_from(body, off)
            samples.append(dict(seq=s_seq, uptimeMs=up, utcEpoch=utc, ppsCount=pps,
                                residualMs=res, holdoverMs=ho, freqPpm=ppm, tempC=tc,
                                tempCorrPpm=tcorr, qualityMs=q, state=state,
                                ppsFresh=flg & 1, timeValid=(flg >> 1) & 1,
                                tempComp=(flg >> 2) & 1, satellites=sats))
        done = bool(flags & FLAG_DONE) or seq_next >= seq_end
        print(f"  page {page}: seq {seq_from}..{seq_next - 1} ({cnt} rows, "
              f"{len(blob)} B, dropped={dropped}, done={int(done)})")
        meta = dict(version=ver, sampleSize=ssz, seqEnd=seq_end, dropped=dropped,
                    firstSeq=samples[0]["seq"] if samples else None,
                    lastSeq=samples[-1]["seq"] if samples else None)
        if save_bin:
            with open(save_bin, "ab") as f:
                f.write(blob)
        page += 1
        if done or cnt == 0:
            break
        seq = seq_next
    return samples, meta


def write_csv(samples: list[dict], meta: dict, out_path: str) -> None:
    with open(out_path, "w", newline="") as f:
        f.write(f"# clock_trace bin2csv rows={len(samples)} seqEnd={meta.get('seqEnd')} "
                f"dropped={meta.get('dropped')}\n")
        w = csv.writer(f)
        w.writerow(CSV_COLS)
        for s in samples:
            w.writerow([s["seq"], s["uptimeMs"], s["utcEpoch"], s["ppsCount"],
                        s["residualMs"], s["holdoverMs"], f"{s['freqPpm']:.4f}",
                        f"{s['tempC']:.2f}", f"{s['tempCorrPpm']:.3f}",
                        s["qualityMs"], s["state"], s["ppsFresh"], s["timeValid"],
                        s["tempComp"], s["satellites"]])


def load_bin_file(path: str) -> list[dict]:
    blob = open(path, "rb").read()
    if blob[:4] != MAGIC:
        raise SystemExit(f"{path}: not a CTRB file")
    _, ver, ssz, seq_from, cnt, seq_next, seq_end, dropped, flags = HDR.unpack_from(blob, 0)
    if ssz != SMPL.size:
        raise SystemExit(f"{path}: sampleSize {ssz} != 42")
    out = []
    for off in range(HDR.size, HDR.size + cnt * ssz, ssz):
        (s_seq, up, utc, pps, res, ho, ppm, tc, tcorr, q,
         state, flg, sats, _rsv) = SMPL.unpack_from(blob, off)
        out.append(dict(seq=s_seq, uptimeMs=up, utcEpoch=utc, ppsCount=pps,
                        residualMs=res, holdoverMs=ho, freqPpm=ppm, tempC=tc,
                        tempCorrPpm=tcorr, qualityMs=q, state=state,
                        ppsFresh=flg & 1, timeValid=(flg >> 1) & 1,
                        tempComp=(flg >> 2) & 1, satellites=sats))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description="READ-ONLY clock-trace bin fetch + CSV convert "
                    "(never start/stop/clear; checks buffer first)")
    ap.add_argument("--host", default="192.168.1.24")
    ap.add_argument("--pass", dest="pw", default="NTP-9EC4",
                    help="device debug pass (same as /cfg)")
    ap.add_argument("-o", "--out", default="clock_trace.csv", help="output CSV path")
    ap.add_argument("--save-bin", default=None,
                    help="also keep the raw CTRB stream in this file")
    ap.add_argument("--limit", type=int, default=8000,
                    help="samples per page (<=12000)")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--from-bin", default=None,
                    help="offline: convert this CTRB file to CSV (no device access)")
    args = ap.parse_args()

    if args.from_bin:
        samples = load_bin_file(args.from_bin)
        write_csv(samples, {"seqEnd": samples[-1]["seq"] + 1 if samples else 0,
                            "dropped": 0}, args.out)
        print(f"wrote {args.out} ({len(samples)} rows) from {args.from_bin}")
        return 0

    # ---- STEP 1: check the buffer BEFORE any download. Read-only. ----
    print(f"[1/2] checking buffer on {args.host} …")
    info = check_buffer(args.host, args.pw, args.timeout)
    state = info.get("state")
    count = int(info.get("count") or 0)
    print(f"      state={state} count={count} capacity={info.get('capacity')} "
          f"psram={info.get('psram')} dropped={info.get('dropped')}")
    if state != "STOP":
        print(f"REFUSED: buffer state is {state!r} — download needs STOP.\n"
              f"  REC : let it finish, then a human must POST /debug/clock/stop\n"
              f"  IDLE: nothing recorded (count=0). This tool will NOT start "
              f"recording by itself.")
        return 2
    if count == 0:
        print("REFUSED: buffer is STOP but count=0 — nothing to download.")
        return 2
    if count < int(info.get("seqNext") or 0):
        print(f"      note: count={count} < seqNext={info.get('seqNext')} "
              f"(ring wrapped; oldest samples overwritten)")

    # ---- STEP 2: download pages + convert. Still read-only. ----
    print(f"[2/2] downloading {count} samples (pages of {args.limit}) …")
    samples, meta = fetch_bin(args.host, args.pw, args.timeout, args.limit,
                              args.save_bin)
    write_csv(samples, meta, args.out)
    states = {}
    for s in samples:
        states[s["state"]] = states.get(s["state"], 0) + 1
    print(f"wrote {args.out}: {len(samples)} rows "
          f"(seq {meta['firstSeq']}..{meta['lastSeq']}, "
          f"seqEnd={meta['seqEnd']}, dropped={meta['dropped']})")
    print("state mix:", {STATE_NAMES.get(k, k): v for k, v in sorted(states.items())})
    return 0


if __name__ == "__main__":
    sys.exit(main())
