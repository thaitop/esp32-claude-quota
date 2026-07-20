// One TLS request, torn down before it returns.
//
// The Weather and Crypto feeds both need HTTPS and both have to release the
// TLS client the moment their response is parsed: a handshake peaks near 45KB
// against roughly 173KB of free heap, so a client held open between polls is a
// third of the headroom spent doing nothing (ADR-0003). Putting the teardown in
// one place means a feed added later cannot forget it.
//
// Certificates are not validated, per ADR-0002: neither API carries a
// credential or private data, and a pinned root that silently expires would
// break the display years later with nothing to diagnose it by.
#pragma once

#include <ArduinoJson.h>

#include "../model.h"

namespace net {

// GETs `https://host/path` and parses the body into `doc`. `filter` keeps the
// document down to the handful of fields a screen actually reads -- Open-Meteo
// returns several hundred bytes of units and metadata around the four numbers
// the Weather screen wants.
FetchOutcome fetchJson(const char *host, const char *path, JsonDocument &doc,
                       const JsonDocument &filter);

}  // namespace net
