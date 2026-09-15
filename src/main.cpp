/**
 * Plane Radar — WiFi setup, radar, weather and clock UI.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/weather_client.h"
#include "services/wifi_setup.h"
#include "ui/info_screens.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;

unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_weather_refresh_ms = 0;
unsigned long g_last_radar_animation_ms = 0;

int g_last_clock_minute = -1;
int g_last_clock_day = -1;

ui::info::Screen g_screen =
    ui::info::Screen::Radar;

/*
 * Radar sweep animation:
 * approximately 30 frames per second.
 *
 * ADS-B polling remains completely independent
 * from this timer.
 */
constexpr unsigned long kRadarAnimationIntervalMs = 33UL;

void setupTime() {
  configTime(
      5 * 3600,
      0,
      "pool.ntp.org",
      "time.nist.gov",
      "time.google.com");
}

void drawCurrentScreen() {
  if (g_screen == ui::info::Screen::Radar) {
    if (WiFi.status() == WL_CONNECTED) {
      ui::radarDisplayDraw();
      g_radar_visible = true;
      g_last_radar_animation_ms = millis();
    }
  } else {
    g_radar_visible = false;
    ui::info::draw(g_screen);
  }
}

void showWeatherScreen() {
  g_screen = ui::info::Screen::Weather;
  g_radar_visible = false;

  g_last_weather_refresh_ms = 0;

  ui::info::drawWeather();

  if (WiFi.status() == WL_CONNECTED) {
    services::weather::update(
        services::location::lat(),
        services::location::lon());

    ui::info::drawWeather();
  }

  g_last_weather_refresh_ms = millis();
}

void showClockScreen() {
  g_screen = ui::info::Screen::Clock;
  g_radar_visible = false;

  g_last_clock_minute = -1;
  g_last_clock_day = -1;

  ui::info::drawClock();

  struct tm tm_now;

  if (getLocalTime(&tm_now, 50)) {
    g_last_clock_minute = tm_now.tm_min;
    g_last_clock_day = tm_now.tm_yday;
  }
}

void cycleScreen() {
  if (g_screen == ui::info::Screen::Radar) {

    showWeatherScreen();

  } else if (g_screen == ui::info::Screen::Weather) {

    showClockScreen();

  } else {

    g_screen = ui::info::Screen::Radar;

    g_last_adsb_fetch_ms = 0;

    drawCurrentScreen();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();

  if (bootButtonConsumeTap()) {
    cycleScreen();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km =
      ui::radar::fetchRadiusKm();

  if (!services::adsb::fetchUpdate(
          services::location::lat(),
          services::location::lon(),
          fetch_km)) {

    handleBootButton();
    return;
  }

  if (g_screen == ui::info::Screen::Radar) {
    ui::radarDisplayRefreshAircraft();
  }

  handleBootButton();
}

void updateWeatherIfNeeded() {
  if (g_screen != ui::info::Screen::Weather) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();

  if (now - g_last_weather_refresh_ms <
      10UL * 60UL * 1000UL) {
    return;
  }

  g_last_weather_refresh_ms = now;

  services::weather::update(
      services::location::lat(),
      services::location::lon());

  ui::info::drawWeather();
}

void updateClockIfNeeded() {
  if (g_screen != ui::info::Screen::Clock) {
    return;
  }

  struct tm tm_now;

  if (!getLocalTime(&tm_now, 10)) {
    return;
  }

  if (tm_now.tm_min != g_last_clock_minute ||
      tm_now.tm_yday != g_last_clock_day) {

    g_last_clock_minute =
        tm_now.tm_min;

    g_last_clock_day =
        tm_now.tm_yday;

    ui::info::drawClock();
  }
}

/*
 * Animate radar sweep independently from ADS-B.
 *
 * This is deliberately a separate timer. Aircraft data can update
 * every several seconds while the sweep continues smoothly at
 * approximately 30 FPS.
 */
void updateRadarAnimation() {
  if (g_screen != ui::info::Screen::Radar) {
    return;
  }

  if (!g_radar_visible) {
    return;
  }

  const unsigned long now = millis();

  if (now - g_last_radar_animation_ms <
      kRadarAnimationIntervalMs) {
    return;
  }

  g_last_radar_animation_ms = now;

  ui::radarDisplayAnimate();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println(
      "Plane Radar + Weather + Clock");

  bootButtonInit();

  displayInit();

  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }

  services::location::init();

  ui::radar::rangeInit();

  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {

    setupTime();

    drawCurrentScreen();
  }
}

void loop() {
  /*
   * Button must remain responsive regardless of
   * which screen is currently displayed.
   */
  handleBootButton();

  /*
   * Keep Wi-Fi stack alive.
   */
  wifiLoop();

  /*
   * Wi-Fi disconnected.
   */
  if (WiFi.status() != WL_CONNECTED) {

    if (g_radar_visible) {
      Serial.println(
          "WiFi lost — will reconnect");

      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms =
        millis() - g_wifi_down_since;

    if (down_ms >=
            config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >=
            config::kWifiReconnectIntervalMs) {

      g_last_reconnect_ms = millis();

      if (wifiReconnect()) {

        g_wifi_down_since = 0;

        setupTime();

        drawCurrentScreen();

        g_last_clock_minute = -1;
        g_last_clock_day = -1;
      }
    }

  } else {

    /*
     * Wi-Fi connected.
     */
    g_wifi_down_since = 0;

    /*
     * RADAR
     */
    if (g_screen ==
        ui::info::Screen::Radar) {

      if (!g_radar_visible) {

        drawCurrentScreen();

      } else {

        /*
         * Smooth sweep animation.
         *
         * This runs independently of ADS-B.
         */
        updateRadarAnimation();

        /*
         * ADS-B data update.
         */
        if (millis() -
                g_last_adsb_fetch_ms >=
            config::kAdsbFetchIntervalMs) {

          g_last_adsb_fetch_ms =
              millis();

          fetchAndDrawAircraft();
        }
      }

    /*
     * WEATHER
     */
    } else if (
        g_screen ==
        ui::info::Screen::Weather) {

      updateWeatherIfNeeded();

    /*
     * CLOCK
     */
    } else {

      updateClockIfNeeded();
    }
  }

  /*
   * Small yield to keep the ESP32 responsive.
   */
  delay(1);
}
