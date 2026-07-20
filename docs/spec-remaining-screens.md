# Spec — the four screens the navbar promises

Status: ready to build. Steps 1 through 9a of the LVGL rewrite are done and on
the `lvgl-rewrite` branch; this spec covers what is left.

Read [CONTEXT.md](../CONTEXT.md) first — the vocabulary below is defined there
and used exactly. The three [ADRs](./adr/) constrain several decisions and are
referenced where they bite.

## Problem Statement

The display shows the two Claude quota windows and nothing else. The navbar has
five Slots, but four of them lead to a page that says the screen has not been
built yet. Someone glancing at the desk can see how much of their Claude
allowance is gone, but not how fast they burned it this week, and still has to
reach for a phone to check the weather or a price.

Beyond that, when the display does go wrong there is nowhere to look. If the
figures stop updating, the only way to find out whether the WiFi dropped, the
bridge died, or the Utilization Cache went stale is to plug in a USB cable and
read the serial console.

## Solution

Fill in the four remaining Screens.

**Weekly Usage** plots the last seven days of utilization, so the trend is
visible rather than just the current figure. **Weather** and **Crypto** each
show one ambient readout fetched straight from a public API. **Setting** shows
what the device knows about its own health — link, feeds, heap, uptime — so a
display that has stopped working can be diagnosed by tapping a Slot instead of
by attaching a cable.

## User Stories

1. As someone watching my Claude usage, I want the Weekly Usage screen to plot
   the last seven days, so that I can see whether this week is unusual.
2. As someone watching my Claude usage, I want each plotted Sample to show the
   weekly utilization at that moment, so that the curve reads as one continuous
   climb toward the limit rather than as disconnected daily totals.
3. As someone watching my Claude usage, I want the current figure shown
   alongside the chart, so that I do not have to switch back to the Claude
   screen to read the number.
4. As someone watching my Claude usage, I want a chart with too few Samples to
   say so, so that a nearly empty plot in the first days after setup does not
   look like a bug.
5. As someone watching my Claude usage, I want the chart to keep its shape when
   the Bridge Feed is briefly unreachable, so that a dropped poll does not blank
   a week of history.
6. As someone who reinstalled or moved the bridge, I want history to survive a
   bridge restart, so that a week of Samples is not lost to a reboot.
7. As someone at my desk, I want the Weather screen to show the current
   temperature where the display is, so that I can decide what to wear without
   picking up a phone.
8. As someone at my desk, I want apparent temperature as well as actual, so that
   I know whether it will feel worse than the number suggests.
9. As someone at my desk, I want a glyph matching the current conditions, so
   that the screen is readable at a glance from across the room.
10. As someone at my desk, I want the Weather screen to distinguish day from
    night, so that a clear night is not drawn with a sun.
11. As someone holding crypto, I want the Crypto screen to show the current
    price, so that I can see where the market is without opening an app.
12. As someone holding crypto, I want the 24-hour change shown as a signed
    percentage in a colour matching its direction, so that the direction is
    readable before the digits are.
12a. As someone holding crypto, I want the 24-hour move in dollars beside the
    percentage, so that I know what the move was worth and not only how far it
    went.
12b. As someone holding crypto, I want the 24-hour trading volume, so that I
    know how much conviction was behind the move.
12c. As someone holding more than one coin, I want to step between BTC, ETH and
    BNB from the screen itself, so that I do not have to reflash the device to
    look at a different one.
13. As someone whose display sits on a desk all day, I want each feed polled no
    more often than its provider allows, so that the device is never rate
    limited or blocked.
14. As someone whose Mac sleeps overnight, I want Weather and Crypto to keep
    working while the bridge is unreachable, so that only the two Claude screens
    degrade — the reason the feed topology is mixed at all (ADR-0002).
15. As someone diagnosing a stuck display, I want the Setting screen to show
    whether WiFi is associated and at what signal strength, so that I can tell a
    link problem from a data problem.
16. As someone diagnosing a stuck display, I want each Feed's last Fetch Outcome
    and how long ago it last succeeded, so that I can tell which feed is broken
    rather than guessing.
17. As someone diagnosing a stuck display, I want the Staleness of the current
    quota reading, so that I can tell a dead bridge from a dead Claude Usage
    app.
