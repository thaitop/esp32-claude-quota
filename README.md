# Claude quota monitor — ESP32-2432S028R

<p align="center">
  <b>🇬🇧 English</b> &nbsp;·&nbsp; <a href="README.th.md">🇹🇭 ภาษาไทย</a>
</p>

A desk display for Claude Code's two usage windows, plus weather, crypto and
stocks, running on the "Cheap Yellow Display" (ESP32-WROOM-32 + 2.8" 240x320
ILI9341 + XPT2046 resistive touch), drawn with LVGL 9.

| Screen | Shows | Feed |
|---|---|---|
| **Claude** | Session and Weekly utilization, with live reset countdowns | Bridge |
| **Weekly Usage** | The last seven days of weekly utilization, plotted | Bridge |
| **Weather** | Temperature, apparent temperature, conditions, humidity | Weather |
| **Crypto** | One coin's price, 24h change, dollar move and volume; BTC/ETH/BNB | Crypto |
| **Stock** | Five tickers as a list, with a market-open badge | Stock |
| **Setting** | Link, per-feed health, quota staleness, heap, uptime, backlight | — |

[CONTEXT.md](CONTEXT.md) defines the shared vocabulary. The [ADRs](docs/adr/)
record the four decisions that constrain the rest.

## What you need

| Item | Notes |
|---|---|
| **ESP32-2432S028R** board | The 2.8" single-USB "CYD". ~$10. |
| **USB cable** | Micro-USB **data** cable — a charge-only one looks like a dead board. |
| **A computer running Claude Code** | Source of the quota numbers. Stays awake, same LAN as the display. macOS below; the bridge is plain Python 3.11+. |
| **A claude.ai session key** | Pasted into a file during setup (step 3). |
| **CH340 USB serial driver** | Built into macOS 12+/Linux. Windows needs [the WCH driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html). |
| **PlatformIO Core** | `pip install -U platformio` — lands at `~/.local/bin/pio`. |

No soldering, no extra parts.

## 1. Clone and configure

```bash
git clone https://github.com/thaitop/esp32-claude-quota.git
cd esp32-claude-quota
cp firmware/src/secrets.h.example firmware/src/secrets.h
```

Edit `firmware/src/secrets.h`:

```c
#define WIFI_SSID     "your-network"        // 2.4GHz only — ESP32 has no 5GHz radio
#define WIFI_PASSWORD "your-password"
#define BRIDGE_BASE_URL "http://192.168.1.117:8787"   // LAN IP of the Claude Code machine
#define WEATHER_LATITUDE  13.75f
#define WEATHER_LONGITUDE 100.50f
#define WEATHER_TZ "Asia/Bangkok"           // IANA zone name — Open-Meteo takes nothing else
#define CLOCK_TZ   "UTC+7"                   // header clock offset, e.g. "UTC-5", "UTC+5:30", "UTC"
#define FINNHUB_TOKEN "your-finnhub-token"  // free key from finnhub.io/register — Stock screen only
```

The Finnhub token is a soft credential (public quotes only); leaving the
placeholder just shows `--` on the Stock screen. Get the machine's IP with
`ipconfig getifaddr en0` (macOS) or `hostname -I` (Linux) — prefer an IP over
`.local`, and give it a DHCP reservation so the firmware need not be reflashed.

`secrets.h` is gitignored. Everything else — poll intervals, timeouts, coins,
tickers, colour thresholds, touch calibration — lives in the committed
`firmware/src/config.h`. Why the token and TZ sit in `secrets.h` is
[ADR-0004](docs/adr/0004-stock-feed-soft-credential.md).

## 2. Flash

```bash
cd firmware
~/.local/bin/pio run --target upload > /tmp/upload.log 2>&1; echo "exit=$?"
grep -E "Hash of data|Hard resetting|SUCCESS|FAILED" /tmp/upload.log
```

Redirect to a file — a piped `tail`/`grep` truncates from the wrong end and the
`SUCCESS` line never arrives, which looks like a failed flash. First build pulls
the toolchain and libraries (a few minutes); later builds ~1 min, upload ~50s.
PlatformIO picks the port; name one with `--upload-port /dev/cu.usbserial-XXXX`
if two boards are attached.

Then watch it boot:

```bash
~/.local/bin/pio device monitor
```

Weather and Crypto work as soon as WiFi is up; the two Claude screens stay at
`--` until the bridge is running.

## 3. Set up the quota source

