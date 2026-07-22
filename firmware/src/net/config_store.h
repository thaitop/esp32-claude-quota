// The installation settings the Config Mode edits and every feed reads.
//
// Until this file existed the location, the tracked coins and the tracked
// stocks were compile-time constants in config.h and secrets.h: changing one
// meant a reflash. They are now a small struct loaded from NVS at boot, with
// those same #defines standing in as the factory defaults when NVS is empty.
//
// The struct lives on the net side, not in model.h: it is configuration, not
// something a Screen draws, and the ui/ layer must stay as ignorant of it as it
// is of HTTP. The feeds read it through the same coinId()/stockSymbol()/... the
// screens already call, whose definitions moved here from model.cpp so a single
// table still backs both the request and the label (the invariant model.cpp's
// comment guarded, now holding across a runtime value rather than a literal).
//
// Config Mode owns the writes; the feeds only read. A write takes effect on the
// reboot Config Mode ends with, so no feed ever reads a half-changed set or a
// price left over from a coin that is no longer tracked -- see ADR-0005.
#pragma once

#include "../model.h"

namespace net {

// Loads the settings from NVS into the process-wide copy the accessors below
// return, falling back to the config.h/secrets.h defaults for any field NVS
// does not carry (a fresh device, or a struct grown since the last save). Call
// once at boot before the first feed runs.
void configBegin();

// The location the Weather Feed asks about and the two timezone strings its
// request and the header clock want. weatherTz is the IANA zone name Open-Meteo
// takes; clockTz is the "UTC+7"/raw-POSIX string timesync.cpp parses.
float configWeatherLat();
float configWeatherLon();
const char *configWeatherTz();
const char *configClockTz();

// A working copy Config Mode mutates as fields validate, seeded from the live
// settings when the portal opens and committed to NVS by configCommit(). Held
// here rather than in the portal so the accessors and the draft cannot drift
// into two layouts.
struct Settings {
  float weatherLat;
  float weatherLon;
  char weatherTz[40];
  char clockTz[16];
  char coinId[(size_t)Coin::Count][24];
  char coinTicker[(size_t)Coin::Count][8];
  char coinName[(size_t)Coin::Count][20];
  char stockSym[(size_t)Ticker::Count][12];
};

// A copy of the live settings, for Config Mode to edit without touching what
// the (stopped) feeds would read.
Settings configDraft();

// The catalog of pickable coins and stocks Config Mode offers as a dropdown.
//
// A curated list rather than free text, because the logos are bitmaps compiled
// into the firmware and keyed by id/symbol: a coin the catalog names has (or can
// have) a matching logo, an arbitrary typed id would render the wrong one or a
// fallback. The catalog carries only strings, so it can be the full list here
// even before every entry's artwork is baked -- an entry with no logo yet draws
// the generic glyph, never a mislabelled one. See ADR-0005 and ui/logos.
struct CoinCatalogEntry {
  const char *id;      // CoinGecko id, what the feed asks for
  const char *ticker;  // the toggle's label
  const char *name;    // the title's label
};
struct StockCatalogEntry {
  const char *symbol;  // Finnhub symbol, also the row's label
  const char *name;    // company name, shown in the picker
};

const CoinCatalogEntry *coinCatalog(size_t &count);
const StockCatalogEntry *stockCatalog(size_t &count);

// Looks a chosen id/symbol up in the catalog, so /save can fill the ticker and
// name from the authoritative entry rather than trusting the browser. Returns
// nullptr for anything not in the catalog.
const CoinCatalogEntry *coinCatalogFind(const char *id);
const StockCatalogEntry *stockCatalogFind(const char *symbol);

// Persists `draft` to NVS. The new values are not applied to the running feeds
// -- Config Mode reboots after this, and boot reloads them cleanly.
void configCommit(const Settings &draft);

}  // namespace net
