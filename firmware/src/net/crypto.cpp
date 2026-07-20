#include "crypto.h"

#include <Arduino.h>

#include "../config.h"
#include "https.h"

namespace net {

FetchOutcome fetchCrypto(AppModel &model) {
  // The response is already two numbers under one key, so the filter is only
  // here to reject anything the free tier decides to wrap them in later.
  JsonDocument filter;
  filter[CRYPTO_COIN_ID] = true;

  JsonDocument doc;
  const FetchOutcome outcome = fetchJson(CRYPTO_HOST, CRYPTO_PATH, doc, filter);
  if (outcome != FetchOutcome::Ok) {
    model.crypto.trusted = false;
    return outcome;
  }

  JsonVariantConst coin = doc[CRYPTO_COIN_ID];
  // A rate-limited CoinGecko answers 200 with an error object rather than a
  // price, which is exactly the renamed-field case that shows up as a blank
  // screen instead of an error. Refusing a non-numeric field catches it.
  //
  // Both fields are required. Defaulting the change to zero would draw a
  // confident green "+0.00%" -- a claim about the market built out of a
  // missing field, which is the thing ADR-0001 exists to forbid.
  if (!coin["usd"].is<float>() || !coin["usd_24h_change"].is<float>()) {
    model.crypto.trusted = false;
    return FetchOutcome::Malformed;
  }

  CryptoSnapshot fetched;
  fetched.priceUsd = coin["usd"].as<float>();
  fetched.change24hPct = coin["usd_24h_change"].as<float>();
  fetched.trusted = true;

  model.crypto = fetched;
  return FetchOutcome::Ok;
}

}  // namespace net
