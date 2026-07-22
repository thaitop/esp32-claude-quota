# Config Mode edits the installation settings over the LAN

The location the Weather Feed asks about, the coins the Crypto Feed tracks and
the symbols the Stock Feed tracks were compile-time constants in `config.h` and
`secrets.h`. Changing any of them meant editing a file and reflashing — the same
accepted cost the Setting screen's header still names for the rest of the
tunables.

Config Mode is the exception carved out for the three settings a person
plausibly changes without a toolchain in reach: where the weather is, which
coins, which stocks. Holding the **Config** button on the Setting screen stops
the feeds, stands up a plain HTTP server on the LAN, and shows an address and a
PIN on the panel. A browser edits the settings and the device reboots onto the
new set. The settings live in NVS (`net/config_store`), with the old
`config.h`/`secrets.h` values standing in as factory defaults.

The coins and stocks are **picked from a catalog**, not typed: their logos are
bitmaps compiled into the firmware and keyed by id/symbol, so an arbitrary typed
id would render the wrong logo or a fallback. The catalog carries only strings,
so it lists the full intended set even before every entry's artwork is baked —
an unbaked entry draws the generic glyph, never a mislabelled one (see
`ui/logos`). The city is the one free-text field, so it is the one thing
validated on the wire.

This bends two earlier decisions, and the bending is the point of writing it
down.

## It does not break ADR-0003 (one connection in flight)

ADR-0003 serialises the feeds because two TLS handshakes do not fit beside the
draw buffers on this board. Config Mode adds a fourth heap consumer — the web
server — and, worse, its validations are themselves outbound TLS fetches (to
confirm a city against Open-Meteo geocoding, a coin id against CoinGecko, a
symbol against Finnhub). Two connections at once is exactly what ADR-0003
forbids.

The rule is kept by construction, not by hope:

- **The feeds are stopped for the whole of Config Mode.** The main loop takes a
  separate path while `configPortalActive()`, never calling `net::service()`. No
  feed fetch can exist to overlap a validation.
- **A validation never overlaps the browser request that asked for it.** The
  handler for `/validate` queues the job and answers `202 pending` immediately;
  the fetch runs later, in `configPortalService()` *between* HTTP clients, while
  the browser polls `/status`. So during the ~45KB TLS peak the inbound side is
  an idle socket, not a request holding server buffers open. This is the reason
  the flow polls instead of blocking — a blocking `/validate` would hold the
  inbound request open across the outbound handshake, the overlap avoided here.

The static cost is a WebServer and a page buffer only while Config Mode is up;
the feeds' own working set is not live at the same time. As with every change
that touches the heap, the number that governs is `lv_mem_monitor`'s
free-biggest, read off the device with Config Mode active and a validation in
flight — not the host, which has no ceiling.

## It respects ADR-0002 (mixed feed topology)

No account credential is exposed. The values Config Mode writes are a location
and public market identifiers. The one credential in play is Finnhub's soft
token (ADR-0004), used to validate a symbol the same way the Stock Feed already
sends it; it is never shown in the UI. The claude.ai cookie stays where ADR-0002
put it — on the Mac, never on the device, never near this server.

## Access is gated by a PIN shown on the panel

The server has no TLS and no account behind it, and it is reachable by anyone on
the LAN for as long as it is up. That window is already narrow: Config Mode is
entered by a physical tap and ends in a reboot. A four-digit PIN from the
hardware RNG, shown only on the panel, gates it further — only someone in front
of the device can read it. This is stricter than the rest of the system's trust
model (the bridge has no auth; the direct feeds use `setInsecure()`), and
deliberately so: the values here include the home coordinates, which the project
treats as personal data (`secrets.h`'s own comment), and the write is
persistent. The PIN costs one line on the panel and one field in the form.

## Applying is a reboot, not a live swap

Saving commits the draft to NVS and reboots; boot reloads the settings cleanly.
A live swap would have to invalidate the running snapshots — a price fetched for
a coin that is no longer tracked is a wrong number wearing a new label — and
reset the feed cursors mid-rotation. The reboot makes the new set the only set
any feed ever sees, for a cost of a couple of seconds on an action taken rarely.

## Timezone follows the location, within the clock's existing limits

Changing the city changes the zone. The Weather Feed's `timezone` takes the IANA
name Open-Meteo geocoding returns directly. The header clock cannot: the device
has no zone database to turn `Europe/London` into an offset with DST rules, and
`timesync.cpp`'s shorthand parser deliberately handles no DST. So Config Mode
prefills the clock offset from the zone's *current* `utc_offset_seconds` (a
second, cheap Open-Meteo call) as a `UTC+N` string the user can override — the
same fixed-offset, no-DST limitation the parser already documents, surfaced
rather than papered over. A DST zone still wants a raw POSIX string typed by
hand, exactly as before.

## Consequences

- `config.h`'s coin/weather path formats became runtime templates: the crypto
  request and the weather request are built with `snprintf` from the stored
  ids/coordinates, not concatenated as literals. `coinId()` and friends moved
  from `model.cpp` (which stayed host-compilable) to `config_store.cpp` (which
  reads NVS and did not), but a single table still backs both the request and
  the label — the invariant `model.cpp` guarded, now holding across a runtime
  value.
- The tracked-item **count** is still fixed (three coins, five stocks): the
  `Coin`/`Ticker` enums remain the slot indices, only their contents are
  editable. Adding a slot is still a firmware change.
- Logos are looked up by id/symbol (`ui/logos`), not by slot: any catalogued
  coin or stock can sit in any slot and still draw its own mark, or the generic
  glyph if its artwork is not baked yet — never a wrong one. Every stock mark is
  a monochrome silhouette recoloured to the theme's TEXT, which retired the
  old Apple-only special case.
- Growing the catalog is three coordinated edits plus art: a row in
  `config_store`'s catalog (the string, so the picker offers it), a row in
  `ui/logos` (the baked mark), and a `MANIFEST` entry with an SVG in
  `tools/mkicons` (regenerated with `resvg-py`/`Pillow`). The catalog can list an
  entry before its logo exists; the logo simply lands later.
