#!/usr/bin/env python3
"""Serve Claude Code quota state as JSON on the LAN for an ESP32 display.

Primary source is ~/.claude/.statusline-usage-cache, which the Claude Usage app
refreshes with authoritative server-side utilization percentages. Reading it
costs nothing and cannot trip a rate limit.

When that cache is missing or stale, fall back to aggregating token counts from
~/.claude/projects/**/*.jsonl. The fallback reports raw token totals, not a
percentage -- Anthropic's limit accounting is not a plain token sum, so any
percentage derived from tokens would be a guess presented as a fact.

No credentials are read and no outbound requests are made.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CLAUDE_DIR = Path.home() / ".claude"
CACHE_FILE = CLAUDE_DIR / ".statusline-usage-cache"
PROJECTS_DIR = CLAUDE_DIR / "projects"

# The cache is written by an external app; treat it as unusable past this age.
CACHE_MAX_AGE_S = 30 * 60

FIVE_HOUR_S = 5 * 60 * 60
WEEK_S = 7 * 24 * 60 * 60

# Tolerate clock skew and sessions whose mtime lags their last entry.
MTIME_SLACK_S = 60 * 60

_TS_RE = re.compile(rb'"timestamp":"([^"]+)"')
_ID_RE = re.compile(rb'"id":"(msg_[^"]+)"')


def _parse_iso(value: str) -> datetime | None:
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (ValueError, AttributeError):
        return None


def read_cache() -> dict | None:
    """Parse the KEY=VALUE utilization cache. Returns None if absent/unparseable."""
    try:
        raw = CACHE_FILE.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None

    fields: dict[str, str] = {}
    for line in raw.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            fields[key.strip()] = value.strip()

    if "UTILIZATION" not in fields:
        return None

    def as_int(key: str) -> int | None:
        try:
            return int(fields[key])
        except (KeyError, ValueError):
            return None

    written_at = as_int("TIMESTAMP")
    if written_at is None:
        # Fall back to file mtime so a cache without TIMESTAMP is still usable.
        try:
            written_at = int(CACHE_FILE.stat().st_mtime)
        except OSError:
            return None

    return {
        "five_pct": as_int("UTILIZATION"),
        "five_reset": fields.get("RESETS_AT") or None,
        "week_pct": as_int("WEEKLY_UTILIZATION"),
        "week_reset": fields.get("WEEKLY_RESETS_AT") or None,
        "profile": fields.get("PROFILE_NAME") or None,
        "age_s": max(0, int(time.time()) - written_at),
    }


def _iter_recent_files(window_s: int):
    """Yield jsonl paths whose mtime falls inside the window (plus slack)."""
    if not PROJECTS_DIR.is_dir():
        return
    cutoff = time.time() - window_s - MTIME_SLACK_S
    for path in PROJECTS_DIR.rglob("*.jsonl"):
        try:
            if path.stat().st_mtime >= cutoff:
                yield path
        except OSError:
            continue


def sum_tokens(window_s: int, now: datetime | None = None) -> dict:
    """Sum billable tokens over the trailing window.

    Claude Code writes one line per content block and each line carries a full
    copy of message.usage, where output_tokens grows as the response streams.
    Summing every line therefore roughly doubles the total. Dedup by
    message.id, keeping the latest line per id, which holds the final counts.
    """
    now = now or datetime.now(timezone.utc)
    cutoff = now.timestamp() - window_s
    latest: dict[str, tuple[float, dict]] = {}

    for path in _iter_recent_files(window_s):
        try:
            with path.open("rb") as handle:
                for line in handle:
                    if b'"assistant"' not in line or b'"usage"' not in line:
                        continue

                    # Cheap pre-filter before paying for json.loads.
                    ts_match = _TS_RE.search(line)
                    if not ts_match:
                        continue
                    stamp = _parse_iso(ts_match.group(1).decode("utf-8", "replace"))
                    if stamp is None or stamp.timestamp() < cutoff:
                        continue
                    if not _ID_RE.search(line):
                        continue

                    try:
                        entry = json.loads(line)
                    except ValueError:
                        continue

                    if entry.get("type") != "assistant":
                        continue
                    message = entry.get("message") or {}
                    usage = message.get("usage")
                    msg_id = message.get("id")
                    if not usage or not msg_id or message.get("model") == "<synthetic>":
                        continue

                    seen = latest.get(msg_id)
                    if seen is None or stamp.timestamp() > seen[0]:
                        latest[msg_id] = (stamp.timestamp(), usage)
        except OSError:
            continue

    totals = {"input": 0, "output": 0, "cache_read": 0, "cache_write": 0}
    for _, usage in latest.values():
        totals["input"] += usage.get("input_tokens") or 0
        totals["output"] += usage.get("output_tokens") or 0
        totals["cache_read"] += usage.get("cache_read_input_tokens") or 0
        totals["cache_write"] += usage.get("cache_creation_input_tokens") or 0

    totals["messages"] = len(latest)
    totals["total"] = sum(
        totals[k] for k in ("input", "output", "cache_read", "cache_write")
    )
    return totals


def _secs_until(iso: str | None, now: datetime) -> int | None:
    stamp = _parse_iso(iso) if iso else None
    if stamp is None:
        return None
    return max(0, int((stamp - now).total_seconds()))


def build_payload(include_tokens: bool = True) -> dict:
    """Assemble the JSON the firmware consumes.

    Field names follow CONTEXT.md rather than being abbreviated. The old short
    keys saved about eighty bytes on a LAN with a 1500-byte MTU, and cost a
    translation table between the two halves of the system: the same "src"
    field meant "why the reading is or is not trustworthy" here and "how the
    HTTP request went" on the device, which are different things that both
    have names now.
    """
    now = datetime.now(timezone.utc)
    cache = read_cache()
    payload: dict = {"recordedAt": int(now.timestamp())}

    fresh = bool(cache) and cache["age_s"] <= CACHE_MAX_AGE_S

    payload["trusted"] = fresh
    payload["trustReason"] = "fresh" if fresh else ("stale" if cache else "missing")
    payload["stalenessSeconds"] = cache["age_s"] if cache else None
    payload["profile"] = (cache or {}).get("profile") or ""

    if fresh:
        payload["session"] = {
            "utilization": cache["five_pct"],
            "secondsToReset": _secs_until(cache["five_reset"], now),
        }
        payload["weekly"] = {
            "utilization": cache["week_pct"],
            "secondsToReset": _secs_until(cache["week_reset"], now),
        }
    else:
        # No trustworthy utilization available. Say so rather than inventing
        # one -- see ADR-0001.
        payload["session"] = {"utilization": None, "secondsToReset": None}
        payload["weekly"] = {"utilization": None, "secondsToReset": None}

    if include_tokens:
        session = sum_tokens(FIVE_HOUR_S, now)
        payload["sessionTokenTotal"] = session["total"]
        payload["sessionMessageCount"] = session["messages"]

    return payload


class QuotaHandler(BaseHTTPRequestHandler):
    server_version = "ClaudeQuotaBridge/1.0"

    def do_GET(self) -> None:  # noqa: N802 - name fixed by BaseHTTPRequestHandler
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        if path not in ("/", "/quota"):
            self.send_error(404, "Not Found")
            return

        try:
            payload = build_payload(include_tokens=self.server.include_tokens)
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        except Exception as exc:  # keep the server alive for the next poll
            body = json.dumps({"ok": False, "src": "error", "err": str(exc)[:120]}).encode()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args) -> None:
        if self.server.verbose:
            sys.stderr.write(
                "%s - %s\n" % (self.address_string(), fmt % args)
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="bind address")
    parser.add_argument("--port", type=int, default=8787, help="listen port")
    parser.add_argument(
        "--no-tokens",
        action="store_true",
        help="skip jsonl token aggregation (faster, less data)",
    )
    parser.add_argument("--once", action="store_true", help="print payload and exit")
    parser.add_argument("-v", "--verbose", action="store_true", help="log requests")
    args = parser.parse_args()

    if args.once:
        print(json.dumps(build_payload(not args.no_tokens), indent=2))
        return 0

    server = ThreadingHTTPServer((args.host, args.port), QuotaHandler)
    server.include_tokens = not args.no_tokens
    server.verbose = args.verbose

    print(f"claude quota bridge listening on http://{args.host}:{args.port}/quota")
    print(f"  cache file : {CACHE_FILE} ({'found' if CACHE_FILE.exists() else 'MISSING'})")
    print(f"  token scan : {'off' if args.no_tokens else 'on'}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
