// The Bridge Feed: quota readings, fetched from the Mac over the LAN.
//
// Plain HTTP on purpose. The bridge exists so that no credential and no
// filesystem access lives on the device, and what crosses the wire is two
// percentages -- see ADR-0002.
#pragma once

#include "../model.h"

namespace net {

// Fetches the current reading and writes it into `model.quota`. Returns how
// the attempt went; Trust is a separate matter the bridge reports inside the
// payload, and a healthy 200 can still carry an untrustworthy reading.
FetchOutcome fetchQuota(AppModel &model);

// Fetches the last seven days of Samples into `model.history`.
FetchOutcome fetchHistory(AppModel &model);

// What the poller registers for the Bridge Feed: the quota reading every time,
// and the history behind it on its own much slower schedule.
//
// History is part of this Feed rather than a fourth one because it is the same
// origin and the same schedule family, and two entries would let them drift
// apart. It reports the quota outcome regardless of how history went: quota is
// what every screen but one depends on, and letting a failing history endpoint
// back the shared timer off to 160s would starve it.
FetchOutcome fetchBridge(AppModel &model);

}  // namespace net
