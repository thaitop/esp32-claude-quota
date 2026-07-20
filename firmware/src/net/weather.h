// The Weather Feed: current conditions from Open-Meteo, fetched directly.
//
// Direct rather than through the bridge so a sleeping Mac costs the two Claude
// screens and nothing else -- see ADR-0002.
#pragma once

#include "../model.h"

namespace net {

FetchOutcome fetchWeather(AppModel &model);

}  // namespace net
