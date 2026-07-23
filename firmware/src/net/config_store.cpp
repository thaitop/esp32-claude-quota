#include "config_store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

#include "../config.h"
#include "../secrets.h"

namespace net {

// The one live copy the accessors return. Filled by configBegin(); reading it
// before that is a defaults-shaped struct of zeroes, which is why configBegin()
// has to run before the first feed. Named (not anonymous) and static so the
// coinId()/... definitions below -- which sit in the global namespace because
// that is where model.h declares them, so both the feeds and the screens find
// the same symbol -- can still reach it within this translation unit.
static Settings live;

namespace {

// One NVS namespace of our own -- theme and display already keep theirs, and
// two handles on one namespace fight over the same keys.
constexpr char NVS_NAMESPACE[] = "cfg";
constexpr char NVS_KEY_BLOB[] = "settings";

// Bumped whenever the Settings layout changes, so a blob written by an older
// build is ignored rather than reinterpreted field-for-field into garbage. The
// device then comes up on defaults, which is the safe direction: a stale coin
// id shows Unknown, it does not point a request somewhere unexpected.
constexpr uint32_t SETTINGS_VERSION = 3;  // v2 added wifiSsid/wifiPass; v3 added bridgeUrl

struct StoredSettings {
  uint32_t version;
  Settings settings;
};

// The factory defaults, assembled from the compile-time constants that used to
// be the only values there were. coinId comes from config.h (which also built
// the request from it), the tickers and names from the tables model.cpp used to
// carry, the symbols and the location from the same places the feeds read them.
const char *const DEFAULT_COIN_ID[(size_t)Coin::Count] = {
    CRYPTO_ID_BTC, CRYPTO_ID_ETH, CRYPTO_ID_BNB};
const char *const DEFAULT_COIN_TICKER[(size_t)Coin::Count] = {"BTC", "ETH", "BNB"};
const char *const DEFAULT_COIN_NAME[(size_t)Coin::Count] = {"Bitcoin", "Ethereum",
                                                            "BNB"};
const char *const DEFAULT_STOCK_SYM[(size_t)Ticker::Count] = {"AAPL", "NVDA", "TSLA",
                                                              "GOOG", "MSFT"};

// The pickable catalog. Strings only, so it is the full intended list already;
// an entry whose logo is not baked yet falls back to the generic glyph in
// ui/logos rather than being left out. The defaults (BTC/ETH/BNB, the five US
// tech names) are the entries that ship with a logo, so a fresh device looks
// right out of the box. Add an entry here, its logo row in ui/logos, its art in
// tools/mkicons, and regenerate -- see ADR-0005.
const CoinCatalogEntry COIN_CATALOG[] = {
    {"bitcoin", "BTC", "Bitcoin"},      {"ethereum", "ETH", "Ethereum"},
    {"binancecoin", "BNB", "BNB"},      {"solana", "SOL", "Solana"},
    {"ripple", "XRP", "XRP"},           {"cardano", "ADA", "Cardano"},
    {"dogecoin", "DOGE", "Dogecoin"},   {"polkadot", "DOT", "Polkadot"},
    {"litecoin", "LTC", "Litecoin"},    {"chainlink", "LINK", "Chainlink"},
    {"tron", "TRX", "TRON"},            {"avalanche-2", "AVAX", "Avalanche"},
};
const StockCatalogEntry STOCK_CATALOG[] = {
    {"AAPL", "Apple"},   {"NVDA", "NVIDIA"},   {"TSLA", "Tesla"},
    {"GOOG", "Alphabet"},{"MSFT", "Microsoft"},{"AMZN", "Amazon"},
    {"META", "Meta"},    {"NFLX", "Netflix"},  {"AMD", "AMD"},
    {"INTC", "Intel"},   {"COIN", "Coinbase"}, {"PYPL", "PayPal"},
};

void copyField(char *dst, size_t n, const char *src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

void loadDefaults(Settings &out) {
  out.weatherLat = WEATHER_LATITUDE;
  out.weatherLon = WEATHER_LONGITUDE;
  copyField(out.weatherTz, sizeof(out.weatherTz), WEATHER_TZ);
  copyField(out.clockTz, sizeof(out.clockTz), CLOCK_TZ);
  for (size_t i = 0; i < (size_t)Coin::Count; i++) {
    copyField(out.coinId[i], sizeof(out.coinId[i]), DEFAULT_COIN_ID[i]);
    copyField(out.coinTicker[i], sizeof(out.coinTicker[i]), DEFAULT_COIN_TICKER[i]);
    copyField(out.coinName[i], sizeof(out.coinName[i]), DEFAULT_COIN_NAME[i]);
  }
  for (size_t i = 0; i < (size_t)Ticker::Count; i++) {
    copyField(out.stockSym[i], sizeof(out.stockSym[i]), DEFAULT_STOCK_SYM[i]);
  }
  copyField(out.wifiSsid, sizeof(out.wifiSsid), WIFI_SSID);
  copyField(out.wifiPass, sizeof(out.wifiPass), WIFI_PASSWORD);
  copyField(out.bridgeUrl, sizeof(out.bridgeUrl), BRIDGE_BASE_URL);
}

}  // namespace

void configBegin() {
  loadDefaults(live);

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);  // read-only
  StoredSettings stored;
  const size_t got = prefs.getBytes(NVS_KEY_BLOB, &stored, sizeof(stored));
  prefs.end();

