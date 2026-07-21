# The Stock Feed puts a soft credential on the device

The Stock screen's prices come from Finnhub's `/quote`, fetched directly by the
ESP32 the way Weather and Crypto are. Unlike those two, Finnhub requires an API
token on every request. That token is a credential, and ADR-0002 drew its line
at exactly this: credentials stay on the host, the device is handed data.

The line is drawn again here, but on a different axis — not "credential vs. no
credential" but "what the credential can do". Finnhub's is a soft credential: it
reads public market data and nothing else, it authenticates no account and
spends no money, and a leak costs the ability to look up stock quotes. The
claude.ai cookie ADR-0002 kept off the device is a full account credential; the
two are not the same kind of secret, and the reasoning that quarantines one does
not obviously bind the other.

So the token is treated like the WiFi password and the home coordinates already
in `secrets.h`: it identifies this installation, it is kept out of the repo, and
it is compiled into a flash image that `esptool read-flash` can recover. The
request goes straight to Finnhub over TLS with `setInsecure()`, the same as the
other direct feeds.

## Considered options

**Proxy the token through the bridge**, the way the quota cookie is, would keep
it off the device. Rejected for the reason ADR-0002 gives for not proxying
Weather and Crypto: it makes the Stock screen depend on the Mac being awake, and
folds a market-data key into the one process that already holds the account
credential — growing that process's blast radius to save a soft key from a
threat (flash extraction, LAN sniffing) that only reaches someone already inside
the network or holding the board.

**Skip the token / find a keyless quote API.** Finnhub's free tier is the
keyless-in-spirit option that actually exists at 60 requests a minute; the
genuinely keyless stock APIs are either unofficial scrapes or gone. A token that
can only read quotes was the smaller compromise.

## Consequences

`secrets.h` now carries a value that is a credential rather than a coordinate,
so the file's "everything here is a credential or an installation detail"
framing holds — but "soft" is doing real work and is worth naming: on a hostile
LAN, `setInsecure()` exposes the token, and the accepted worst case is that
someone else can fetch stock quotes with it. If Finnhub ever gates account-level
or paid actions behind the same token, this ADR stops applying and the key
belongs behind the bridge.

The device sends the token in a URL query string over a connection whose
certificate it does not validate. That is acceptable only for as long as the
token stays soft.
