# Claude quota monitor — ESP32-2432S028R

A desk display for Claude Code's 5-hour and weekly usage windows, running on the
"Cheap Yellow Display" (ESP32-WROOM-32 + 2.8" 240x320 ILI9341 + XPT2046 touch).

```
┌──────────────────────────────────────┐
│ CLAUDE QUOTA                 ThaiTop │
├──────────────────────────────────────┤
│ 5-HOUR                          89 % │
│ resets 1h 07m                        │
│ ████████████████████████░░░░         │
├──────────────────────────────────────┤
│ WEEKLY                          31 % │
│ resets 2d 08h                        │
│ ████████░░░░░░░░░░░░░░░░░░░░         │
└──────────────────────────────────────┘
  rssi -52dBm | 112M tok/5h | 3s ago
```

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

`bridge/quota_bridge.py` reads that file and serves it as JSON on the LAN. This
means **zero outbound API calls** — the bridge cannot get rate limited no matter
how often the display polls, and no credential is ever read.

If the cache is missing or older than 30 minutes, the bridge reports
`ok: false` and the display shows `--` instead of a percentage. It deliberately
does not synthesize a percentage from token counts: Anthropic's limit accounting
is not a plain token sum, so a token-derived percentage would be a guess wearing
the costume of a fact. Token totals are still reported separately as `tok5h`
for the footer readout.

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
```

Flags: `--port`, `--host`, `--no-tokens` (skip the jsonl scan, much faster),
`--once` (print one payload and exit), `-v` (log requests).

To keep it running across reboots, see "Run the bridge as a service" below.

### 2. Firmware (on the ESP32)

Fill in your network details:

```bash
cp firmware/src/secrets.h.example firmware/src/secrets.h
$EDITOR firmware/src/secrets.h
```

`BRIDGE_URL` should point at the Mac's LAN IP. Prefer an IP over `.local` —
mDNS resolution from the ESP32 is unreliable on networks with client isolation.
If the Mac's IP is assigned by DHCP, give it a reservation on your router so the
firmware does not need reflashing when the lease changes.

Build and flash:

```bash
cd firmware
~/.local/bin/pio run --target upload
~/.local/bin/pio device monitor
```

## Touch controls

| Gesture | Effect |
|---|---|
| Tap | Force an immediate refresh |
| Hold ~1s | Blank the screen (hold again to wake) |

## Colour thresholds

| Usage | Colour |
|---|---|
| < 60% | green |
| 60–84% | amber |
| ≥ 85% | red |

## Run the bridge as a service

`launchd` keeps the bridge alive across logins. Write
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
  <key>StandardErrorPath</key><string>/tmp/claude-quota-bridge.err</string>
</dict>
</plist>
```

```bash
launchctl load ~/Library/LaunchAgents/com.local.claude-quota-bridge.plist
```

## Troubleshooting

**Colours look inverted.** CYD units ship with two different ILI9341 init
sequences. Swap `-DILI9341_2_DRIVER=1` for `-DILI9341_DRIVER=1` in
`platformio.ini` and rebuild.

**Display is blank but serial shows activity.** Backlight pin. GPIO21 is
correct for the ESP32-2432S028R; other CYD revisions use GPIO27.

**Display shows `--` and `bridge: stale`.** The Claude Usage app has not
refreshed `~/.claude/.statusline-usage-cache` recently. Check with
`cat ~/.claude/.statusline-usage-cache` — if `TIMESTAMP` is old, the app that
maintains it is not running.

**`bridge: unreachable`.** macOS firewall is blocking inbound connections, or
the Mac's IP changed. Verify from another device:
`curl http://<mac-ip>:8787/quota`.

**Upload fails to connect.** Hold the BOOT button while the upload starts.

## Security notes

- The ESP32 stores WiFi credentials in plaintext in flash. Anyone with physical
  access can recover them with `esptool read-flash`. `secrets.h` is gitignored.
- The bridge serves on the LAN with no authentication. It exposes usage
  percentages and token counts only — no credentials, no prompt content. Still,
  bind it to a trusted network.
- Unrelated to this project but worth acting on: `~/.claude/fetch-claude-usage.swift`
  contains a live `sk-ant-sid02-...` session key in plaintext. Consider rotating
  it, and keep that file out of any repo or cloud-synced backup.
