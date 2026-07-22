#include "weather.h"

#include <Arduino.h>

#include "../config.h"
#include "config_store.h"
#include "https.h"

namespace net {
namespace {

// A missing field parses as null, and null must not become a plausible zero:
// 0C is a real temperature. Anything unreadable costs the whole snapshot its
// Trust rather than being filled in.
bool readFloat(JsonVariantConst value, float &out) {
  if (!value.is<float>()) return false;
  out = value.as<float>();
  return true;
}

}  // namespace

FetchOutcome fetchWeather(AppModel &model) {
  char path[224];
  snprintf(path, sizeof(path), WEATHER_PATH_FMT, configWeatherLat(),
           configWeatherLon(), configWeatherTz());

  // Open-Meteo wraps the four numbers we want in units, a timezone name and an
  // elevation. The filter keeps the parsed document to the "current" object.
  JsonDocument filter;
  filter["current"] = true;

  JsonDocument doc;
  const FetchOutcome outcome = fetchJson(WEATHER_HOST, path, doc, filter);
  if (outcome != FetchOutcome::Ok) {
    model.weather.trusted = false;
    return outcome;
  }

  JsonVariantConst current = doc["current"];
  WeatherSnapshot fetched;

  // Every field the screen makes a claim from is required. Defaulting the
  // weather code to zero means Clear, and defaulting is_day means day, so a
  // response that dropped either would draw a confident sun over a rainy
  // night -- a picture assembled out of missing data (ADR-0001).
  if (!readFloat(current["temperature_2m"], fetched.temperatureC) ||
      !readFloat(current["apparent_temperature"], fetched.apparentC) ||
      !current["weather_code"].is<int>() || !current["is_day"].is<int>()) {
    model.weather.trusted = false;
    return FetchOutcome::Malformed;
  }

  fetched.code = (uint8_t)current["weather_code"].as<int>();
  fetched.isDay = current["is_day"].as<int>() != 0;
  // Humidity is the exception: it is a garnish rather than a claim, and the
  // screen simply omits the line when it is absent.
  fetched.humidityPct = current["relative_humidity_2m"] | -1;
  fetched.trusted = true;

  model.weather = fetched;
  return FetchOutcome::Ok;
}

}  // namespace net
