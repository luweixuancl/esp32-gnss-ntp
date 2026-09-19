#!/usr/bin/env python3
"""Clock-trace client: start/stop/status/fetch/clear against /debug/clock*.

Fetch is only allowed after stop (HTTP 409 while recording).
Default transport is binary pages (CTRB) — fast, Content-Length, NTP shed on device.
CSV is produced locally after download.

Examples:
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 start
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 stop
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 fetch -o out.csv
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 \\
      capture --seconds 3600 -o hour.csv
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# Must match include/clock_trace.h ClockTraceSample / ClockTraceBinHeader.
HDR_FMT = "<4sHHIIIIII"  # magic, ver, sampleSize, seqFrom, count, seqNext, seqEnd, dropped, flags
HDR_SIZE = struct.calcsize(HDR_FMT)  # 32
SAMPLE_FMT = "<IIIIiIfffHBBBB"  # 42 bytes
SAMPLE_SIZE = struct.calcsize(SAMPLE_FMT)
assert SAMPLE_SIZE == 42
assert HDR_SIZE == 32

CSV_HEADER = (
    "seq,uptimeMs,utcEpoch,ppsCount,residualMs,holdoverMs,freqPpm,tempC,tempCorrPpm,"
    "qualityMs,state,ppsFresh,timeValid,tempComp,satellites\n"
)


def _url(host: str, path: str, password: str, extra: dict | None = None) -> str:
    q: dict[str, str] = {}
    if password:
        q["pass"] = password
    if extra:
        q.update({k: str(v) for k, v in extra.items()})
    base = f"http://{host}{path}"
    return f"{base}?{urllib.parse.urlencode(q)}" if q else base


def _req(method: str, url: str, timeout: float = 120.0) -> tuple[int, bytes]:
    r = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.getcode(), resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def cmd_json(host: str, password: str, method: str, path: str) -> int:
    code, body = _req(method, _url(host, path, password), timeout=30.0)
    text = body.decode("utf-8", errors="replace")
    try:
        data = json.loads(text)
        print(json.dumps(data, indent=2, ensure_ascii=False))
    except json.JSONDecodeError:
        print(text, end="" if text.endswith("\n") else "\n")
    return 0 if 200 <= code < 300 else 1


def sample_to_csv_row(s: tuple) -> str:
    (seq, uptime_ms, utc_epoch, pps, residual, holdover, freq, temp, tcorr, quality, state, flags, sats, _res) = s
    return (
        f"{seq},{uptime_ms},{utc_epoch},{pps},{residual},{holdover},"
        f"{freq:.4f},{temp:.2f},{tcorr:.3f},{quality},{state},"
        f"{1 if flags & 1 else 0},{1 if flags & 2 else 0},{1 if flags & 4 else 0},{sats}\n"
    )


def fetch_bin_page(host: str, password: str, from_seq: int, limit: int) -> tuple[dict, list[tuple]]:
    url = _url(
        host,
        "/debug/clock/data",
        password,
        {"from": from_seq, "limit": limit},
    )
    code, body = _req("GET", url, timeout=180.0)
    if code == 204:
        return {"done": True, "seqNext": from_seq, "seqEnd": from_seq, "count": 0}, []
    if code != 200:
        raise RuntimeError(f"HTTP {code}: {body[:200]!r}")
    if len(body) < HDR_SIZE:
        raise RuntimeError(f"short response ({len(body)} B)")
    magic, ver, sample_size, seq_from, count, seq_next, seq_end, dropped, flags = struct.unpack_from(
        HDR_FMT, body, 0
    )
    if magic != b"CTRB":
        raise RuntimeError(f"bad magic {magic!r} (need CTRB binary; is firmware too old?)")
    if ver != 1 or sample_size != SAMPLE_SIZE:
        raise RuntimeError(f"unsupported ver/size {ver}/{sample_size}")
    need = HDR_SIZE + count * SAMPLE_SIZE
    if len(body) < need:
        raise RuntimeError(f"truncated body got={len(body)} need={need}")
    samples = []
    off = HDR_SIZE
    for _ in range(count):
        samples.append(struct.unpack_from(SAMPLE_FMT, body, off))
        off += SAMPLE_SIZE
    meta = {
        "seqFrom": seq_from,
        "count": count,
        "seqNext": seq_next,
        "seqEnd": seq_end,
        "dropped": dropped,
        "done": bool(flags & 1) or seq_next >= seq_end,
        "bytes": len(body),
    }
    return meta, samples


def cmd_fetch(host: str, password: str, out_path: str, limit: int) -> int:
    st_code, st_body = _req("GET", _url(host, "/debug/clock", password), timeout=30.0)
    if st_code != 200:
        print(st_body.decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    st = json.loads(st_body.decode("utf-8"))
    if st.get("state") != "STOP":
        print(f"refuse fetch: state={st.get('state')} (stop first)", file=sys.stderr)
        return 2
    total = int(st.get("count") or 0)
    if total == 0:
        print("empty trace", file=sys.stderr)
        open(out_path, "w", encoding="utf-8").write(
            f"# clock_trace empty fw={st.get('fw','')}\n{CSV_HEADER}"
        )
        return 0

    seq_first = int(st.get("seqFirst") or 0)
    seq_end = int(st.get("seqNext") or 0)
    from_seq = seq_first
    rows = 0
    t0 = time.time()
    with open(out_path, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(
            f"# clock_trace fw={st.get('fw','')} count={total} dropped={st.get('dropped',0)} "
            f"seqFirst={seq_first} seqNext={seq_end} psram={1 if st.get('psram') else 0} "
            f"cap={st.get('capacity',0)}\n"
        )
        fp.write(CSV_HEADER)
        while from_seq < seq_end:
            try:
                meta, samples = fetch_bin_page(host, password, from_seq, limit)
            except Exception as exc:
                print(f"fetch error at from={from_seq}: {exc}", file=sys.stderr)
                return 1
            if meta["count"] == 0:
                break
            for s in samples:
                fp.write(sample_to_csv_row(s))
                rows += 1
            from_seq = int(meta["seqNext"])
            elapsed = max(time.time() - t0, 1e-3)
            print(
                f"… {rows}/{total} seq→{from_seq}  {meta['bytes']/1024:.1f} KiB/page  "
                f"{rows/elapsed:.0f} samp/s",
                file=sys.stderr,
            )
            if meta.get("done"):
                break
    print(f"wrote {out_path} ({rows} rows, {time.time()-t0:.1f}s)", file=sys.stderr)
    return 0


def cmd_capture(host: str, password: str, seconds: int, out_path: str, limit: int) -> int:
    if cmd_json(host, password, "POST", "/debug/clock/start") != 0:
        return 1
    print(f"recording {seconds}s …", file=sys.stderr)
    time.sleep(max(1, seconds))
    if cmd_json(host, password, "POST", "/debug/clock/stop") != 0:
        return 1
    rc = cmd_fetch(host, password, out_path, limit)
    if rc != 0:
        return rc
    return cmd_json(host, password, "POST", "/debug/clock/clear")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--pass", dest="password", default="", help="web/SoftAP password")
    ap.add_argument("--limit", type=int, default=8000, help="samples per binary page")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status")
    sub.add_parser("start")
    sub.add_parser("stop")
    sub.add_parser("clear")
    p_fetch = sub.add_parser("fetch", help="download binary pages → local CSV")
    p_fetch.add_argument("-o", "--output", required=True)
    p_cap = sub.add_parser("capture", help="start → sleep → stop → fetch → clear")
    p_cap.add_argument("--seconds", type=int, required=True)
    p_cap.add_argument("-o", "--output", required=True)

    args = ap.parse_args()
    if args.cmd == "status":
        return cmd_json(args.host, args.password, "GET", "/debug/clock")
    if args.cmd == "start":
        return cmd_json(args.host, args.password, "POST", "/debug/clock/start")
    if args.cmd == "stop":
        return cmd_json(args.host, args.password, "POST", "/debug/clock/stop")
    if args.cmd == "clear":
        return cmd_json(args.host, args.password, "POST", "/debug/clock/clear")
    if args.cmd == "fetch":
        return cmd_fetch(args.host, args.password, args.output, args.limit)
    if args.cmd == "capture":
        return cmd_capture(args.host, args.password, args.seconds, args.output, args.limit)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
