#pragma once

namespace services::weather {

struct CurrentWeather {
  bool valid = false;
  float temperature_c = 0.0f;
  float apparent_c = 0.0f;
  float humidity_pct = 0.0f;
  float pressure_hpa = 0.0f;
  float wind_ms = 0.0f;
  float wind_deg = 0.0f;
  int weather_code = -1;
};

bool update(double lat, double lon);
const CurrentWeather& current();
const char* descriptionRu(int weather_code);

}  // namespace services::weather
