#include "stock.h"

#include <Arduino.h>

#include "../config.h"
#include "../secrets.h"
#include "https.h"

namespace net {
namespace {

// Which symbol the next fetch is for. Advanced every call, success or failure,
// so one delisted or rate-limited symbol cannot wedge the rotation on itself
// and starve the other four -- the poller's backoff still slows the cadence,
// but it slows it evenly across the list rather than sticking on one row.
uint8_t cursor = 0;

// One symbol's reply. Both fields are required: Finnhub answers an unknown
// symbol with a 200 whose `c` is 0, and a plan without quote access with the
// change field absent. Defaulting either would draw a confident price or a flat
// "+0.00%" out of a missing number, which is the thing ADR-0001 forbids -- so a
// zero price or a non-numeric change fails the parse and the row shows Unknown.
bool parseQuote(JsonVariantConst body, StockQuote &quote) {
  if (!body["c"].is<float>() || !body["dp"].is<float>()) return false;

  const float price = body["c"].as<float>();
  if (price <= 0.0f) return false;  // Finnhub's "symbol not found" sentinel

  quote.priceUsd = price;
  quote.changePct = body["dp"].as<float>();
  quote.trusted = true;
  return true;
}

}  // namespace

FetchOutcome fetchStock(AppModel &model) {
  const Ticker which = (Ticker)cursor;
  cursor = (cursor + 1) % (uint8_t)Ticker::Count;

  char path[96];
  snprintf(path, sizeof(path), FINNHUB_PATH_FMT, stockSymbol(which),
           FINNHUB_TOKEN);

  // /quote is a flat object of seven numbers; the filter keeps the two the row
  // reads so a rate-limit body wrapped around an error cannot slip through as a
  // price.
  JsonDocument filter;
  filter["c"] = true;
  filter["dp"] = true;

  JsonDocument doc;
  const FetchOutcome outcome = fetchJson(FINNHUB_HOST, path, doc, filter);
  if (outcome != FetchOutcome::Ok) {
    // Only this symbol loses trust. The other four were fetched seconds ago and
    // a single failed GET says nothing about them; if the whole feed is down
    // each will fail on its own turn and blank within one cycle.
    model.stocks.quote(which).trusted = false;
    return outcome;
  }

  StockQuote fetched;
  if (parseQuote(doc.as<JsonVariantConst>(), fetched)) {
    model.stocks.quote(which) = fetched;
    return FetchOutcome::Ok;
  }

  model.stocks.quote(which).trusted = false;
  return FetchOutcome::Malformed;
}

}  // namespace net
