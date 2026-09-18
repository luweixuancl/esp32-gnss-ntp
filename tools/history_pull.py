#!/usr/bin/env python3
"""Pull device-side PSRAM history (/history + /history.csv).

Read-only. Useful after flashing v1.1.9+ on ESP32-S3 to verify the ring
and dump a window for offline analysis (alongside clock_drift_monitor).

Examples:
  tools/history_pull.py --host 10.81.127.14
  tools/history_pull.py --host 10.81.127.14 --last 3600 --csv out.csv
"""
from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request


def get(url: str, timeout: float = 30.0) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.read()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", required=True, help="device IP / hostname")
    ap.add_argument("--last", type=int, default=3600,
                    help="seconds of CSV to fetch (0 = all; default 3600)")
    ap.add_argument("--max", type=int, default=0,
                    help="optional hard cap on CSV rows")
    ap.add_argument("--csv", default="",
                    help="write CSV to this path (default: stdout if --csv-only)")
    ap.add_argument("--csv-only", action="store_true",
                    help="skip JSON summary; only emit CSV")
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    base = "http://" + args.host.rstrip("/")

    if not args.csv_only:
        try:
            raw = get(base + "/history", timeout=min(args.timeout, 15.0))
        except (urllib.error.URLError, TimeoutError) as exc:
            print(f"GET /history failed: {exc}", file=sys.stderr)
            return 1
        try:
            summary = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as exc:
            print(f"bad JSON: {exc}", file=sys.stderr)
            return 1
        print(json.dumps(summary, indent=2, ensure_ascii=False))
        if not summary.get("enabled"):
            print("history disabled on this device (expected on ESP32-C3).",
                  file=sys.stderr)
            return 0

    q = []
    if args.last > 0:
        q.append(f"last={args.last}")
    if args.max > 0:
        q.append(f"max={args.max}")
    csv_url = base + "/history.csv"
    if q:
        csv_url += "?" + "&".join(q)

    try:
        body = get(csv_url, timeout=args.timeout)
    except (urllib.error.URLError, TimeoutError) as exc:
        print(f"GET /history.csv failed: {exc}", file=sys.stderr)
        return 1

    if args.csv:
        with open(args.csv, "wb") as f:
            f.write(body)
        print(f"wrote {len(body)} bytes → {args.csv}", file=sys.stderr)
    elif args.csv_only:
        sys.stdout.buffer.write(body)
    else:
        # Preview first lines after JSON summary.
        text = body.decode("utf-8", errors="replace")
        lines = text.splitlines()
        preview = 12 if len(lines) > 12 else len(lines)
        print(f"\n# CSV {len(lines)} lines (showing {preview})")
        print("\n".join(lines[:preview]))
        if len(lines) > preview:
            print("...")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
