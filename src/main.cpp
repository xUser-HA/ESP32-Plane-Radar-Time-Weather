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

/*
 * Information screens are intentionally NOT redrawn every second.
 *
 * Weather:
 *   refresh from Open-Meteo every 10 minutes.
 *
 * Clock:
 *   redraw only when the minute changes.
 *
 * This prevents the complete TFT from being cleared and redrawn
 * continuously, which causes visible flicker.
 */
unsigned long g_last_weather_refresh_ms = 0;
int g_last_clock_minute = -1;
int g_last_clock_day = -1;

ui::info::Screen g_screen = ui::info::Screen::Radar;


/*
 * ============================================================
 * TIME
 * ============================================================
 */

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


/*
 * ============================================================
 * DRAW CURRENT SCREEN
 * ============================================================
 */

void drawCurrentScreen() {

  if (
      g_screen ==
      ui::info::Screen::Radar
  ) {

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

      ui::radarDisplayDraw();

      g_radar_visible = true;
    }

  } else {

    g_radar_visible = false;

    ui::info::draw(
        g_screen
    );
  }
}


/*
 * ============================================================
 * WEATHER
 * ============================================================
 */

void showWeatherScreen() {

  g_screen =
      ui::info::Screen::Weather;

  g_radar_visible = false;

  /*
   * Clear old refresh timer.
   * We want an immediate request when entering
   * the weather screen.
   */
  g_last_weather_refresh_ms = 0;

  /*
   * Draw the screen immediately.
   *
   * If there is no cached weather yet,
   * info_screens.cpp will show "No data".
   */
  ui::info::drawWeather();


  /*
   * Request current weather.
   *
   * WiFi is already connected here.
   */
  if (
      WiFi.status() ==
      WL_CONNECTED
  ) {

    services::weather::update(
        services::location::lat(),
        services::location::lon()
    );

    /*
     * Draw ONCE after the request.
     *
     * This is important:
     * we do not keep redrawing the screen.
     */
    ui::info::drawWeather();
  }

  /*
   * Start the 10-minute weather timer
   * after the initial request.
   */
  g_last_weather_refresh_ms =
      millis();
}


/*
 * ============================================================
 * CLOCK
 * ============================================================
 */

void showClockScreen() {

  g_screen =
      ui::info::Screen::Clock;

  g_radar_visible = false;

  /*
   * Force the first clock draw.
   */
  g_last_clock_minute = -1;
  g_last_clock_day = -1;

  ui::info::drawClock();


  /*
   * Remember the current time so that the main loop
   * does not redraw the clock again until the minute changes.
   */
  struct tm tm_now;

  if (
      getLocalTime(
          &tm_now,
          50
      )
  ) {

    g_last_clock_minute =
        tm_now.tm_min;

    g_last_clock_day =
        tm_now.tm_yday;
  }
}


/*
 * ============================================================
 * SCREEN SWITCHING
 *
 * RADAR -> WEATHER -> CLOCK -> RADAR
 * ============================================================
 */

void cycleScreen() {

  if (
      g_screen ==
      ui::info::Screen::Radar
  ) {

    showWeatherScreen();

  } else if (
      g_screen ==
      ui::info::Screen::Weather
  ) {

    showClockScreen();

  } else {

    g_screen =
        ui::info::Screen::Radar;

    g_last_adsb_fetch_ms =
        0;

    drawCurrentScreen();
  }
}


/*
 * ============================================================
 * BOOT BUTTON
 * ============================================================
 */

void handleBootButton() {

  bootButtonPollLongPress();

  if (
      bootButtonConsumeTap()
  ) {

    /*
     * One press = next screen.
     */
    cycleScreen();
  }
}


/*
 * ============================================================
 * ADS-B
 * ============================================================
 */

void fetchAndDrawAircraft() {

  const float fetch_km =
      ui::radar::fetchRadiusKm();

  if (
      !services::adsb::fetchUpdate(
          services::location::lat(),
          services::location::lon(),
          fetch_km
      )
  ) {

    handleBootButton();

    return;
  }


  if (
      g_screen ==
      ui::info::Screen::Radar
  ) {

    ui::radarDisplayRefreshAircraft();
  }


  handleBootButton();
}


/*
 * ============================================================
 * WEATHER REFRESH
 * ============================================================
 *
 * Weather is refreshed only every 10 minutes.
 *
 * IMPORTANT:
 * The display is NOT redrawn between refreshes.
 */

void updateWeatherIfNeeded() {

  if (
      g_screen !=
      ui::info::Screen::Weather
  ) {

    return;
  }


  if (
      WiFi.status() !=
      WL_CONNECTED
  ) {

    return;
  }


  const unsigned long now =
      millis();


  if (
      now -
      g_last_weather_refresh_ms <
      10UL * 60UL * 1000UL
  ) {

    return;
  }


  g_last_weather_refresh_ms =
      now;


  /*
   * Get new weather data.
   */
  services::weather::update(
      services::location::lat(),
      services::location::lon()
  );


  /*
   * Redraw only once after the update.
   */
  ui::info::drawWeather();
}


