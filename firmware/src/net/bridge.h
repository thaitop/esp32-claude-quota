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

}  // namespace net
