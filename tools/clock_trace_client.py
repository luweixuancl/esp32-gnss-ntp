#!/usr/bin/env python3
"""Clock-trace client: start/stop/status/fetch/clear against /debug/clock*.

Fetch is only allowed after stop (device returns HTTP 409 while recording).
Incremental CSV download is handled automatically.

Examples:
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 start
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 status
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 stop
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 fetch -o out.csv
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 clear

  # Unattended: start → wait → stop → fetch → clear
  python3 tools/clock_trace_client.py --host 192.168.1.24 --pass NTP-9EC4 \\
      capture --seconds 3600 -o hour.csv
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def _url(host: str, path: str, password: str) -> str:
    q = urllib.parse.urlencode({"pass": password}) if password else ""
    base = f"http://{host}{path}"
    return f"{base}?{q}" if q else base


def _req(method: str, url: str, timeout: float = 30.0) -> tuple[int, bytes]:
    r = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.getcode(), resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def cmd_json(host: str, password: str, method: str, path: str) -> int:
    code, body = _req(method, _url(host, path, password))
    text = body.decode("utf-8", errors="replace")
    try:
        data = json.loads(text)
        print(json.dumps(data, indent=2, ensure_ascii=False))
    except json.JSONDecodeError:
        print(text, end="" if text.endswith("\n") else "\n")
    return 0 if 200 <= code < 300 else 1


def parse_next(chunk: str) -> tuple[int | None, bool | None]:
    next_seq = None
    done = None
    for line in chunk.splitlines():
        if line.startswith("# next="):
            # # next=1234 done=1
            parts = line[2:].strip().split()
            for p in parts:
                if p.startswith("next="):
                    next_seq = int(p.split("=", 1)[1])
                elif p.startswith("done="):
                    done = p.split("=", 1)[1] == "1"
    return next_seq, done


def cmd_fetch(host: str, password: str, out_path: str, limit: int) -> int:
    st_code, st_body = _req("GET", _url(host, "/debug/clock", password))
    if st_code != 200:
        print(st_body.decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    st = json.loads(st_body.decode("utf-8"))
    if st.get("state") != "STOP":
        print(
            f"refuse fetch: state={st.get('state')} (stop first)",
            file=sys.stderr,
        )
        return 2
    if int(st.get("count") or 0) == 0:
        print("empty trace", file=sys.stderr)
        open(out_path, "w", encoding="utf-8").close()
        return 0

    seq_first = int(st.get("seqFirst") or 0)
    seq_next_end = int(st.get("seqNext") or 0)
    from_seq = seq_first
    wrote = 0
    with open(out_path, "w", encoding="utf-8", newline="\n") as fp:
        while from_seq < seq_next_end:
            q = {"from": str(from_seq), "limit": str(limit)}
            if password:
                q["pass"] = password
            url = f"http://{host}/debug/clock/data?{urllib.parse.urlencode(q)}"
            code, body = _req("GET", url, timeout=120.0)
            if code == 204:
                break
            if code != 200:
                print(body.decode("utf-8", errors="replace"), file=sys.stderr)
                return 1
            text = body.decode("utf-8", errors="replace")
            # Drop trailer meta from file body but keep CSV rows + first header.
            lines_out = []
            for line in text.splitlines():
                if line.startswith("# next="):
                    continue
                lines_out.append(line)
            chunk = "\n".join(lines_out)
            if chunk and not chunk.endswith("\n"):
                chunk += "\n"
            # Avoid duplicating CSV header on continuation pages.
            if wrote > 0:
                kept = []
                for line in chunk.splitlines(True):
                    if line.startswith("seq,") or line.startswith("# clock_trace"):
                        continue
                    kept.append(line)
                chunk = "".join(kept)
            fp.write(chunk)
            wrote += 1
            nxt, done = parse_next(text)
            if nxt is None:
                print("missing # next= trailer", file=sys.stderr)
                return 1
            if done or nxt <= from_seq:
                break
            from_seq = nxt
            print(f"… fetched through seq {from_seq}", file=sys.stderr)
    print(f"wrote {out_path} (pages={wrote}, seq {seq_first}..{seq_next_end})", file=sys.stderr)
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
    ap.add_argument("--limit", type=int, default=2000, help="samples per fetch page")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status")
    sub.add_parser("start")
    sub.add_parser("stop")
    sub.add_parser("clear")
    p_fetch = sub.add_parser("fetch")
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