  // Only a blob that is exactly this build's size and version replaces the
  // defaults. A short read (never written), a longer one (an older, smaller
  // struct) or a version mismatch all leave `live` on defaults.
  if (got == sizeof(stored) && stored.version == SETTINGS_VERSION) {
    live = stored.settings;
    Serial.println("config: loaded from NVS");
  } else {
    Serial.printf("config: using defaults (nvs read=%u, want=%u)\n", (unsigned)got,
                  (unsigned)sizeof(stored));
  }
}

float configWeatherLat() { return live.weatherLat; }
float configWeatherLon() { return live.weatherLon; }
const char *configWeatherTz() { return live.weatherTz; }
const char *configClockTz() { return live.clockTz; }
const char *configWifiSsid() { return live.wifiSsid; }
const char *configWifiPass() { return live.wifiPass; }
const char *configBridgeUrl() { return live.bridgeUrl; }

Settings configDraft() { return live; }

const CoinCatalogEntry *coinCatalog(size_t &count) {
  count = sizeof(COIN_CATALOG) / sizeof(COIN_CATALOG[0]);
  return COIN_CATALOG;
}

const StockCatalogEntry *stockCatalog(size_t &count) {
  count = sizeof(STOCK_CATALOG) / sizeof(STOCK_CATALOG[0]);
  return STOCK_CATALOG;
}

const CoinCatalogEntry *coinCatalogFind(const char *id) {
  for (const CoinCatalogEntry &e : COIN_CATALOG)
    if (strcmp(e.id, id) == 0) return &e;
  return nullptr;
}

const StockCatalogEntry *stockCatalogFind(const char *symbol) {
  for (const StockCatalogEntry &e : STOCK_CATALOG)
    if (strcmp(e.symbol, symbol) == 0) return &e;
  return nullptr;
}

void configCommit(const Settings &draft) {
  StoredSettings stored;
  stored.version = SETTINGS_VERSION;
  stored.settings = draft;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBytes(NVS_KEY_BLOB, &stored, sizeof(stored));
  prefs.end();
  Serial.println("config: committed to NVS");
}

void configCommitWifi(const char *ssid, const char *pass) {
  // Start from the live copy so every other setting survives, overwrite only
  // the two WiFi fields, and write the whole blob back -- one namespace, one
  // key, so a partial write is not a thing NVS lets us do anyway.
  Settings draft = live;
  copyField(draft.wifiSsid, sizeof(draft.wifiSsid), ssid);
  copyField(draft.wifiPass, sizeof(draft.wifiPass), pass);
  configCommit(draft);
  Serial.println("config: wifi credentials committed to NVS");
}

}  // namespace net

// The accessors model.h declares, in the global namespace it declares them in
// so the feeds (in namespace net) and the screens (in namespace ui) resolve to
// one definition. Moved here from model.cpp: they now return the runtime
// settings rather than a compile-time table, but a single table still backs
// both the request and the label, so a coin the request asks for and the row
// the screen labels can never disagree.
const char *coinId(Coin coin) {
  if ((size_t)coin >= (size_t)Coin::Count) return "";
  return net::live.coinId[(size_t)coin];
}

const char *coinTicker(Coin coin) {
  if ((size_t)coin >= (size_t)Coin::Count) return "--";
  return net::live.coinTicker[(size_t)coin];
}

const char *coinName(Coin coin) {
  if ((size_t)coin >= (size_t)Coin::Count) return "--";
  return net::live.coinName[(size_t)coin];
}

const char *stockSymbol(Ticker ticker) {
  if ((size_t)ticker >= (size_t)Ticker::Count) return "--";
  return net::live.stockSym[(size_t)ticker];
}
