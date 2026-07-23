#include "crypto.h"

#include <Arduino.h>

#include "../config.h"
#include "https.h"

namespace net {
namespace {

// One coin out of the response. Returns false without touching the quote when
// the element carries anything but numbers where the price, the 24h change and
// the 7d change should be.
//
// A rate-limited CoinGecko answers 200 with an error object rather than a
// price, which is exactly the renamed-field case that shows up as a blank
// screen instead of an error. Refusing a non-numeric field catches it.
//
// Every field is required. Defaulting a change to zero would draw a confident
// green "+0.00%" -- a claim about the market built out of a missing field,
// which is the thing ADR-0001 exists to forbid.
bool parseCoin(JsonVariantConst coin, CoinQuote &quote) {
  if (!coin["current_price"].is<float>() ||
      !coin["price_change_percentage_24h"].is<float>() ||
      !coin["price_change_percentage_7d_in_currency"].is<float>()) {
    return false;
  }

  quote.priceUsd = coin["current_price"].as<float>();
  quote.change24hPct = coin["price_change_percentage_24h"].as<float>();
  quote.change7dPct = coin["price_change_percentage_7d_in_currency"].as<float>();
  quote.trusted = true;
  return true;
}

// Which Coin an array element belongs to, matched by the id CoinGecko echoes
// back. Count when the element is for a coin the screen does not track (the
// request never asks for one, but the match is by value, so it is checked).
Coin coinFor(JsonVariantConst coin) {
  const char *id = coin["id"].as<const char *>();
  if (id == nullptr) return Coin::Count;
  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) {
    if (strcmp(id, coinId((Coin)i)) == 0) return (Coin)i;
  }
  return Coin::Count;
}

}  // namespace

FetchOutcome fetchCrypto(AppModel &model) {
  // /coins/markets returns an array of fat objects (image URLs, all-time highs,
  // supply figures) and only four of their fields matter here. The array filter
  // is one template object applied to every element, so it both drops the bulk
  // before it reaches the heap (ADR-0003) and keeps the id needed to match each
  // element back to a coin.
  JsonDocument filter;
  JsonObject tmpl = filter.add<JsonObject>();
  tmpl["id"] = true;
  tmpl["current_price"] = true;
  tmpl["price_change_percentage_24h"] = true;
  tmpl["price_change_percentage_7d_in_currency"] = true;

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
  // simply drops out of the array, and failing the whole fetch over it would
  // blank two coins that arrived intact -- and, because the poller backs off on
  // failure, would stop refreshing them as well.
  //
  // The array is sorted by market cap, not by the order the request listed, so
  // each element is matched back to its coin by id. A coin absent from the
  // response keeps its last figures but loses trust, same as a bad parse.
  bool seen[(uint8_t)Coin::Count] = {false};
  uint8_t parsed = 0;
  for (JsonVariantConst element : doc.as<JsonArrayConst>()) {
    const Coin which = coinFor(element);
    if (which == Coin::Count) continue;
    CoinQuote fetched;
    if (parseCoin(element, fetched)) {
      model.crypto.coin(which) = fetched;
      seen[(uint8_t)which] = true;
      parsed++;
    }
  }
  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) {
    if (!seen[i]) model.crypto.coin((Coin)i).trusted = false;
  }

  // Nothing usable in a body that parsed as JSON is the malformed case: the
  // screen has no figures to show and the Setting screen should say why.
  return parsed > 0 ? FetchOutcome::Ok : FetchOutcome::Malformed;
}

}  // namespace net
