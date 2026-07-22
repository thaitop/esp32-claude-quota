#include "model.h"

// Host-compilable alongside model.h -- no Arduino headers here either.
//
// coinId()/coinTicker()/coinName()/stockSymbol() used to live here as
// compile-time tables. They now read the runtime settings and so moved to
// net/config_store.cpp, which is not host-compilable (it touches NVS) -- the
// pure, host-compilable half is what stayed: the code below maps a fetched
// weather code to a Screen's vocabulary and turns the enums into words, neither
// of which depends on how the tracked coins or symbols are stored.

// WMO 4677, as Open-Meteo reports it. The ranges rather than the individual
// codes: 61, 63 and 65 are light, moderate and heavy rain, and the screen has
// one rain glyph.
WeatherCondition conditionFor(uint8_t wmoCode) {
  if (wmoCode >= 95) return WeatherCondition::Storm;                   // 95..99
  if (wmoCode >= 85) return WeatherCondition::Snow;                    // 85, 86
  if (wmoCode >= 80) return WeatherCondition::Rain;                    // 80..82
  if (wmoCode >= 71 && wmoCode <= 77) return WeatherCondition::Snow;   // incl. 77 grains
  if (wmoCode >= 51 && wmoCode <= 67) return WeatherCondition::Rain;   // drizzle through freezing rain
  if (wmoCode == 45 || wmoCode == 48) return WeatherCondition::Fog;
  if (wmoCode == 3) return WeatherCondition::Cloudy;
  if (wmoCode == 1 || wmoCode == 2) return WeatherCondition::PartlyCloudy;
  return WeatherCondition::Clear;  // 0, and anything the code space grows later
}

const char *describe(WeatherCondition condition) {
  switch (condition) {
    case WeatherCondition::Clear:        return "Clear";
    case WeatherCondition::PartlyCloudy: return "Partly cloudy";
    case WeatherCondition::Cloudy:       return "Cloudy";
    case WeatherCondition::Fog:          return "Fog";
    case WeatherCondition::Rain:         return "Rain";
    case WeatherCondition::Snow:         return "Snow";
    case WeatherCondition::Storm:        return "Thunderstorm";
  }
  return "--";
}

const char *describe(FetchOutcome outcome) {
  switch (outcome) {
    case FetchOutcome::Never:       return "no data yet";
    case FetchOutcome::Ok:          return "ok";
    case FetchOutcome::Offline:     return "offline";
    case FetchOutcome::Unreachable: return "unreachable";
    case FetchOutcome::Rejected:    return "rejected";
    case FetchOutcome::Malformed:   return "malformed";
  }
  return "unknown";
}
