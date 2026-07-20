// The wall clock: one NTP sync at boot, then the ESP32's own RTC.
//
// Not a Feed. The feeds exist because their values go stale in minutes and the
// poller has to keep them fresh against a rate limit; the clock is the opposite
// case -- the RTC drifts by a couple of seconds a day, which is well inside the
// minute the header prints, so re-polling would spend requests to correct an
// error nobody can see. Sync once, stop the daemon, read the RTC after that.
#pragma once

#include "../model.h"

namespace net {

// Blocks up to NTP_WAIT_MS for the first packet, then stops SNTP whether it
// arrived or not. Returns true if the RTC now holds real time. Safe to call
// again after a failure -- it is a no-op once synced.
bool syncClock();

bool clockSynced();

// Renders the RTC into model.wallClock, or empties it while unsynced. Cheap
// enough for the one-second tick.
void readClock(AppModel &model);

}  // namespace net
