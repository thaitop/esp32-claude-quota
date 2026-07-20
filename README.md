# Claude quota monitor — ESP32-2432S028R

A desk display for Claude Code's two usage windows, plus a few ambient
readouts, running on the "Cheap Yellow Display" (ESP32-WROOM-32 + 2.8"
240x320 ILI9341 + XPT2046 resistive touch), drawn with LVGL 9.

Five Screens, one per navbar Slot:

| Screen | Shows | Feed |
|---|---|---|
| **Claude** | Session and Weekly utilization, with live reset countdowns | Bridge |
| **Weekly Usage** | The last seven days of weekly utilization, plotted | Bridge |
| **Weather** | Temperature, apparent temperature, conditions, humidity | Weather |
| **Crypto** | One coin's price, 24-hour change, dollar move and volume; BTC/ETH/BNB via a toggle | Crypto |
| **Setting** | Link, per-feed health, quota staleness, heap, uptime | — |

[CONTEXT.md](CONTEXT.md) defines the vocabulary both halves of the system use.
The [ADRs](docs/adr/) record the three decisions that constrain the rest.

## How it gets the numbers

The percentages Claude Code shows under `/usage` are not exposed by any public
API. They are, however, already on disk: the Claude Usage app writes
authoritative server-side utilization to `~/.claude/.statusline-usage-cache`:

```
UTILIZATION=89
RESETS_AT=2026-07-20T10:59:59Z
WEEKLY_UTILIZATION=31
WEEKLY_RESETS_AT=2026-07-22T17:59:59Z
```

`bridge/quota_bridge.py` reads that file and serves it as JSON on the LAN. That
means **zero outbound API calls** for the quota — the bridge cannot be rate
limited no matter how often the display polls, and no credential is ever read.

If the cache is missing or older than 30 minutes, the bridge reports
`"trusted": false` and every quota figure on the display becomes `--`. It
deliberately does not synthesize a percentage from token counts: Anthropic's
limit accounting is not a plain token sum, so a token-derived percentage would
be a guess wearing the costume of a fact
([ADR-0001](docs/adr/0001-never-derive-utilization-from-tokens.md)).

Weather and crypto are fetched **directly by the device**, not proxied through
the bridge, so a sleeping Mac costs the two Claude screens and leaves the other
two working ([ADR-0002](docs/adr/0002-mixed-feed-topology.md)).

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
| Weather | 10min | Open-Meteo, 10k requests/day |
| Crypto | 60s | CoinGecko free tier |

Failures back off from 5s to 160s.

## Setup

### 1. Bridge (on the Mac)

Requires a Python with working TLS — use Homebrew's, not the python.org build:

```bash
/opt/homebrew/bin/python3.13 bridge/quota_bridge.py
# → http://0.0.0.0:8787/quota
```

Check it:

```bash
curl -s http://localhost:8787/quota | python3 -m json.tool
curl -s 'http://localhost:8787/history?days=7' | python3 -m json.tool
```

Flags: `--port`, `--host`, `--no-tokens` (skip the jsonl scan, much faster),
`--once` (print one payload and exit), `-v` (log requests).

### 2. Run the bridge on login

`launchd` keeps it alive across reboots. The job lives at
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
    <string>/opt/homebrew/bin/python3.13</string>
    <string>/Users/thaitop/esp32-claude-quota/bridge/quota_bridge.py</string>
    <string>--no-tokens</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/claude-quota-bridge.out</string>
  <key>StandardErrorPath</key><string>/tmp/claude-quota-bridge.err</string>