18. As someone diagnosing a stuck display, I want free heap and uptime, so that
    I can spot a slow leak or an unexplained reboot without a serial cable.
19. As someone diagnosing a stuck display, I want the device's own IP address
    and the bridge URL it is using, so that I can test the same request from
    another machine.
20. As someone who has set this up once, I want the Setting screen to be a
    readout and not an editor, so that I am never fighting a keyboard on a
    resistive panel.
21. As anyone looking at the display, I want a feed that has never succeeded to
    look different from one that has gone stale, so that "not set up yet" is not
    confused with "broken now".
22. As anyone looking at the display, I want any Screen with no trustworthy data
    to show the Unknown marker, so that no screen ever shows a figure it cannot
    stand behind (ADR-0001).
23. As anyone looking at the display, I want every Screen to use the same type,
    palette and Ramp as the Claude screen, so that the device reads as one
    product.
24. As anyone using the display, I want tapping a card to force an immediate
    refresh on whichever Screen I am on, so that I do not wait out a ten-minute
    weather interval to see a change.
25. As anyone using the display, I want holding anywhere above the navbar to
    blank the screen, so that the display can be silenced at night from any
    Screen.
26. As anyone using the display, I want switching Screens to be instant, so that
    the navbar feels like hardware rather than like a web page.
27. As the person maintaining this, I want the device to survive the bridge
    being down for hours, so that I come back to a working display rather than
    to a device that has wedged itself.
28. As the person maintaining this, I want the bridge to start on login, so that
    the display works after a reboot without my intervention.
29. As the person maintaining this, I want the README to describe the system as
    it now is, so that setting it up again in a year does not mean reading the
    source.

## Implementation Decisions

### Feeds and scheduling

- Three Feeds exist: `Bridge`, `Weather`, `Crypto`. Weekly Usage draws from the
  Bridge Feed rather than becoming a fourth — it is the same origin and the same
  schedule family, and a separate Feed entry would let the two drift apart.
- Every fetch goes through the existing poller. It already serialises requests
  and picks the most overdue Feed. **Nothing may call a fetch directly** — that
  is the mechanism enforcing ADR-0003, and a feed that bypasses it reintroduces
  the overlapping-TLS failure the ADR exists to prevent.
- Poll intervals are already in the tunables: quota 20s, history 10min, weather
  10min, crypto 60s, with failures backing off from 5s to 160s.
- Weather and Crypto are fetched over TLS with certificate validation disabled,
  per ADR-0002. Each connection must be torn down as soon as its response is
  parsed; the TLS client must not be held open between polls.

### History

- The bridge gains a history endpoint returning an array of Samples over a
  requested number of days.
- The bridge records a Sample each time it serves a quota reading it considers
  trustworthy, appending to a file under the bridge's own directory. Recording
  on read rather than on a timer means no second scheduler and no daemon
  behaviour when nothing is watching.
- Samples are deduplicated to at most one per resolution bucket, so a display
  polling every 20 seconds does not write 4,000 rows a day.
- Resolution is three-hourly, giving 56 Samples over seven days — the capacity
  the firmware's history ring is already sized for.
- The history file is append-only text, one Sample per line, so it survives a
  bridge restart and can be inspected or truncated by hand.
- A Sample carries both session and weekly utilization even though only weekly
  is plotted today, because the cost is a few bytes and regenerating history is
  impossible after the fact.
- The firmware keeps history in the existing fixed-size array in the model. No
  dynamic allocation: the ring is 452 bytes and sized at compile time.

### Screens

- Each Screen owns a build function that populates its page and an update
  function that takes the model. This mirrors the Claude screen and keeps the
  ui layer ignorant of HTTP and the net layer ignorant of LVGL.
- Update functions must remain change-detecting. The main loop calls them every
  second for the countdown, and redrawing unchanged widgets on a 320x240 panel
  over SPI is visible as stutter.
- Weekly Usage uses the chart widget already enabled in the LVGL config. No new
  widget classes may be enabled without a matching reason — the widget list is
  pruned deliberately and each addition costs flash.
- Weather condition glyphs are generated into flash by the icon generator
  alongside the existing artwork, in the same RGB565A8 format. WMO weather codes
  are collapsed into a small set of conditions rather than one glyph per code.
