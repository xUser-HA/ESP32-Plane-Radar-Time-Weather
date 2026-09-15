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

unsigned long g_last_info_refresh_ms = 0;
unsigned long g_last_radar_animation_ms = 0;

ui::info::Screen g_screen =
    ui::info::Screen::Radar;

// Single tap changes screen.
// Double tap changes radar range.
bool g_pending_tap = false;
unsigned long g_pending_tap_ms = 0;

constexpr unsigned long kDoubleTapWindowMs = 420;

// Radar animation target: about 30 FPS.
constexpr unsigned long
    kRadarAnimationIntervalMs = 33UL;

void setupTime() {
  // Kazakhstan / Almaty: UTC+5, no DST.
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

void onRangeTap() {
  ui::radar::rangeNext();

  char range_label[12];

  ui::radar::formatCurrentRing3Label(
      range_label,
      sizeof(range_label));

  Serial.printf(
      "Range: %s (outer ~%.0f km)\n",
      range_label,
      ui::radar::rangeCurrent().outer_km);

  if (g_screen == ui::info::Screen::Radar &&
      WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();

    g_last_radar_animation_ms =
        millis();
  }
}

void cycleScreen() {
  if (g_screen ==
      ui::info::Screen::Radar) {
    g_screen =
        ui::info::Screen::Weather;

    services::weather::update(
        services::location::lat(),
        services::location::lon());

  } else if (
      g_screen ==
      ui::info::Screen::Weather) {
    g_screen =
        ui::info::Screen::Clock;

  } else {
    g_screen =
        ui::info::Screen::Radar;
  }

  drawCurrentScreen();
}

void handleBootButton() {
  bootButtonPollLongPress();

  if (bootButtonConsumeTap()) {
    const unsigned long now =
        millis();

    if (g_pending_tap &&
        now - g_pending_tap_ms <=
            kDoubleTapWindowMs) {
      g_pending_tap = false;

      if (g_screen ==
          ui::info::Screen::Radar) {
        onRangeTap();
      }

      return;
    }

    g_pending_tap = true;
    g_pending_tap_ms = now;
  }

  if (g_pending_tap &&
      millis() - g_pending_tap_ms >
          kDoubleTapWindowMs) {
    g_pending_tap = false;

    cycleScreen();
  }
}

void updateRadarAnimation() {
  if (g_screen !=
      ui::info::Screen::Radar) {
    return;
  }

  if (!g_radar_visible) {
    return;
  }

  const unsigned long now =
      millis();

  if (now -
          g_last_radar_animation_ms <
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

  services::adsb::setPollFn(
      wifiLoop);

  if (wifiSetupConnect()) {
    setupTime();

    services::weather::update(
        services::location::lat(),
        services::location::lon());

    /*
     * Start ADS-B networking in a separate
     * FreeRTOS task.
     *
     * The main loop will no longer wait
     * for HTTPS/ADS-B requests.
     */
    services::adsb::startBackgroundUpdates(
        services::location::lat(),
        services::location::lon(),
        ui::radar::fetchRadiusKm(),
        config::kAdsbFetchIntervalMs);

    drawCurrentScreen();
  }
}

void loop() {
  /*
   * Keep the display/button loop responsive.
   */
  handleBootButton();

  wifiLoop();

  /*
   * Radar sweep runs independently from
   * ADS-B network requests.
   */
  updateRadarAnimation();

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
        millis() -
        g_wifi_down_since;

    if (down_ms >=
            config::kWifiDownGraceMs &&
        millis() -
                g_last_reconnect_ms >=
            config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms =
          millis();

      if (wifiReconnect()) {
        g_wifi_down_since = 0;

        setupTime();

        services::weather::update(
            services::location::lat(),
            services::location::lon());

        drawCurrentScreen();
      }
    }

  } else {
    g_wifi_down_since = 0;

    if (g_screen ==
        ui::info::Screen::Radar) {

      if (!g_radar_visible) {
        drawCurrentScreen();
      }

    } else {

      if (millis() -
              g_last_info_refresh_ms >=
          30000UL) {
        g_last_info_refresh_ms =
            millis();

        if (g_screen ==
            ui::info::Screen::Weather) {

          services::weather::update(
              services::location::lat(),
              services::location::lon());

          ui::info::drawWeather();

        } else {

          ui::info::drawClock();
        }
      }
    }
  }

  /*
   * Small yield so WiFi/FreeRTOS tasks
   * get processor time without creating
   * a visible delay in the sweep.
   */
  delay(1);
}
