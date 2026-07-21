# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A desk display for Claude Code's two rate-limit windows, plus weather and crypto, running on the ESP32-2432S028R ("Cheap Yellow Display": ESP32-WROOM-32, 2.8" 240x320 ILI9341, XPT2046 resistive touch), drawn with LVGL 9. Two halves:

- **`firmware/`** — C++/Arduino for the ESP32 (PlatformIO).
- **`bridge/`** — Python on the Mac running Claude Code. Fetches utilization from claude.ai and serves it over the LAN.

Read these before non-trivial work — they carry the reasoning the code assumes:
- **`CONTEXT.md`** — the shared vocabulary both halves use (Quota Window, Utilization, Trust, Staleness, Snapshot, Feed, Ramp, …). Terms have one name on both sides of the wire; keep it that way, and honour the `_Avoid_` lists.
- **`docs/adr/`** — the three decisions that constrain everything else (see Invariants below).
- **`README.md`** — build/flash/run and troubleshooting for someone starting from bare hardware.

## Commands

Firmware (run from `firmware/`; `pio` lands at `~/.local/bin/pio`):

```bash
~/.local/bin/pio run                       # build
~/.local/bin/pio run --target upload       # build + flash (auto-picks port)
~/.local/bin/pio device monitor            # serial @ 115200
~/.local/bin/pio run -t clean              # REQUIRED after editing include/lv_conf.h (PlatformIO caches the LVGL build)
```

Redirect uploads to a file and check the exit code — `pio` writes hundreds of lines and a piped `tail`/`grep` in an agent shell truncates from the wrong end, returning the header (dep graph + memory table) while the `SUCCESS` line never arrives, which looks exactly like a failed flash:

```bash
~/.local/bin/pio run --target upload > /tmp/upload.log 2>&1; echo "exit=$?"
grep -E "Hash of data|Hard resetting|SUCCESS|FAILED" /tmp/upload.log
```

Bridge (use a Python with a working TLS trust store — Homebrew's, **not** the python.org build):

```bash
python3 bridge/fetch_usage.py --check      # verify key file paths/permissions, no key printed
python3 bridge/fetch_usage.py              # write bridge/usage-cache once
python3 bridge/fetch_usage.py --interval   # loop on a 60s timer (the fetcher's production mode)
python3 bridge/fetch_usage.py --raw        # print the real claude.ai payload (for debugging field renames)
python3 bridge/quota_bridge.py             # serve /quota + /history on :8787
python3 bridge/quota_bridge.py --once      # print one payload and exit
python3 bridge/quota_bridge.py --no-tokens # skip the jsonl transcript scan (much faster)
```

There is **no test suite and no host simulator** in the repo despite `model.h`/`https.h` comments describing host-compilable seams — that layering is real (see below) but the harness that would exercise it is not checked in.

## Architecture

### Two processes on the Mac, deliberately split (do not merge them)

- **`fetch_usage.py`** is the only component that ever holds the `sessionKey` cookie (a full account credential). It hits the undocumented `GET https://claude.ai/api/organizations/<org-id>/usage` on a 60s timer and writes a KEY=VALUE cache (`bridge/usage-cache`). The key is read from `~/.config/claude-quota/session-key` (mode 600, outside the repo) on **every** pass, so an expired key is fixed by pasting a fresh one — no restart.
- **`quota_bridge.py`** reads that cache and serves it as JSON on the LAN. It makes **zero outbound calls** and touches **no credential** — a file reader with an HTTP interface.

The split decouples the display's poll rate (20s) from the claude.ai call rate (60s): a second display or a faster poll changes nothing about what leaves the machine. Folding the fetch into the bridge would tie them together and put a credential in the LAN-facing process.

### Firmware layering (`firmware/src/`)

The seam is **`model.h`**: the `AppModel` struct and its snapshots. `net/` fills these structs in; `ui/` reads them. Neither side knows about the other — `ui/` never includes `<WiFi.h>` or HTTP; `net/` never touches LVGL. `model.h` deliberately uses `<cstdint>` not `<Arduino.h>` so it could compile on the host. Preserve this: when a screen needs a fact about the radio or a feed, route it through the model (see `recordLink()` in `main.cpp`), don't reach across the seam.

- **`net/poller.cpp`** — round-robin scheduler. Runs **at most one fetch per pass**, for whichever feed is most overdue. `main.cpp` calls `net::service()` each loop.
- **`net/bridge.cpp`** — the Bridge Feed (quota + history). History runs on its own 10-min sub-timer *inside* the Bridge Feed's slot, so there is still only one request in flight ever.
- **`net/https.cpp`** — shared one-shot TLS GET for Weather/Crypto; tears the client down the moment the body parses.
- **`ui/`** — one `screen_*.cpp` per Screen (Claude, Weekly, Weather, Crypto, Setting), plus `nav`, `theme`, `widgets`, `format`. Screens are change-detecting; only the active Screen repaints. Switching to a Screen calls its update function so it redraws what went stale while hidden (`refreshActiveScreen` in `main.cpp`).
- **`config.h`** — committed tunables (poll intervals, timeouts, coins, TZ, colour ramp, touch calibration). **`secrets.h`** — gitignored (WiFi creds, bridge URL, home coordinates); copy from `secrets.h.example`.

### Invariants (the three ADRs — violating these produces silent, delayed failures)

1. **Utilization is reported, never derived** (ADR-0001). When the cache is missing/stale the display shows Unknown (`--`), never a percentage synthesized from token counts. Zero is a real value; Unknown is `-1` sentinels (`UTILIZATION_UNKNOWN`, `RESET_UNKNOWN`) rendered as `--`. Token Total is reported under its own name and never feeds utilization.
2. **Mixed feed topology** (ADR-0002). Quota goes through the bridge (credential stays off the device); Weather and Crypto are fetched **directly** by the ESP32 with `setInsecure()` (no credential, no private data, no CA to expire). A sleeping Mac costs only the two Claude screens.
3. **Partial draw buffer, one feed in flight** (ADR-0003). ~290KB usable heap, no PSRAM; a TLS handshake peaks ~45KB. LVGL uses a 1/10-screen double buffer (~30KB), and feeds are **never** polled concurrently. The subtle third heap consumer is LVGL's own pool `LV_MEM_SIZE`: the Claude screen's gradient progress bar needs a 9088-byte *layer* per repaint. If the largest contiguous free block drops below that, LVGL logs "Allocating layer buffer failed. Try later" and retries forever, starving the loop — a freeze/reboot minutes later, not a crash at the edit. **Any change that grows a screen or touches buffer/poll scheduling must be checked against `lv_mem_monitor()` free-biggest on the device** (printed at boot in `main.cpp`), not the (ceiling-less) host.

### Trust vs. Fetch Outcome

A healthy HTTP 200 can still carry values too stale to show. `Trust` (fit to display) is distinct from `FetchOutcome` (transport-level: Never/Ok/Offline/Unreachable/Rejected/Malformed). A reading older than `QUOTA_STALENESS_LIMIT_S` (30 min) loses Trust even on a good fetch. Keep these separate — never render a `FetchOutcome` as a value.
</content>
</invoke>
