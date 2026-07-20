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
One origin of displayable data, together with its polling schedule. Three exist:
the Bridge Feed, the Weather Feed and the Crypto Feed. Answers "where does this
number come from".
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

## Interface

**Screen**:
One full-display view owned by exactly one navbar slot. Five exist: Claude,
Weekly Usage, Weather, Crypto, Setting.
_Avoid_: page, view, tab (the tab is the control, not the destination)

**Navbar**:
The persistent strip of five icons along the bottom edge. Icon-only — the labels
from the original design are dropped.
_Avoid_: tab bar, footer, menu

**Slot**:
One of the navbar's five touch targets. Its hit area is deliberately larger than
the icon drawn inside it.
_Avoid_: tab, button, item

**Ramp**:
The mapping from a utilization figure to its display colour — green while
comfortable, amber while warning, red while alarming. Shared by a window's
progress bar and its reset countdown so the pair reads as one object.
_Avoid_: threshold, colour scale, gradient
