// The US equities session, from the clock alone.
//
// Finnhub's free /quote carries no "is the market open" flag, so the Stock
// screen's badge is inferred from the time. This lives in net/ rather than in
// the screen for the same reason timesync does: it needs <time.h>, and the ui
// layer stays ignorant of it (the seam in model.h). The result is written into
// the model as a MarketSession, which is all the screen ever reads.
#pragma once

#include "../model.h"

namespace net {

// The regular US session right now. Unknown until the RTC is set. Regular hours
// only, and exchange holidays are not modelled -- see ADR-0004.
MarketSession marketSession();

}  // namespace net
