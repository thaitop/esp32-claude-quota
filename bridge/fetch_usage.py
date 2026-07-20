#!/usr/bin/env python3
"""Fetch Claude utilization from claude.ai and write the cache the bridge reads.

This is the quota source. It replaces the dependency this project used to have
on Claude Usage Tracker, a third-party macOS menu bar app whose private cache
file we were reading: that file's path and format were an implementation detail
of somebody else's release cycle, it only existed on macOS, and the app hits
this same endpoint anyway -- so reading it bought no stability, only coupling.

    GET https://claude.ai/api/organizations/<org-id>/usage
    Cookie: sessionKey=<session-key>

That endpoint is not a public API and carries no compatibility promise. The
session key is a full account credential rather than a scoped API key, so it is
read from a file only its owner can open, and is never logged, echoed, passed as
an argument, or written anywhere. Run --check to see where it is being read from
without printing it.

Why this is a separate process from quota_bridge.py, rather than the bridge
fetching on demand: the display polls the bridge every twenty seconds, and
proxying that straight through would mean twenty-second polling against
claude.ai. Splitting them decouples the two rates -- the display reads a file as
often as it likes while this refreshes on its own slow schedule -- and keeps
quota_bridge.py free of any credential at all.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ORGS_URL = "https://claude.ai/api/organizations"
USAGE_URL = "https://claude.ai/api/organizations/{org_id}/usage"

# Beside history.log, not under ~/.claude: that directory belongs to Claude Code
# and, previously, to the app whose file this one replaces.
DEFAULT_CACHE = Path(__file__).resolve().parent / "usage-cache"

DEFAULT_KEY_FILE = Path.home() / ".config" / "claude-quota" / "session-key"

HTTP_TIMEOUT_S = 20

# Matched to Claude Usage Tracker, which polls this endpoint every 60 seconds --
# a shipped app doing so is the evidence that the rate is tolerated, which is
# better than picking a cautious number out of the air.
#
# It is also about the rate at which the figure can actually move: a session
# window that goes from empty to full over five hours advances a percent roughly
# every three minutes, so a slower poll would show a number one or two points
# behind during heavy use -- which matters either side of the 60% and 85% colour
# thresholds. Slower is still safe, just laggier; the bridge does not call the
# reading stale until it is thirty minutes old.
DEFAULT_INTERVAL_S = 60

# The response carries the same two figures twice: as top-level objects, and as
# entries in a "limits" array that also describes per-model scoped windows we do
# not display. The top-level pair is read first because it is unambiguous; the
# array is the fallback, on the guess that the flat fields are the older shape
# and likelier to be dropped. Confirm any change with --raw before editing.
SESSION_KEYS = ("five_hour",)
WEEKLY_KEYS = ("seven_day", "weekly", "week", "seven_days")

# Matched against limits[].kind. "weekly_all" is the account-wide weekly window;
# "weekly_scoped" is a per-model limit and is deliberately not one of these.
SESSION_KINDS = ("session",)
WEEKLY_KINDS = ("weekly_all",)


class UsageError(RuntimeError):
    """Anything that should reach the user as a message rather than a traceback."""


def read_session_key(path: Path) -> str:
    """Read the key, refusing a file other users can open.

    A session key in a group- or world-readable file is a credential shared with
    every account on the host, which is not what anyone means to do -- so this
    fails rather than warns.
    """
    try:
        mode = path.stat().st_mode
    except OSError as error:
        raise UsageError(
            f"cannot read the session key at {path} ({error.strerror}). "
            "See the setup section of README.md."
        ) from None

    if mode & (stat.S_IRWXG | stat.S_IRWXO):
        raise UsageError(
            f"{path} is readable by other users. Run: chmod 600 {path}"
        )

    key = path.read_text(encoding="utf-8").strip()
    if not key:
        raise UsageError(f"{path} is empty")
    return key


def _get(url: str, session_key: str):
    request = urllib.request.Request(
        url,
        headers={
            "Cookie": f"sessionKey={session_key}",
            "Accept": "application/json",
            "User-Agent": "esp32-claude-quota/1.0",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_S) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as error:
        if error.code in (401, 403):
            raise UsageError(
                f"claude.ai rejected the session key (HTTP {error.code}). It has "
                "most likely expired -- copy a fresh one from the browser into "
                "the key file."
            ) from None
        raise UsageError(f"claude.ai returned HTTP {error.code}") from None
    except urllib.error.URLError as error:
        # The python.org macOS build ships without a populated trust store, and
        # its handshake failure is otherwise a wall of traceback.
        if "CERTIFICATE_VERIFY_FAILED" in str(error.reason):
            raise UsageError(
                "TLS certificate verification failed. This Python has no CA "
                "certificates -- use Homebrew's interpreter, or run the "
                '"Install Certificates.command" from the python.org install.'
            ) from None
        raise UsageError(f"cannot reach claude.ai: {error.reason}") from None


def discover_org_id(session_key: str) -> str:
    """Pick the organization to report on, so it need not be configured.

    Accounts normally have exactly one. With several, the first is used and the
    rest are named in the error path only if that turns out to be wrong -- there
    is no way to guess which one the user means, so --org-id exists.
    """
    payload = _get(ORGS_URL, session_key)
    if not isinstance(payload, list) or not payload:
        raise UsageError("no organizations on this account")
    org_id = payload[0].get("uuid") or payload[0].get("id")
    if not org_id:
        raise UsageError("could not find an organization id in the response")
    return str(org_id)


def fetch_usage(session_key: str, org_id: str) -> dict:
    if "/" in org_id or ".." in org_id:
        raise UsageError("organization id must not contain '/' or '..'")
    return _get(USAGE_URL.format(org_id=org_id), session_key)


def _window(payload: dict, keys: tuple[str, ...], kinds: tuple[str, ...]):
    """Find one limit window, as (percent, resets_at), or None.

    Utilization comes back as a float (16.0) at the top level and as an int
    (16) in the limits array. Both are rounded rather than truncated, so the two
    paths cannot disagree by a point on a value like 16.6.
    """
    for key in keys:
        window = payload.get(key)
        if isinstance(window, dict) and window.get("utilization") is not None:
            return round(window["utilization"]), window.get("resets_at")

    limits = payload.get("limits")
    if isinstance(limits, list):
        for entry in limits:
            if not isinstance(entry, dict) or entry.get("kind") not in kinds:
                continue
            if entry.get("percent") is None:
                continue
            return round(entry["percent"]), entry.get("resets_at")
    return None


def to_cache_text(payload: dict, now: int) -> str:
    """Render the payload as the KEY=VALUE cache quota_bridge.py parses."""
    session = _window(payload, SESSION_KEYS, SESSION_KINDS)
    if session is None:
        raise UsageError(
            "no session-window utilization in the response. Run --raw to see "
            f"what came back; expected one of {', '.join(SESSION_KEYS)} or a "
            f"limits entry of kind {', '.join(SESSION_KINDS)}."
        )

    session_pct, session_reset = session
    lines = [
        f"UTILIZATION={session_pct}",
        f"RESETS_AT={session_reset or ''}",
        f"TIMESTAMP={now}",
    ]

    weekly = _window(payload, WEEKLY_KEYS, WEEKLY_KINDS)
    if weekly is not None:
        weekly_pct, weekly_reset = weekly
        lines.append(f"WEEKLY_UTILIZATION={weekly_pct}")
        lines.append(f"WEEKLY_RESETS_AT={weekly_reset or ''}")
    else:
        # Omit rather than invent. quota_bridge.py forwards a missing weekly
        # figure as null and the Weekly screen shows the Unknown marker, which
        # is the truth (ADR-0001). --raw names the field if it moved.
        print(
            f"warning: no weekly window matched {', '.join(WEEKLY_KEYS)} or a "
            f"limits entry of kind {', '.join(WEEKLY_KINDS)}; the Weekly "
            "screen will read '--'. Run --raw and add the real name.",
            file=sys.stderr,
        )

    return "\n".join(lines) + "\n"


def write_cache(path: Path, text: str) -> None:
    """Replace the cache atomically, so the bridge never reads a partial file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def run_once(session_key: str, org_id: str, cache: Path, raw: bool) -> None:
    payload = fetch_usage(session_key, org_id)
    if raw:
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return
    write_cache(cache, to_cache_text(payload, int(time.time())))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch Claude utilization and write the bridge's cache."
    )
    parser.add_argument(
        "--key-file",
        type=Path,
        default=Path(os.environ.get("CLAUDE_QUOTA_KEY_FILE") or DEFAULT_KEY_FILE),
        help=f"file holding the sessionKey cookie, mode 600 (default: {DEFAULT_KEY_FILE})",
    )
    parser.add_argument(
        "--org-id",
        default=os.environ.get("CLAUDE_ORG_ID") or None,
        help="organization UUID (default: discovered from the account)",
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=DEFAULT_CACHE,
        help=f"cache file to write (default: {DEFAULT_CACHE})",
    )
    parser.add_argument(
        "--interval",
        type=int,
        nargs="?",
        const=DEFAULT_INTERVAL_S,
        metavar="SECONDS",
        help="keep running, refreshing this often "
        f"(default when given without a value: {DEFAULT_INTERVAL_S})",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="print the JSON response and exit, writing nothing",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report where the key and cache would be read and written, then exit",
    )
    args = parser.parse_args()

    if args.check:
        # Deliberately prints paths and permissions, never the key itself.
        print(f"key file: {args.key_file}")
        try:
            mode = args.key_file.stat().st_mode
            print(f"  exists, mode {stat.filemode(mode)}")
            if mode & (stat.S_IRWXG | stat.S_IRWXO):
                print("  WARNING: readable by other users; chmod 600 it")
        except OSError as error:
            print(f"  missing ({error.strerror})")
        print(f"cache:    {args.cache}")
        print(f"org id:   {args.org_id or '(discovered at run time)'}")
        return 0

    # Validate once up front so a typo fails immediately rather than looking
    # like a healthy daemon that never writes anything.
    try:
        org_id = args.org_id or discover_org_id(read_session_key(args.key_file))
    except UsageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    while True:
        try:
            # Re-read every pass rather than caching the key in memory. A
            # session key expires, and the fix is to paste a new one into the
            # file -- which has to take effect without hunting down the process
            # to restart, because a failing pass does not exit this loop and so
            # launchd's KeepAlive will never do it either.
            run_once(read_session_key(args.key_file), org_id, args.cache, args.raw)
        except (UsageError, OSError, json.JSONDecodeError) as error:
            # Never include the request in the message: it carries the cookie.
            print(f"fetch failed: {error}", file=sys.stderr)
            if not args.interval:
                return 1
        else:
            if not args.interval or args.raw:
                return 0

        # A failed pass leaves the previous cache in place. It ages past the
        # bridge's staleness limit on its own and the display falls back to the
        # Unknown marker, so there is nothing to clean up here.
        time.sleep(args.interval)


if __name__ == "__main__":
    sys.exit(main())
