// The Crypto Feed: one coin's price and 24-hour change, from CoinGecko.
#pragma once

#include "../model.h"

namespace net {

FetchOutcome fetchCrypto(AppModel &model);

}  // namespace net
