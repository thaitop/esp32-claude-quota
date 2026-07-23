# WiFi Setup: an AP-mode fallback for changing the network on the device

The WiFi credentials were compile-time constants in `secrets.h`
(`WIFI_SSID`/`WIFI_PASSWORD`). Changing the network the display joins meant an
edit and a reflash — the toolchain-in-reach cost the Setting screen's header
still names for the rest of the tunables. That is the one setting most likely to
change away from the toolchain: the display moves to a new place, the router is
replaced, the password rotates, or a board flashed with placeholder creds is
handed to someone whose network the flasher does not know.

Config Mode (ADR-0005) cannot be the answer. Reaching its LAN server needs the
radio already up, so it can edit weather/coins/stocks but not the WiFi it is
itself running on. The device has to be reachable *before* it has a network —
which means it has to make one.

## The device becomes its own access point

When `connectWifi()` fails to associate inside `WIFI_CONNECT_TIMEOUT_MS`, or the
screen is held during boot, the radio switches from station to `WIFI_AP` and
brings up a WPA2 access point named `ClaudeQuota-XXXX` (the low MAC bytes, so two
on a desk do not collide). A phone joins it, opens `http://192.168.4.1/`, types
the real SSID and password, and the device commits them to NVS and reboots onto
the new network. `net/wifi_portal` owns the AP and the server; the panel overlay
that shows the AP name, password and URL is built by the caller in `main.cpp` —
the same net/ui seam `recordLink()` and Config Mode's overlay cross.

Two doors, both before the feeds are registered:

- **Association failed.** A network that is gone or a wrong password lands here,
  so it is fixable without a reflash. The cost is that a transient outage at boot
  (a router slower to come up than the ESP after a power blip) also lands here,
  and an unattended display then waits on the AP screen until a person reboots
  it. This is the standard WiFiManager trade and is accepted: the alternative,
  retrying forever, strands a genuinely-misconfigured device with no on-device
  way out.
- **Screen held at boot.** The present-user override, for changing the network
  even when the saved one still connects. Sampled across a short window with a
  continuous-press requirement, so a settling glitch does not trip it.

## It is not Config Mode, and deliberately simpler

Config Mode's whole complexity is validating free-text fields against live APIs
without ever opening two TLS connections at once (ADR-0003/0005). WiFi Setup has
none of that surface:

- **Nothing to validate on the wire.** In AP mode there is no uplink, so there
  is no "check this against Open-Meteo" step. WPA2 either accepts the credentials
  on the next boot or the device lands back in this same portal — the recovery
  path, not a failure to guard against at save time. `setup()` returns before
  registering any feed, because there is nothing to fetch with no uplink.
- **No feed can overlap anything.** The feeds are never even started, so
  ADR-0003's one-connection rule is kept by there being only ever one connection
  possible. The static cost is a WebServer and a page buffer with the feeds' own
  working set never live beside it — lighter than Config Mode, which has to fit
  the same server *plus* a validation fetch. The overlay uses plain labels, not
  the Claude screen's gradient bar, so it adds no LVGL layer pressure. As always,
  the number that governs is `lv_mem_monitor`'s free-biggest read off the device;
  measured with the AP up it sits near boot-idle (~199KB heap free).

## Access is gated by the WPA2 password, not a separate PIN

Config Mode's server sits on a network the device does not control, so it adds a
panel PIN on top. WiFi Setup owns the network: joining the AP at all requires the
8-digit WPA2 password shown only on the panel, so being on the AP *is* the gate.
A second PIN inside would be redundant. The password is 8 digits from the
hardware RNG — WPA2's minimum length — regenerated each time the portal opens.

The one value that crosses this link is the user's home WiFi password, typed into
the form. It crosses inside the WPA2-encrypted AP session, readable only by
something that already holds the panel-shown key. This is why the AP is
WPA2-protected rather than open: an open AP would expose that password to anyone
in radio range during the setup window.

## Applying is a reboot, matching ADR-0005

Saving commits to NVS and reboots; boot reloads the WiFi and tries to join it.
No live radio-mode swap from AP back to STA mid-session, no half-applied state —
the same reasoning ADR-0005 gives for Config Mode, for the same cost of a couple
of seconds on a rare action.

## Consequences

- `WIFI_SSID`/`WIFI_PASSWORD` in `secrets.h` are now **factory defaults**, not
  the live values: the first boot of a fresh or NVS-erased device tries them,
  and after WiFi Setup writes NVS, editing them changes nothing until a reflash
  or NVS erase — exactly the status ADR-0005 already gave the location and the
  tracked coins/stocks. The Finnhub token stays compile-time (no radio-independent
  door makes sense for it).
- `Settings` grew `wifiSsid`/`wifiPass`, so `SETTINGS_VERSION` went 1 → 2. A blob
  written by a v1 build is a version mismatch and is ignored, so **the first flash
  carrying this change resets every Config Mode setting to defaults** — a device
  that had a customised location/coins/stocks comes up on the factory set and
  must be reconfigured once. This is the safe direction the version gate exists
  for (a stale field shows Unknown, it never points a request somewhere
  unexpected); it is a one-time cost at this upgrade, not ongoing.
- `configCommitWifi()` writes only the two WiFi fields onto the live copy of the
  rest, so provisioning in AP mode — where the portal has none of the other
  settings' validation context — cannot clobber a location or a coin.
- The Setting screen's SSID now reads from NVS (`configWifiSsid()`) rather than
  the `WIFI_SSID` macro, so it reflects the network actually joined.
