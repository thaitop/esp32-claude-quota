// The Stock Feed: one symbol's price and day's change, from Finnhub.
//
// Finnhub's /quote takes a single symbol, so unlike the Crypto Feed this cannot
// fetch the whole screen in one request. Each call fetches the next symbol in
// the list and advances, so the round-robin poller's one-request-in-flight rule
// (ADR-0003) still holds -- the rotation lives inside this one slot the way the
// bridge's history sub-timer lives inside the Bridge Feed's.
#pragma once

#include "../model.h"

namespace net {

FetchOutcome fetchStock(AppModel &model);

}  // namespace net