</dict>
</plist>
```

```bash
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
launchctl list | grep claude-quota    # second column is the last exit code
launchctl unload ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
```

`KeepAlive` restarts the bridge if it dies, so stop it through `launchctl`
rather than with `kill` — otherwise it comes straight back.

### 3. Firmware (on the ESP32)

`firmware/src/secrets.h` is gitignored and holds everything installation
specific — WiFi credentials, the bridge's LAN address, and the coordinates the
weather is fetched for (personal data, which is why they are not in the
committed tunables):

```c
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."
#define BRIDGE_BASE_URL "http://192.168.1.117:8787"
#define WEATHER_LATITUDE  13.75f
#define WEATHER_LONGITUDE 100.50f
```

Prefer an IP over `.local` — mDNS resolution from the ESP32 is unreliable on
networks with client isolation. If the Mac's address comes from DHCP, give it a
reservation so the firmware does not need reflashing when the lease changes.

Everything else — poll intervals, timeouts, the tracked coin, the colour ramp
thresholds, touch calibration — is in `firmware/src/config.h`, which is
committed.

Build and flash:

```bash
cd firmware
~/.local/bin/pio run --target upload
~/.local/bin/pio device monitor
```

Redirect the upload to a file rather than piping it through `tail` or `grep`:

```bash
~/.local/bin/pio run --target upload > /tmp/upload.log 2>&1; echo "exit=$?"
grep -E "Hash of data|Hard resetting|SUCCESS|FAILED" /tmp/upload.log
```

`pio` writes a couple of hundred lines and an agent shell truncates from the
end, so a piped `tail` returns the *header* — the dependency graph and the
memory table — and the `SUCCESS` line never arrives. That looks exactly like a
flash that did not finish, and the obvious response is to run it again. The
board takes fifty seconds a time and the second attempt reads no better than the
first. The exit code and the log file settle it in one pass.

The Setting screen is a readout, not an editor. Changing configuration means
reflashing, which is the accepted cost of never fighting an on-screen keyboard
with a stylus.

### 4. Regenerating the artwork and type

Both are baked into flash and both are checked in, so neither is needed for a
normal build:

```bash
tools/mkicons_lvgl.py --preview   # navbar tiles, mascot, weather glyphs
tools/mkfont_lvgl.py              # the Inter faces (needs npx and fonttools)
```

## Touch controls

| Gesture | Effect |
|---|---|
| Tap a navbar Slot | Switch Screen |
| Tap above the navbar | Force an immediate refresh of every feed |
| Hold ~1s above the navbar | Blank the display (tap to wake) |

## Colour thresholds

The Ramp is shared by a window's progress bar and its reset countdown, so the
pair reads as one object.

| Utilization | Colour |
|---|---|
| < 60% | green |
| 60–84% | amber |
| ≥ 85% | red |

## Troubleshooting

**Any figure shows `--`.** That is the Unknown marker, and it means the value
behind it is not trustworthy — never that it is zero. Open the Setting screen:
it names the failing Feed, when it last succeeded, and how stale the quota
reading is, which separates a dead bridge from a dead Claude Usage app.

**A feed says "not tried yet" versus "never ok".** The first means no attempt
has been made since boot; the second means every attempt has failed and the
feed has never worked — a setup problem rather than an outage.

**Weekly Usage says "Not enough history yet".** Fewer than two Samples exist.
Samples land one per three hours, so a fresh install takes a day before the
curve says much. `wc -l bridge/history.log` confirms it.

**Colours look inverted.** CYD units ship with two different ILI9341 init
sequences. Swap `-DILI9341_2_DRIVER=1` for `-DILI9341_DRIVER=1` in
`platformio.ini` and rebuild. Note that phone photos of this panel render the
card fill as vivid blue when it is not — trust an eyeball over a photograph,
and test with saturated primaries, since white and black survive a byte swap
unchanged.

**Display is blank but serial shows activity.** Backlight pin. GPIO21 is
correct for the ESP32-2432S028R; other CYD revisions use GPIO27.

**`bridge: unreachable`.** macOS firewall is blocking inbound connections, or
the Mac's IP changed. Verify from another device:
`curl http://<mac-ip>:8787/quota`.

**Upload fails to connect.** Hold the BOOT button while the upload starts.

**The board resets when the serial port is opened.** It does — DTR assertion on
port open is a power-on reset, not a crash. A real crash reports a software
reset with a backtrace.

## Security notes

- The ESP32 stores WiFi credentials in plaintext in flash. Anyone with physical
  access can recover them with `esptool read-flash`. `secrets.h` is gitignored.
- The bridge serves on the LAN with no authentication. It exposes usage
  percentages and, optionally, token counts — no credentials, no prompt
  content. Still, bind it to a trusted network.
- Weather and crypto are fetched without certificate validation, deliberately
  and with reasons, in [ADR-0002](docs/adr/0002-mixed-feed-topology.md). Neither
  request carries a credential or private data.
- Unrelated to this project but worth acting on: `~/.claude/fetch-claude-usage.swift`
  contains a live `sk-ant-sid02-...` session key in plaintext. Consider rotating
  it, and keep that file out of any repo or cloud-synced backup.
