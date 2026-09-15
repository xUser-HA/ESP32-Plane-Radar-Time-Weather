#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

using PollFn = void (*)();

void setPollFn(PollFn fn);

bool fetchUpdate(
    double center_lat,
    double center_lon,
    float fetch_radius_km);

/** Start non-blocking background ADS-B updates. */
void startBackgroundUpdates(
    double center_lat,
    double center_lon,
    float fetch_radius_km,
    unsigned long interval_ms);

}  // namespace services::adsb
