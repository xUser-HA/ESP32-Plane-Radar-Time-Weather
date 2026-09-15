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
unsigned long g_last_info_refresh_ms = 0;

ui::info::Screen g_screen = ui::info::Screen::Radar;

void setupTime() {
  // Kazakhstan / Almaty: UTC+5, no DST.
  configTime(
      5 * 3600,
      0,
      "pool.ntp.org",
      "time.nist.gov",
      "time.google.com"
  );
}

void drawCurrentScreen() {

  if (g_screen == ui::info::Screen::Radar) {

    if (WiFi.status() == WL_CONNECTED) {
      ui::radarDisplayDraw();
      g_radar_visible = true;
    }

  } else {

    g_radar_visible = false;
    ui::info::draw(g_screen);
  }
}


/*
 * Switch between the three screens:
 *
 * RADAR -> WEATHER -> CLOCK -> RADAR
 *
 * Radar range is fixed at 25 km and is no longer
 * changed by the BOOT button.
 */
void cycleScreen() {

  if (g_screen == ui::info::Screen::Radar) {

    g_screen = ui::info::Screen::Weather;

    // Show the weather screen immediately.
    // The network request is performed after the screen
    // has already been displayed.
    ui::info::drawWeather();

    // Get fresh weather data.
    services::weather::update(
        services::location::lat(),
        services::location::lon()
    );

    // Redraw with the received data.
    ui::info::drawWeather();

  } else if (g_screen == ui::info::Screen::Weather) {

    g_screen = ui::info::Screen::Clock;

    ui::info::drawClock();

  } else {

    g_screen = ui::info::Screen::Radar;

    drawCurrentScreen();
  }
}


void handleBootButton() {

  bootButtonPollLongPress();

  if (bootButtonConsumeTap()) {

    // One press = next screen.
    cycleScreen();
  }
}


void fetchAndDrawAircraft() {

  const float fetch_km = ui::radar::fetchRadiusKm();

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

} // namespace


void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("Plane Radar + Weather + Clock");

  bootButtonInit();

  displayInit();

  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }

  services::location::init();

  /*
   * Initialize the radar range system.
   *
   * The next step will force the only available range
   * to 25 km.
   */
  ui::radar::rangeInit();

  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {

    setupTime();

    /*
     * Do not block startup with a weather request.
     * Radar starts immediately.
     */
    drawCurrentScreen();
  }
}


void loop() {

  handleBootButton();

  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {

    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms =
        millis() - g_wifi_down_since;

    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >=
            config::kWifiReconnectIntervalMs) {

      g_last_reconnect_ms = millis();

      if (wifiReconnect()) {

        g_wifi_down_since = 0;

        setupTime();

        drawCurrentScreen();
      }
    }

  } else {

    g_wifi_down_since = 0;

    if (g_screen == ui::info::Screen::Radar) {

      if (!g_radar_visible) {

        drawCurrentScreen();

      } else if (
          millis() - g_last_adsb_fetch_ms >=
          config::kAdsbFetchIntervalMs) {

        g_last_adsb_fetch_ms = millis();

        fetchAndDrawAircraft();
      }

    } else {

      /*
       * Refresh information screens every 30 seconds.
       *
       * Weather itself is cached for 10 minutes by
       * weather_client.cpp, so this does not hammer
       * Open-Meteo.
       */

      if (millis() - g_last_info_refresh_ms >= 30000UL) {

        g_last_info_refresh_ms = millis();

        if (g_screen == ui::info::Screen::Weather) {

          services::weather::update(
              services::location::lat(),
              services::location::lon()
          );

          ui::info::drawWeather();

        } else {

          ui::info::drawClock();
        }
      }
    }
  }

  delay(10);
}
