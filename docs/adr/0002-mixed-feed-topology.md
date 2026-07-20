# Quota goes through the bridge; weather and crypto do not

The Bridge Feed has to exist — utilization comes from a cookie-authenticated
claude.ai endpoint, and that cookie is a full account credential. An ESP32 holds
whatever it is flashed with in plaintext that `esptool read-flash` recovers, so
the credential stays on the host and the device is handed a percentage.

The Weather and Crypto feeds have no such constraint, so they are fetched
straight from Open-Meteo and CoinGecko by the ESP32 rather than being proxied
through the bridge as well.

## Considered options

Routing everything through the bridge would have kept TLS off the device
entirely, which matters on a board with no PSRAM. It was rejected because it
makes the whole display dependent on one Mac being awake: with the mixed
topology, a sleeping Mac costs the Claude and Weekly Usage screens but leaves
Weather and Crypto working.

## Consequences

The firmware carries a TLS stack it would otherwise not need. A handshake peaks
around 45KB, which is why feeds are polled strictly one at a time — see
ADR-0003. Both external APIs are called without certificate validation
(`setInsecure()`): they carry no credentials and no private data, and a pinned
root CA that silently expires would break the display years later with no
diagnosable cause.
