#pragma once

namespace services::weather {

struct CurrentWeather {
  float temperature_c = 0.0f;
  float apparent_c = 0.0f;
  float humidity_pct = 0.0f;
  float pressure_hpa = 0.0f;
  float wind_ms = 0.0f;
  float wind_deg = 0.0f;
  int weather_code = -1;
  bool valid = false;
};

const CurrentWeather& current();

bool update(double lat, double lon);

/**
 * Resolve the IANA timezone from the supplied coordinates
 * using Open-Meteo timezone=auto.
 */
bool updateTimezone(double lat, double lon);

}  // namespace services::weather
