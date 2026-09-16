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

bool g_pending_tap = false;
unsigned long g_pending_tap_ms = 0;

constexpr unsigned long kDoubleTapWindowMs =
    420UL;

constexpr unsigned long
    kRadarAnimationIntervalMs =
        33UL;

/*
 * NTP uses UTC.
 *
 * The local UTC offset is obtained automatically
 * from the device coordinates through Open-Meteo.
 */
void setupTime() {
  const long utc_offset_seconds =
      services::weather::timezoneOffsetSeconds();

  Serial.printf(
      "time: UTC offset = %ld seconds\n",
      utc_offset_seconds);

  configTime(
      utc_offset_seconds,
      0,
      "pool.ntp.org",
      "time.nist.gov",
      "time.google.com");
}

/*
 * Resolve timezone from device coordinates.
 *
 * Open-Meteo uses:
 *
 *   latitude + longitude
 *          ↓
 *      timezone=auto
 *          ↓
 *   timezone + utc_offset_seconds
 */
bool setupTimezone() {
  const double lat =
      services::location::lat();

  const double lon =
      services::location::lon();

  if (!services::weather::updateTimezone(
          lat,
          lon)) {
    Serial.println(
        "time: timezone resolution failed");

    return false;
  }

  const char* timezone =
      services::weather::timezone();

  if (timezone == nullptr ||
      timezone[0] == '\0') {
    Serial.println(
        "time: timezone is empty");

    return false;
  }

  Serial.printf(
      "time: timezone = %s\n",
      timezone);

  Serial.printf(
      "time: UTC offset = %ld seconds\n",
      services::weather::
          timezoneOffsetSeconds());

  return true;
}

void drawCurrentScreen() {
  if (g_screen ==
      ui::info::Screen::Radar) {

    if (WiFi.status() ==
        WL_CONNECTED) {

      ui::radarDisplayDraw();

      g_radar_visible = true;

      g_last_radar_animation_ms =
          millis();
    }

  } else {

    g_radar_visible = false;

    ui::info::draw(
        g_screen);
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
      ui::radar::rangeCurrent()
          .outer_km);

  if (g_screen ==
          ui::info::Screen::Radar &&
      WiFi.status() ==
          WL_CONNECTED) {

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
      millis() -
              g_pending_tap_ms >
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

  g_last_radar_animation_ms =
      now;

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

    /*
     * Resolve timezone from coordinates
     * before starting the local clock.
     */
    setupTimezone();

    /*
     * Synchronize NTP using UTC plus the
     * automatically detected local offset.
     */
    setupTime();

    services::weather::update(
        services::location::lat(),
        services::location::lon());

    /*
     * ADS-B remains in the background
     * so the radar animation is not blocked
     * by HTTPS requests.
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
  handleBootButton();

  wifiLoop();

  /*
   * Radar animation is independent from
   * the ADS-B network task.
   */
  updateRadarAnimation();

  if (WiFi.status() !=
      WL_CONNECTED) {

    if (g_radar_visible) {

      Serial.println(
          "WiFi lost — will reconnect");

      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since =
          millis();
    }

    const unsigned long down_ms =
        millis() -
        g_wifi_down_since;

    if (down_ms >=
            config::kWifiDownGraceMs &&
        millis() -
                g_last_reconnect_ms >=
            config::
                kWifiReconnectIntervalMs) {

      g_last_reconnect_ms =
          millis();

      if (wifiReconnect()) {

        g_wifi_down_since = 0;

        /*
         * Resolve timezone again after
         * Wi-Fi reconnect.
         */
        services::weather::
            updateTimezone(
                services::location::lat(),
                services::location::lon());

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

  delay(1);
}