- Setting is a readout only. Configuration stays in the committed tunables file
  and the gitignored secrets file; changing it means reflashing, which is the
  accepted cost of not building a text-entry UI for a stylus.
- The Setting screen reads the per-Feed status already carried in the model.
  Nothing new needs to be recorded to build it.

### Shared behaviour

- Trust is per-Feed. One Feed failing must not blank another Feed's Screen.
- A Feed that has never succeeded must render differently from one whose data
  has aged out. The distinction already exists in the model as the "never
  attempted" outcome versus a failure after a prior success.
- The Unknown marker stays `--` everywhere, and no Screen may substitute a last
  known value for a missing one.
- Colour and type come from the theme and the generated Inter faces. No screen
  may introduce a literal colour or call up a font that is not already
  generated.

### Deployment

- The bridge gets a launchd job so it starts on login. The README already
  contains the plist; it has not been installed.
- The bridge and the firmware share a wire vocabulary and must be updated
  together. There is no version negotiation and none is planned for a two-node
  system on one LAN.

## Testing Decisions

**No automated tests.** Decided explicitly: verification is by flashing the
board and confirming behaviour on the panel, as it has been for every step so
far.

This is a real trade-off and worth naming. The parsers for weather and crypto
are the part of this work most likely to break silently — a renamed field or a
null where a number was expected produces Unknown rather than an error, which
is the correct behaviour and also an invisible one. Nothing will catch that
except noticing the screen is blank.

What the build does have, and what should be extended rather than dropped:

- The firmware's model and formatting headers compile on the host against
  `<cstdint>`, not `<Arduino.h>`. Keep it that way. It costs nothing and it is
  what would make tests possible later without restructuring.
- The bridge's payload builder can be exercised from the command line without
  starting a server, which is how the wire format was checked when it changed.
- Layout failures that produce an invisible widget rather than an error are
  worth a runtime tripwire on the serial console. There is precedent: a
  zero-width pill cost a long debugging session, and the check that would have
  caught it in seconds is three lines.

Verification for each Screen means: the figures match the source of truth
checked independently; free heap does not drift across polls; and the Screen
degrades to Unknown rather than to a stale figure when its Feed is stopped.

## Out of Scope

- The SDL simulator. Deliberately skipped: flashing takes about 25 seconds,
  which turned out to be fast enough to iterate against directly.
- Editing configuration on the device, whether by on-screen keyboard or by a
  web interface. Coordinates and the tracked coin change about once a year.
- Multiple coins, multiple locations, forecasts beyond current conditions, or
  any per-screen settings.
- Detail Screens behind the quota cards. The design's chevron was removed
  precisely because nothing opens.
- Certificate pinning for the external feeds — rejected in ADR-0002, with
  reasons.
- Deriving utilization from token counts — rejected in ADR-0001, with reasons.
- Any change to the draw buffer size or to the one-request-at-a-time rule
  without revisiting ADR-0003 first.

## Further Notes

Traps already paid for. All four cost real time; none produced an error
message.

- **LVGL config silently ignored.** The config file must use an `LV_CONF_H`
  include guard rather than `#pragma once`, and must be reached by an absolute
  path with the quotes escaped through to the compiler. Merely placing it on the
  include path leaves every setting at its default while the build still
  succeeds. Verify by symbol, not by exit status: grep the map file for a widget
  that should have been disabled.
- **Content sizing collapses.** Sizing a container to its content resolved to
  zero width, both with a centred child and with flex. The container renders as
  an invisible sliver and clips its own text. Measure and set the width
  explicitly.
- **Photographs lie about dark colours.** Phone photos of this panel render the
  card fill and the progress track as vivid blue. The panel is correct; the
  camera is not. Ask for an eyeball check before investigating a colour bug.
  Note also that white and black survive a byte swap unchanged, so a screen made
  only of those proves nothing — test with saturated primaries.
- **Opening the serial port resets the board.** DTR assertion on port open
  produces a power-on reset, not a crash. A real crash reports a software reset
  with a backtrace.

Headroom as measured on the board after the bridge feed landed: about 173KB of
heap free, against the 45KB a TLS handshake peaks at. Both external feeds still
have to fit inside that, one at a time.
