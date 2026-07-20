// Round-robin scheduling across the feeds, one request in flight at a time.
//
// The serialisation is not an implementation detail: a TLS handshake peaks
// near 45KB and two overlapping ones do not fit beside the draw buffers on
// this board. See ADR-0003. Enforcing it here means a feed added later cannot
// accidentally opt out of the rule.
#pragma once

#include <cstdint>

#include "../model.h"

namespace net {

using FetchFn = FetchOutcome (*)(AppModel &);

// Registers a feed. `intervalMs` is how long after a success before it is due
// again; failures retry sooner.
void registerFeed(Feed feed, FetchFn fetch, uint32_t intervalMs);

// Runs at most one fetch, for whichever registered feed is most overdue.
// Returns true if it fetched. Call from the main loop.
bool service(AppModel &model, uint32_t nowMs);

// Forces every feed due immediately -- the manual refresh gesture.
void refreshAll();

}  // namespace net