`bridge/fetch_usage.py` gets the utilization reading. Give it a session key:

```bash
mkdir -p ~/.config/claude-quota
pbpaste > ~/.config/claude-quota/session-key   # or paste into an editor
chmod 600 ~/.config/claude-quota/session-key
```

The value is the `sessionKey` cookie from a browser logged in to claude.ai
(dev tools → Application → Cookies → `https://claude.ai`). Claude Code's OAuth
login does **not** work — the usage endpoint is cookie authenticated. The script
refuses a group- or world-readable key file, so the `chmod` is not optional.

```bash
python3 bridge/fetch_usage.py --check    # verify paths/permissions, no key printed
python3 bridge/fetch_usage.py            # write bridge/usage-cache once
python3 bridge/fetch_usage.py --interval # loop on a 60s timer (production mode)
```

Use a Python with a working TLS trust store — Homebrew's, not the python.org
build (which fails the handshake with "no CA certificates"). Pass `--org-id` if
you belong to more than one organization.

Already running [Claude Usage Tracker](https://github.com/hamed-elfayome/Claude-Usage-Tracker)?
Point the bridge at its cache and skip this step:
`python3 bridge/quota_bridge.py --cache ~/.claude/.statusline-usage-cache`.

## 4. Start the bridge

```bash
/opt/homebrew/bin/python3.13 bridge/quota_bridge.py
# → http://0.0.0.0:8787/quota
```

It reads the cache and serves it as JSON on the LAN — zero outbound calls, no
credential. Check it:

```bash
curl -s http://localhost:8787/quota | python3 -m json.tool
curl -s http://<machine-ip>:8787/quota     # from a phone or laptop
```

Flags: `--port`, `--host`, `--no-tokens` (skip the jsonl scan, much faster),
`--once`, `-v`. If the second `curl` hangs, the host firewall is blocking
inbound — allow the Python binary (macOS: System Settings → Network → Firewall).

## 5. Keep both processes running

There are two separate processes — the fetcher (talks to claude.ai on a slow
timer) and the bridge (serves the LAN) — so you install **two** `launchd` jobs.
Ready-made plists live in `bridge/launchd/`. Copy both into
`~/Library/LaunchAgents/`, drop the `.example` suffix, and set your repo path:

```bash
cp bridge/launchd/com.local.claude-quota-fetch.plist.example \
   ~/Library/LaunchAgents/com.local.claude-quota-fetch.plist
cp bridge/launchd/com.local.claude-quota-bridge.plist.example \
   ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist

# the plists ship with a placeholder path (/Users/you/esp32-claude-quota);
# this rewrites it to your real repo path. Run it from the repo root, so $PWD
# is the checkout — check first with: echo $PWD
sed -i '' "s#/Users/you/esp32-claude-quota#$PWD#" \
  ~/Library/LaunchAgents/com.local.claude-quota-{fetch,bridge}.plist
```

Then load both and check they are up:

```bash
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-fetch.plist
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
launchctl list | grep claude-quota    # second column is the last exit code (0 = ok)
```

Each plist points at a wrapper script (`bridge/claude-quota-fetch` /
`bridge/claude-quota-bridge`) rather than at `python3.13` directly, so Login
Items lists them readably and stdout stays unbuffered. No session key goes in
either plist — it is read from `~/.config/claude-quota/session-key` at run time.

Notes:

- After editing a plist, `unload` then `load` it — `kickstart -k` reuses the
  cached definition and the edit appears to do nothing.
- `KeepAlive` restarts a dead job, so stop them through `launchctl unload`, not
  `kill`, or they come straight back.
- An expired session key does **not** kill the fetcher — it keeps looping and
  logging the rejection. Paste a fresh key into the key file and the display
  recovers within one interval, no restart.
- The wrappers default to `/opt/homebrew/bin/python3.13`. If your Homebrew
  Python lives elsewhere, set `CLAUDE_QUOTA_PYTHON` in the plist (an
  `EnvironmentVariables` dict) or edit the wrapper.
- Linux: a user systemd unit with `Restart=always`. Windows: Task Scheduler at
  logon.

## How it gets the numbers

The `/usage` percentages are not in any public API. The web app has its own
endpoint, which is what this reads:

```
GET https://claude.ai/api/organizations/<org-id>/usage
Cookie: sessionKey=<session-key>
```

Two processes, deliberately. `fetch_usage.py` holds the credential, calls that
on a 60s timer, and writes a small KEY=VALUE file:

```
UTILIZATION=89
RESETS_AT=2026-07-20T10:59:59Z
TIMESTAMP=1784557286
WEEKLY_UTILIZATION=31
WEEKLY_RESETS_AT=2026-07-22T17:59:59Z
```

`quota_bridge.py` reads it and serves JSON on the LAN. The split decouples the
display's 20s poll from the 60s claude.ai call, so a second display changes
nothing about what leaves the machine, and keeps the account credential out of
the LAN-facing process.

If the cache is missing or older than 30 minutes the bridge reports
`"trusted": false` and every quota figure becomes `--`. It never synthesizes a
percentage from token counts — that would be a guess wearing the costume of a
fact ([ADR-0001](docs/adr/0001-never-derive-utilization-from-tokens.md)).

Weather, crypto and stocks are fetched **directly by the device**, so a sleeping
Mac costs only the two Claude screens
([ADR-0002](docs/adr/0002-mixed-feed-topology.md)). One request is in flight at
a time, ever — a TLS handshake peaks near 45KB and two overlapping do not fit
beside the draw buffers
([ADR-0003](docs/adr/0003-partial-draw-buffer-and-serial-polling.md)).

| Feed | Interval |
|---|---|
| Bridge — quota | 20s |
| Bridge — history | 10min (inside the Bridge slot) |
| Weather | 5min |
| Crypto | 60s |

The bridge also appends a trustworthy Sample (at most one per 3h bucket) to
`bridge/history.log`, which feeds the Weekly Usage plot — 56 Samples cover the
seven days the firmware's ring holds.

## Touch controls

| Gesture | Effect |
|---|---|
| Tap a navbar Slot | Switch Screen |
| Tap above the navbar | Force an immediate refresh of every feed |
| Hold ~1s above the navbar | Blank the display (tap to wake) |

The Setting screen is a readout, not an editor (apart from the backlight
stepper). Changing configuration means reflashing.

## Colour thresholds

| Utilization | Colour |
|---|---|
| < 60% | green |
| 60–84% | amber |
| ≥ 85% | red |

## Troubleshooting

- **Upload won't connect** (`No serial data received`). Hold BOOT while it
  starts. No port under `pio device list` → cable or CH340 driver.
- **Upload dies mid-transfer.** `upload_speed` is already 230400; a stubborn
  unit may need 115200 in `platformio.ini`.
- **Blank display, serial active.** Backlight pin. GPIO21 is right for this
  board; other CYD revisions use GPIO27 — change `-DTFT_BL=21`.
- **Colours inverted.** Swap `-DILI9341_2_DRIVER=1` for `-DILI9341_DRIVER=1`.
- **Touches off-centre.** Replace the four `TOUCH_RAW_*` constants in
  `config.h` with your own measurements.
- **WiFi never joins.** 2.4GHz only; captive-portal networks won't work.
- **Any figure shows `--`.** The Unknown marker — the value is not trustworthy,
  not zero. The Setting screen names the failing Feed and how stale it is.
- **Only Claude screens `--`, bridge fine.** The cache behind it is stale/missing,
  nearly always an expired session key. Paste a fresh one; it recovers next pass.
  `cat bridge/usage-cache` and `python3 bridge/fetch_usage.py` show the failure.
- **Editing `include/lv_conf.h` changes nothing.** PlatformIO caches the LVGL
  build — run `pio run -t clean` first.
- **Board resets on serial open.** Normal — DTR assertion is a power-on reset,
  not a crash.

## Security notes

- The `sessionKey` cookie is a **full account credential**, not a scoped API
  key. `fetch_usage.py` is the only component that ever sees it; it is never
  logged or passed where `ps` would show it. It lives in
  `~/.config/claude-quota/session-key`, outside the repo, mode 600. Treat it
  like your password; do not put it in a plist or a shell profile.
- The ESP32 stores WiFi credentials in plaintext in flash — treat a board you
  give away as a board you gave your WiFi password to.
- The bridge serves on the LAN with no auth (percentages and optional token
  counts, no credentials). Bind it to a trusted network; do not port-forward.
- Weather and crypto skip certificate validation deliberately
  ([ADR-0002](docs/adr/0002-mixed-feed-topology.md)); neither carries a
  credential.
- `/api/organizations/<id>/usage` is undocumented and can change without
  notice. When it breaks, the display shows `--` rather than a stale number.
