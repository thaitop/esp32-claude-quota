#include "crypto.h"

#include <Arduino.h>

#include "../config.h"
#include "https.h"

namespace net {
namespace {

// One coin out of the response. Returns false without touching the quote when
// the coin's object is missing or carries anything but numbers where the price,
// the change and the volume should be.
//
// A rate-limited CoinGecko answers 200 with an error object rather than a
// price, which is exactly the renamed-field case that shows up as a blank
// screen instead of an error. Refusing a non-numeric field catches it.
//
// Every field is required. Defaulting the change to zero would draw a confident
// green "+0.00%" -- a claim about the market built out of a missing field,
// which is the thing ADR-0001 exists to forbid.
bool parseCoin(JsonVariantConst coin, CoinQuote &quote) {
  if (!coin["usd"].is<float>() || !coin["usd_24h_change"].is<float>() ||
      !coin["usd_24h_vol"].is<float>()) {
    return false;
  }

  quote.priceUsd = coin["usd"].as<float>();
  quote.change24hPct = coin["usd_24h_change"].as<float>();
  quote.volume24hUsd = coin["usd_24h_vol"].as<float>();
  quote.trusted = true;
  return true;
}

}  // namespace

FetchOutcome fetchCrypto(AppModel &model) {
  // Three objects of three numbers each, so the filter is only here to reject
  // anything the free tier decides to wrap them in later.
  JsonDocument filter;
  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) {
    filter[coinId((Coin)i)] = true;
  }

  // Built from the runtime ids rather than a literal path: Config Mode can
  // change which coins are tracked, and coinId() reads the same table the
  // filter above and the labels on screen do, so the request cannot ask for a
  // coin the screen is not showing.
  char path[192];
  snprintf(path, sizeof(path), CRYPTO_PATH_FMT, coinId(Coin::BTC),
           coinId(Coin::ETH), coinId(Coin::BNB));

  JsonDocument doc;
  const FetchOutcome outcome = fetchJson(CRYPTO_HOST, path, doc, filter);
  if (outcome != FetchOutcome::Ok) {
    // The transport failed, so nothing in the response can be believed --
    // including the coins that were fine an hour ago.
    for (CoinQuote &quote : model.crypto.coins) quote.trusted = false;
    return outcome;
  }

  // Coin by coin rather than all-or-nothing. A coin id CoinGecko has retired
  // comes back as a 200 with that one key absent, and failing the whole fetch
  // over it would blank two coins that arrived intact -- and, because the
  // poller backs off on failure, would stop refreshing them as well.
  uint8_t parsed = 0;
  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) {
    const Coin which = (Coin)i;
    CoinQuote fetched;
    if (parseCoin(doc[coinId(which)], fetched)) {
      model.crypto.coin(which) = fetched;
      parsed++;
    } else {
      model.crypto.coin(which).trusted = false;
    }
  }

  // Nothing usable in a body that parsed as JSON is the malformed case: the
  // screen has no figures to show and the Setting screen should say why.
  return parsed > 0 ? FetchOutcome::Ok : FetchOutcome::Malformed;
}

}  // namespace net
