# Claude quota monitor — ESP32-2432S028R

A desk display for Claude Code's two usage windows, plus a few ambient
readouts, running on the "Cheap Yellow Display" (ESP32-WROOM-32 + 2.8"
240x320 ILI9341 + XPT2046 resistive touch), drawn with LVGL 9.

Six Screens, one per navbar Slot:

| Screen | Shows | Feed |
|---|---|---|
| **Claude** | Session and Weekly utilization, with live reset countdowns | Bridge |
| **Weekly Usage** | The last seven days of weekly utilization, plotted | Bridge |
| **Weather** | Temperature, apparent temperature, conditions, humidity | Weather |
| **Crypto** | One coin's price, 24-hour change, dollar move and volume; BTC/ETH/BNB via a toggle | Crypto |
| **Stock** | Five tickers as a list — price and the day's move each; a market-open badge | Stock |
| **Setting** | Link, per-feed health, quota staleness, heap, uptime, backlight | — |

[CONTEXT.md](CONTEXT.md) defines the vocabulary both halves of the system use.
The [ADRs](docs/adr/) record the four decisions that constrain the rest.

## Build one yourself

### What you need

| Item | Notes |
|---|---|
| **ESP32-2432S028R** board | The 2.8" single-USB "CYD". ~$10 from AliExpress/Amazon. Other CYD revisions mostly work — see the backlight note in Troubleshooting. |
| **USB cable** | Micro-USB, **data** cable. A charge-only cable enumerates nothing and looks like a dead board. |
| **A computer running Claude Code** | The quota numbers come from this machine. It must stay awake and on the same LAN as the display. Instructions below are macOS; the bridge itself is plain Python 3.11+ and runs anywhere Claude Code does. |
| **A claude.ai session key** | The two Claude screens need one. It is pasted into a file during setup — see [step 3](#3-set-up-the-quota-source). No extra app to install. |
| **CH340 USB serial driver** | Built into macOS 12+ and Linux. Windows needs [the WCH driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html). |
| **PlatformIO Core** | `pip install -U platformio` — lands at `~/.local/bin/pio`. Handles the ESP32 toolchain and every library itself. |

No soldering, no extra parts. Everything else is on the board.

### 1. Clone and configure

```bash
git clone https://github.com/thaitop/esp32-claude-quota.git
cd esp32-claude-quota
cp firmware/src/secrets.h.example firmware/src/secrets.h
```

Edit `firmware/src/secrets.h`:

```c
#define WIFI_SSID     "your-network"        // 2.4GHz only — ESP32 has no 5GHz radio
#define WIFI_PASSWORD "your-password"
#define BRIDGE_BASE_URL "http://192.168.1.117:8787"   // the LAN IP of the Claude Code machine
#define WEATHER_LATITUDE  13.75f
#define WEATHER_LONGITUDE 100.50f
#define FINNHUB_TOKEN "your-finnhub-token"  // free key from finnhub.io/register — Stock screen only
```

The Finnhub token is a soft credential: it reads public stock quotes and nothing
else. Leaving the placeholder in place just means the Stock screen shows `--` for
every ticker; every other screen works. Register a free key at
[finnhub.io/register](https://finnhub.io/register) if you want live prices. Why
it lives here rather than in `config.h`, and why the device is trusted with it
directly, is [ADR-0004](docs/adr/0004-stock-feed-soft-credential.md).

Find the bridge machine's IP with `ipconfig getifaddr en0` (macOS) or
`hostname -I` (Linux). Prefer an IP over `.local` — mDNS resolution from the
ESP32 is unreliable on networks with client isolation. If the address comes
from DHCP, give the machine a reservation so the firmware does not need
reflashing when the lease changes.

`secrets.h` is gitignored. Everything else — poll intervals, timeouts, the
tracked coins and stock tickers, the timezone, colour thresholds, touch
calibration — is in `firmware/src/config.h`, which is committed. (The stock
symbols themselves are a table in `model.cpp`; the market-hours window is in
`config.h`.)

The timezone is a POSIX TZ string and defaults to Bangkok:

```c
#define CLOCK_TZ "ICT-7"   // note the sign is backwards: -7 means UTC+7
```

### 2. Flash

Plug the board in and flash. Redirect the upload to a file rather than
piping it through `tail` or `grep`:

```bash
cd firmware
~/.local/bin/pio run --target upload > /tmp/upload.log 2>&1; echo "exit=$?"
grep -E "Hash of data|Hard resetting|SUCCESS|FAILED" /tmp/upload.log
```

`pio` writes a couple of hundred lines and an agent shell — or any piped
`tail` — truncates from the end, so it returns the *header* (the dependency
graph and the memory table) and the `SUCCESS` line never arrives. That looks
exactly like a flash that did not finish, and the obvious response is to run it
again. The board takes fifty seconds a time and the second attempt reads no
better than the first. The exit code and the log file settle it in one pass.

First build downloads the ESP32 toolchain and four libraries — a few minutes.
Later builds take about a minute, and the upload itself another fifty seconds.
PlatformIO picks the serial port on its own; if two boards are attached, name
one with `--upload-port /dev/cu.usbserial-XXXX`.

Then watch it come up:

```bash
~/.local/bin/pio device monitor
```

Serial should show the WiFi join, then a fetch per feed. The display lights up
at boot even before the network is up. Weather and Crypto work as soon as the
board has WiFi; the two Claude screens stay at `--` until the bridge is up.

### 3. Set up the quota source

The two Claude screens need a utilization reading, and `bridge/fetch_usage.py`
is what gets it. Give it a claude.ai session key:

```bash
mkdir -p ~/.config/claude-quota
pbpaste > ~/.config/claude-quota/session-key   # or paste into an editor
chmod 600 ~/.config/claude-quota/session-key
```

The value is the `sessionKey` cookie from a browser logged in to claude.ai —
open developer tools, Application → Cookies → `https://claude.ai`, and copy
`sessionKey`. Claude Code's own OAuth login will not work here: the usage
endpoint is cookie authenticated.

The script refuses to read the file if anyone but you can, so the `chmod` is
not optional. Check the setup without printing the key:

```bash
python3 bridge/fetch_usage.py --check
```

Then fetch:

```bash
python3 bridge/fetch_usage.py            # write bridge/usage-cache once
cat bridge/usage-cache
# UTILIZATION=89
# RESETS_AT=2026-07-20T10:59:59Z
# TIMESTAMP=1784557286
# WEEKLY_UTILIZATION=31
# WEEKLY_RESETS_AT=2026-07-22T17:59:59Z
```

The organization is discovered from the account. Pass `--org-id` if you belong
to more than one and the wrong one is picked.

This needs a Python whose TLS trust store is populated. A python.org build on
macOS fails the handshake before sending anything:

```
TLS certificate verification failed. This Python has no CA certificates
```

Use Homebrew's interpreter, or run the `Install Certificates.command` that
ships in the python.org install.

Keep it refreshed. The default is every 60 seconds, matching what Claude Usage
Tracker does against the same endpoint:

```bash
python3 bridge/fetch_usage.py --interval
```

In production this gets its own `launchd` job, separate from the bridge's; see
[step 5](#5-keep-both-processes-running).

A session key is a **full account credential**, not a scoped API key. Read
[Security notes](#security-notes) before putting one on a shared machine.

#### Already running Claude Usage Tracker?

[That app](https://github.com/hamed-elfayome/Claude-Usage-Tracker) writes an
equivalent cache at `~/.claude/.statusline-usage-cache`. Point the bridge at it
and skip this step entirely:

```bash
python3 bridge/quota_bridge.py --cache ~/.claude/.statusline-usage-cache
```

This project does not depend on it. That path and format are internal to
somebody else's app and can change in any release — which is why the default is
a file this repo owns.

To confirm which file is actually in use, read the bridge's startup banner:

```
claude quota bridge listening on http://0.0.0.0:8787/quota
  cache file : /Users/you/esp32-claude-quota/bridge/usage-cache (found)
  history    : /Users/you/esp32-claude-quota/bridge/history.log (56 samples over 7d)
  token scan : off
```

If that path is under `~/.claude`, the app's file is being read. If it is under
`bridge/`, it is not. For proof rather than inspection, write a value nothing
else would produce and see it come back out:

```bash
printf 'UTILIZATION=77\nRESETS_AT=2099-01-01T00:00:00Z\nTIMESTAMP=%s\n' \
  "$(date +%s)" > bridge/usage-cache
python3 bridge/quota_bridge.py --no-tokens --once | grep -A2 '"session"'
# "utilization": 77   → the bridge is reading this repo's file
rm bridge/usage-cache && python3 bridge/fetch_usage.py
```

### 4. Start the bridge

On the machine running Claude Code. Requires a Python with working TLS — on
macOS use Homebrew's, not the python.org build:

```bash
/opt/homebrew/bin/python3.13 bridge/quota_bridge.py
# → http://0.0.0.0:8787/quota
```

Check it, from the same machine and then from another one:

```bash
curl -s http://localhost:8787/quota | python3 -m json.tool
curl -s 'http://localhost:8787/history?days=7' | python3 -m json.tool
curl -s http://<that-machine-ip>:8787/quota      # from a phone or laptop
```

Flags: `--port`, `--host`, `--no-tokens` (skip the jsonl scan, much faster),
`--once` (print one payload and exit), `-v` (log requests).

If the second `curl` hangs, the host firewall is blocking inbound connections.
On macOS, System Settings → Network → Firewall → Options, and allow the Python
binary.

### 5. Keep both processes running

Two jobs, because there are two processes: the fetcher talks to claude.ai on a
slow timer, the bridge serves the LAN. `launchd` keeps them alive across
reboots. Change the paths to match your install.

`~/Library/LaunchAgents/com.local.claude-quota-fetch.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.local.claude-quota-fetch</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/you/esp32-claude-quota/bridge/claude-quota-fetch</string>
    <string>--interval</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/claude-quota-fetch.out</string>
  <key>StandardErrorPath</key><string>/tmp/claude-quota-fetch.err</string>
</dict>
</plist>
```

There is no session key in that file — it is read from
`~/.config/claude-quota/session-key` at run time. Keep it that way; a plist is
world readable.

`~/Library/LaunchAgents/com.local.claude-quota-bridge.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.local.claude-quota-bridge</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/you/esp32-claude-quota/bridge/claude-quota-bridge</string>
    <string>--no-tokens</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/claude-quota-bridge.out</string>
  <key>StandardErrorPath</key><string>/tmp/claude-quota-bridge.err</string>
</dict>
</plist>
```

Both jobs run a small wrapper script rather than the interpreter directly, and
both wrappers exist for their filenames. System Settings → General → Login Items
& Extensions lists background items by the executable `launchd` was pointed at,
not by the job's `Label` — so a plist naming `python3.13` produces two identical
"python3.13 — Item from unidentified developer" rows that cannot be told apart
from each other or from anything else Python on the machine. Pointing at
`claude-quota-fetch` and `claude-quota-bridge` makes that list readable.

The wrappers also supply `-u`. Without it Python buffers stdout when `launchd`
redirects it to a file, and the startup banner — the thing that tells you which
cache file is being read — never reaches the log.

They honour `CLAUDE_QUOTA_PYTHON` if Homebrew's interpreter lives somewhere
else.

```bash
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-fetch.plist
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
launchctl list | grep claude-quota    # second column is the last exit code
launchctl unload ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
```

After editing a plist, `unload` then `load` it. `launchctl kickstart -k`
restarts the process but reuses the definition `launchd` already has cached, so
the edit appears to do nothing — the job comes back with the old arguments and
the only clue is `ps` showing a command line that does not match the file you
just changed.

`KeepAlive` restarts a job if it dies, so stop them through `launchctl` rather
than with `kill` — otherwise they come straight back.

An expired session key does **not** kill the fetcher: it keeps looping and
writes the rejection to `/tmp/claude-quota-fetch.err` each pass, because a
transient network failure should not tear the job down. The key is re-read from
disk on every pass, so pasting a fresh one into
`~/.config/claude-quota/session-key` is the whole fix — no restart, no
`launchctl` dance. The display recovers within one interval.

On Linux the equivalent is a user systemd unit with `Restart=always`; on
Windows, Task Scheduler at logon.

### 6. Optional — regenerate artwork and type

Both are baked into flash and both are checked in, so neither is needed for a
normal build:

```bash
tools/mkicons_lvgl.py --preview   # navbar tiles, mascot, weather glyphs
tools/mkfont_lvgl.py              # the Inter faces (needs npx and fonttools)
```

## How it gets the numbers

The percentages Claude Code shows under `/usage` are not exposed by any public
API. The web app has its own endpoint for them, which is what this reads:

```
GET https://claude.ai/api/organizations/<org-id>/usage
Cookie: sessionKey=<session-key>
```

Two processes, deliberately. `bridge/fetch_usage.py` holds the credential,
makes that request on a 60-second timer, and writes a small KEY=VALUE file:

```
UTILIZATION=89
RESETS_AT=2026-07-20T10:59:59Z
TIMESTAMP=1784557286
WEEKLY_UTILIZATION=31
WEEKLY_RESETS_AT=2026-07-22T17:59:59Z
```

`bridge/quota_bridge.py` reads that file and serves it as JSON on the LAN. It
makes **zero outbound calls** and touches **no credential** — it is a file
reader with an HTTP interface.

The split is the point. The display polls every twenty seconds; claude.ai is
called every sixty. Those two rates are independent, so a second display, or a
much faster poll, changes nothing about what leaves the machine. Folding the
fetch into the bridge would tie them together and put an account credential in
the process listening on the LAN.

The response carries more than this: a `limits` array repeating the same two
windows alongside per-model scoped ones, spend figures, and a spread of null
fields for features the account does not have. Only the account-wide session and
weekly windows are read. The two spellings are both parsed — flat field first,
`limits` entry as a fallback — so either one disappearing is survivable.

The endpoint is undocumented and the session key expires. Both failures land in
the same place: the cache stops being refreshed, ages past thirty minutes, and
the display says so.

If the cache is missing or older than 30 minutes, the bridge reports
`"trusted": false` and every quota figure on the display becomes `--`. It
deliberately does not synthesize a percentage from token counts: Anthropic's
limit accounting is not a plain token sum, so a token-derived percentage would
be a guess wearing the costume of a fact
([ADR-0001](docs/adr/0001-never-derive-utilization-from-tokens.md)).

Weather, crypto and stocks are fetched **directly by the device**, not proxied
through the bridge, so a sleeping Mac costs the two Claude screens and leaves the
other three working ([ADR-0002](docs/adr/0002-mixed-feed-topology.md)). The stock
prices need a Finnhub API token on the device — a soft credential the other two
direct feeds do without; that bend is
[ADR-0004](docs/adr/0004-stock-feed-soft-credential.md). The Stock screen fetches
one ticker per poll (Finnhub quotes one symbol at a time), cycling all five in
about a minute, and shows a market-open badge worked out from the clock in US
Eastern — with DST but without exchange holidays, so it reads "open" on the ten-odd
market holidays a year.

### History

The bridge appends a Sample each time it serves a reading it considers
trustworthy, at most one per three-hour bucket, to `bridge/history.log`:

```
1784557286 58 38      # unix seconds, session %, weekly %
```

Append-only text, one Sample per line, so it survives a bridge restart and can
be read with `tail` or truncated with an editor. Fifty-six Samples cover seven
days, which is what the firmware's fixed-size ring holds. Recording happens on
read rather than on a timer, so nothing runs when nothing is watching.

### Polling

One request is in flight at a time, ever. A TLS handshake peaks near 45KB
against roughly 173KB of free heap, and two overlapping ones do not fit beside
the draw buffers
([ADR-0003](docs/adr/0003-partial-draw-buffer-and-serial-polling.md)).
`net/poller.cpp` picks whichever Feed is most overdue; nothing may fetch
outside it.

| Feed | Interval | Notes |
|---|---|---|
| Bridge — quota | 20s | LAN, free, no limits |
| Bridge — history | 10min | inside the Bridge Feed's slot, on its own timer |
| Weather | 5min | Open-Meteo, 10k requests/day |
| Crypto | 60s | CoinGecko free tier |

Failures back off from 5s to 160s.

## Touch controls

| Gesture | Effect |
|---|---|
| Tap a navbar Slot | Switch Screen |
| Tap above the navbar | Force an immediate refresh of every feed |
| Hold ~1s above the navbar | Blank the display (tap to wake) |

The Setting screen is a readout, not an editor — apart from the backlight
stepper. Changing configuration means reflashing, which is the accepted cost of
never fighting an on-screen keyboard with a stylus.

## Colour thresholds

The Ramp is shared by a window's progress bar and its reset countdown, so the
pair reads as one object.

| Utilization | Colour |
|---|---|
| < 60% | green |
| 60–84% | amber |
| ≥ 85% | red |

## Troubleshooting

**Upload fails to connect** (`Failed to connect to ESP32: No serial data
received`). Hold the BOOT button while the upload starts. If no port shows up
at all under `pio device list`, it is the cable or the CH340 driver.

**Upload starts then dies mid-transfer.** `upload_speed` is already backed off
to 230400 in `platformio.ini` because 460800 makes this board's CH340 drop
bytes. A stubborn unit may need 115200.

**Nothing on the display, but serial shows activity.** Backlight pin. GPIO21 is
correct for the ESP32-2432S028R; other CYD revisions use GPIO27 — change
`-DTFT_BL=21` in `platformio.ini`.

**Colours look inverted.** CYD units ship with two different ILI9341 init
sequences. Swap `-DILI9341_2_DRIVER=1` for `-DILI9341_DRIVER=1` in
`platformio.ini` and rebuild. Note that phone photos of this panel render the
card fill as vivid blue when it is not — trust an eyeball over a photograph,
and test with saturated primaries, since white and black survive a byte swap
unchanged.

**Touches land off-centre.** Resistive panels vary unit to unit. Replace the
four `TOUCH_RAW_*` constants in `config.h` with your own measurements and
reflash.

**WiFi never joins.** The ESP32 has no 5GHz radio. The SSID must be a 2.4GHz
band, and captive-portal networks will not work at all.

**Any figure shows `--`.** That is the Unknown marker, and it means the value
behind it is not trustworthy — never that it is zero. Open the Setting screen:
it names the failing Feed, when it last succeeded, and how stale the quota
reading is, which separates a dead bridge from a dead quota source.

**Only the two Claude screens show `--`, and the Setting screen says the bridge
is fine.** The bridge is healthy and the cache behind it is not. Check it
directly:

```bash
cat bridge/usage-cache                           # missing → nothing writes it
curl -s localhost:8787/quota | grep trustReason  # "stale" | "missing" | "fresh"
```

`missing` means `fetch_usage.py` has never written the cache; `stale` means it
wrote once and stopped. Both are nearly always an expired session key, and a
running fetcher will be logging the rejection every pass without exiting. Paste
a fresh key into the key file and it recovers on the next pass. To see the error
directly: 

```bash
python3 bridge/fetch_usage.py --check    # paths and permissions, no key printed
python3 bridge/fetch_usage.py            # the actual failure
```

**Weekly is `--` but Session is fine.** The cache has `UTILIZATION` without
`WEEKLY_UTILIZATION`, because the weekly window came back under a name
`fetch_usage.py` does not recognise — it warns on stderr when this happens. The
endpoint reports each window twice, as a top-level object and as an entry in a
`limits` array, and the script reads the array when the flat field is gone; a
rename in both at once is what this failure means. `fetch_usage.py --raw` prints
the real payload, and the names go in `WEEKLY_KEYS` / `WEEKLY_KINDS` at the top
of that file.

**A feed says "not tried yet" versus "never ok".** The first means no attempt
has been made since boot; the second means every attempt has failed and the
feed has never worked — a setup problem rather than an outage.

**`bridge: unreachable`.** Firewall is blocking inbound connections, the
machine is asleep, or its IP changed. Verify from another device:
`curl http://<machine-ip>:8787/quota`.

**Weekly Usage says "Not enough history yet".** Fewer than two Samples exist.
Samples land one per three hours, so a fresh install takes a day before the
curve says much. `wc -l bridge/history.log` confirms it.

**The board resets when the serial port is opened.** It does — DTR assertion on
port open is a power-on reset, not a crash. A real crash reports a software
reset with a backtrace.

**Editing `include/lv_conf.h` changes nothing.** PlatformIO caches the LVGL
library build. Run `pio run -t clean` first.

## Security notes

- The ESP32 stores WiFi credentials in plaintext in flash. Anyone with physical
  access can recover them with `esptool read-flash`. `secrets.h` is gitignored;
  keep it that way, and treat a board you give away as a board you gave your
  WiFi password to.
- The bridge serves on the LAN with no authentication. It exposes usage
  percentages and, optionally, token counts — no credentials, no prompt
  content. Still, bind it to a trusted network; do not port-forward it.
- Weather and crypto are fetched without certificate validation, deliberately
  and with reasons, in [ADR-0002](docs/adr/0002-mixed-feed-topology.md). Neither
  request carries a credential or private data.

### About the session key

`fetch_usage.py` is the only component that ever sees it. Neither the firmware
nor the bridge handles it, and it is never logged, echoed, or passed as an
argument where `ps` would show it.

- The `sessionKey` cookie is a **full account credential**, not a scoped API
  key. Anyone holding it can act as you on claude.ai until it expires. Treat it
  the way you would treat the password.
- It lives in `~/.config/claude-quota/session-key`, outside this repo, mode
  600. The fetcher refuses to start if the file is group- or world-readable
  rather than warning and continuing.
- Do not put it in the launchd plist, a shell profile, or a `.env` committed by
  habit. A plist is world readable and an exported variable is visible to every
  process running as you.
- `/api/organizations/<id>/usage` is undocumented. It can change or start
  refusing unfamiliar clients without notice, and nothing here has any claim on
  it continuing to work. When it breaks, the display shows `--` rather than a
  stale number.
</content>
</invoke>
