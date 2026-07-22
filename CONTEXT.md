# Claude Quota Monitor

A desk display that shows how much of Claude Code's rate-limit allowance has
been spent, alongside a few ambient readouts. The vocabulary below is shared by
both halves of the system — the Python bridge on the Mac and the C++ firmware on
the ESP32 — so that a concept has one name on both sides of the wire.

## Quota

**Quota Window**:
A rolling period over which Anthropic meters usage against a limit. Two exist:
the Session Window and the Weekly Window.
_Avoid_: quota, bucket, period, limit

**Session Window**:
The shorter of the two quota windows, roughly five hours long. Labelled
"Current" on screen.
_Avoid_: five, 5h, current window, short window

**Weekly Window**:
The seven-day quota window.
_Avoid_: week, long window

**Utilization**:
How much of a quota window has been consumed, as a whole-number percentage.
Authoritative only when it comes from Anthropic's own accounting — never
computed here.
_Avoid_: usage, percentage, pct, consumption

**Reset**:
The instant a quota window's utilization returns to zero. Displayed as time
remaining, never as a wall-clock timestamp.
_Avoid_: expiry, refresh, rollover

**Utilization Cache**:
The `bridge/usage-cache` file, written by `fetch_usage.py` and read by the
bridge, holding server-side utilization figures. The only trustworthy origin of
utilization in this system.
_Avoid_: statusline file, usage file

**Token Total**:
The sum of billable tokens observed in Claude Code's transcripts over a window.
Reported alongside utilization but never used to derive it — Anthropic's limit
accounting is not a plain token sum.
_Avoid_: usage, cost, spend

## Data flow

**Feed**:
One origin of displayable data, together with its polling schedule. Four exist:
the Bridge Feed, the Weather Feed, the Crypto Feed and the Stock Feed. Answers
"where does this number come from". The Stock Feed is the one that fetches a
single item per pass — one ticker at a time — rather than its whole screen in
one request, since Finnhub's quote endpoint takes one symbol.
_Avoid_: source, provider, backend, API

**Bridge**:
The HTTP service on the Mac that reads the Utilization Cache and serves it to
the firmware over the LAN. It exists so no credential and no filesystem access
is ever needed on the device.
_Avoid_: server, proxy, agent

**Snapshot**:
One feed's most recent successfully parsed values, held in memory on the device.
What a screen renders.
_Avoid_: state, data, payload, cache

**Sample**:
A single utilization reading recorded with its timestamp, appended by the Bridge
over time. The Weekly Usage screen plots a series of these.
_Avoid_: datapoint, history entry, record

**Trust**:
Whether a snapshot's values are fit to display. Distinct from whether the last
fetch succeeded: a snapshot can come from a healthy HTTP response and still be
untrustworthy because the Utilization Cache behind it went stale.
_Avoid_: ok, valid, healthy

**Staleness**:
The age of the underlying reading, measured from when it was written rather than
when it was fetched. A snapshot past its staleness limit loses Trust.
_Avoid_: age, expiry, TTL

**Fetch Outcome**:
Why the last attempt to refresh a feed succeeded or failed — unreachable,
malformed, rejected, or fine. A transport-level fact, never shown as a
utilization value.
_Avoid_: source, status, error

**Unknown**:
The displayed value when Trust is absent. Rendered as `--`. Distinct from zero,
and never filled in with the last known figure.
_Avoid_: null, empty, N/A

**Ticker**:
One tracked stock, named by its exchange symbol (AAPL, NVDA, …). The symbol the
Stock Feed asks Finnhub about and the label the row prints are the same string —
the counterpart to a Coin on the Crypto side.
_Avoid_: stock, symbol (as a separate concept), equity

**Market Session**:
Whether the US regular trading session is open right now — Open, Closed, or
Unknown. Derived from the clock, never fetched, and never confused with Trust: a
price read while the market is Closed is still trustworthy, it just is not
moving. Drives the Stock screen's badge.
_Avoid_: market hours, open/closed flag, trading status

## Interface

**Screen**:
One full-display view owned by exactly one navbar slot. Six exist: Claude,
Weekly Usage, Weather, Crypto, Stock, Setting.
_Avoid_: page, view, tab (the tab is the control, not the destination)

**Navbar**:
The persistent strip of six icons along the bottom edge. Icon-only — the labels
from the original design are dropped.
_Avoid_: tab bar, footer, menu

**Slot**:
One of the navbar's six touch targets. Its hit area is deliberately larger than
the icon drawn inside it.
_Avoid_: tab, button, item

**Ramp**:
The mapping from a utilization figure to its display colour — green while
comfortable, amber while warning, red while alarming. Shared by a window's
progress bar and its reset countdown so the pair reads as one object.
_Avoid_: threshold, colour scale, gradient

## Configuration

**Setting** (the tunables, not the Screen):
The installation values that describe *this* display rather than repo-wide
behaviour: the Weather location and its two timezones, the tracked coin ids, the
tracked stock symbols. Held in NVS by `config_store`, defaulting to the
`config.h`/`secrets.h` constants. Distinct from the Setting Screen, which shows
health and now also carries the button into Config Mode.
_Avoid_: config, preferences, options

**Config Mode**:
The state the device enters from the Setting Screen's Config button: feeds
stopped, a LAN web server up, an address and a PIN on the panel. A browser edits
the Settings and the device reboots onto them. The one editor in an otherwise
reflash-to-change system — see ADR-0005.
_Avoid_: setup mode, admin, portal (the code calls the object a portal; the
state the user is in is Config Mode)

**Catalog** (Config Mode):
The curated set of coins and stocks the pickers offer. Coins and stocks are
chosen from it, not typed, because their logos are baked into the firmware and
keyed by id/symbol — a catalogued item has (or can have) a matching mark. Held
in `config_store`.
_Avoid_: list, options, presets

**Validation** (Config Mode):
Confirming the one free-text field — the city — against Open-Meteo geocoding
before it is kept. Runs with the feeds stopped, one fetch at a time, so it never
breaks the one-connection rule (ADR-0003/0005). Coins and stocks need none: a
catalog pick is known-good. A city that fails keeps the previous location.
_Avoid_: check, verify, lookup