/*
 * ============================================================
 * CLOCK REFRESH
 * ============================================================
 *
 * The clock has NO second hand.
 *
 * Therefore there is absolutely no reason
 * to redraw it every second.
 *
 * It is redrawn only when the minute changes.
 */

void updateClockIfNeeded() {

  if (
      g_screen !=
      ui::info::Screen::Clock
  ) {

    return;
  }


  struct tm tm_now;

  if (
      !getLocalTime(
          &tm_now,
          10
      )
  ) {

    return;
  }


  /*
   * Redraw when:
   *
   * - minute changes
   * - day changes
   *
   * Day check is useful around midnight.
   */
  if (
      tm_now.tm_min !=
          g_last_clock_minute
      ||
      tm_now.tm_yday !=
          g_last_clock_day
  ) {

    g_last_clock_minute =
        tm_now.tm_min;

    g_last_clock_day =
        tm_now.tm_yday;


    /*
     * One complete redraw per minute.
     *
     * No flicker because it happens only once
     * when the minute changes.
     */
    ui::info::drawClock();
  }
}


/*
 * ============================================================
 * SETUP
 * ============================================================
 */

} // namespace


void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println(
      "Plane Radar + Weather + Clock"
  );


  /*
   * Button.
   */
  bootButtonInit();


  /*
   * Display.
   */
  displayInit();


  /*
   * WiFi setup portal.
   */
  if (
      wifiShowsSetupScreenOnBoot()
  ) {

    statusScreenPortal();
  }


  /*
   * Radar location.
   */
  services::location::init();


  /*
   * Radar range.
   *
   * Fixed 25 km.
   */
  ui::radar::rangeInit();


  /*
   * ADS-B WiFi polling callback.
   */
  services::adsb::setPollFn(
      wifiLoop
  );


  /*
   * Connect WiFi.
   */
  if (
      wifiSetupConnect()
  ) {

    setupTime();


    /*
     * Radar starts immediately.
     */
    drawCurrentScreen();
  }
}


/*
 * ============================================================
 * MAIN LOOP
 * ============================================================
 */

void loop() {

  /*
   * Button must be checked frequently.
   */
  handleBootButton();


  /*
   * Keep WiFi portal / connection alive.
   */
  wifiLoop();


  /*
   * ========================================================
   * WIFI DISCONNECTED
   * ========================================================
   */

  if (
      WiFi.status() !=
      WL_CONNECTED
  ) {

    if (g_radar_visible) {

      Serial.println(
          "WiFi lost — will reconnect"
      );

      g_radar_visible = false;
    }


    if (
        g_wifi_down_since == 0
    ) {

      g_wifi_down_since =
          millis();
    }


    const unsigned long down_ms =
        millis() -
        g_wifi_down_since;


    if (
        down_ms >=
            config::kWifiDownGraceMs
        &&
        millis() -
            g_last_reconnect_ms >=
            config::kWifiReconnectIntervalMs
    ) {

      g_last_reconnect_ms =
          millis();


      if (
          wifiReconnect()
      ) {

        g_wifi_down_since =
            0;


        setupTime();


        /*
         * Restore the current screen
         * after WiFi reconnect.
         */
        drawCurrentScreen();


        /*
         * Force a fresh clock draw next time
         * we check it.
         */
        g_last_clock_minute = -1;
        g_last_clock_day = -1;
      }
    }


  /*
   * ========================================================
   * WIFI CONNECTED
   * ========================================================
   */

  } else {

    g_wifi_down_since = 0;


    /*
     * ======================================================
     * RADAR SCREEN
     * ======================================================
     */

    if (
        g_screen ==
        ui::info::Screen::Radar
    ) {

      if (
          !g_radar_visible
      ) {

        drawCurrentScreen();

      } else if (
          millis() -
              g_last_adsb_fetch_ms >=
          config::kAdsbFetchIntervalMs
      ) {

        g_last_adsb_fetch_ms =
            millis();

        fetchAndDrawAircraft();
      }


    /*
     * ======================================================
     * WEATHER SCREEN
     * ======================================================
     */

    } else if (
        g_screen ==
        ui::info::Screen::Weather
    ) {

      /*
       * Nothing is redrawn continuously.
       *
       * Weather is refreshed only every 10 minutes.
       */
      updateWeatherIfNeeded();


    /*
     * ======================================================
     * CLOCK SCREEN
     * ======================================================
     */

    } else {

      /*
       * Nothing is redrawn continuously.
       *
       * Clock updates only once per minute.
       */
      updateClockIfNeeded();
    }
  }


  /*
   * Short delay.
   *
   * The display is NOT redrawn here.
   */
  delay(10);
}
