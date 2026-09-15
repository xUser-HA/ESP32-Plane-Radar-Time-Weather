#include "ui/info_screens.h"

#include <Arduino.h>
#include <time.h>
#include <cstdio>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "services/weather_client.h"

namespace ui::info {

namespace {
constexpr int cx = config::kDisplayWidth / 2;
constexpr int cy = config::kDisplayHeight / 2;

void style(float size, uint16_t fg, uint16_t bg = config::kColorBlack) {
  tft.fillScreen(bg);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(textdatum_t::middle_center);
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, size);
  } else {
    tft.setTextSize(1);
  }
}

void line(const char* s, int y) {
  tft.drawString(s, cx, y);
}

const char* windDir(float deg) {
  static const char* dirs[] = {"С","СВ","В","ЮВ","Ю","ЮЗ","З","СЗ"};
  int i = (int)((deg + 22.5f) / 45.0f) % 8;
  return dirs[i];
}

}  // namespace

void drawWeather() {
  style(0.72f, config::kTextOnBlack);

  const auto& w = services::weather::current();
  line("ПОГОДА", 25);

  if (!w.valid) {
    line("Нет данных", 82);
    line("Проверьте Wi-Fi", 112);
    line("и повторите позже", 142);
    return;
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "%.1f C", w.temperature_c);
  if (displayFontIsSmooth()) displayFontSetSmoothSize(tft, 1.25f);
  line(buf, 72);

  if (displayFontIsSmooth()) displayFontSetSmoothSize(tft, 0.62f);
  line(services::weather::descriptionRu(w.weather_code), 105);

  snprintf(buf, sizeof(buf), "Ощущается %.1f C", w.apparent_c);
  line(buf, 128);
  snprintf(buf, sizeof(buf), "Влажность %.0f%%", w.humidity_pct);
  line(buf, 150);
  snprintf(buf, sizeof(buf), "Ветер %.1f м/с %s", w.wind_ms, windDir(w.wind_deg));
  line(buf, 172);
  snprintf(buf, sizeof(buf), "Давление %.0f hPa", w.pressure_hpa);
  line(buf, 194);
}

void drawClock() {
  style(0.8f, config::kTextOnBlack);
  line("ЧАСЫ", 28);

  struct tm tm_now;
  if (!getLocalTime(&tm_now, 50)) {
    line("--:--:--", 100);
    line("Синхронизация NTP", 145);
    return;
  }

  char timebuf[16], datebuf[24], daybuf[24];
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_now);
  strftime(datebuf, sizeof(datebuf), "%d.%m.%Y", &tm_now);

  static const char* days[] = {
      "Воскресенье", "Понедельник", "Вторник", "Среда",
      "Четверг", "Пятница", "Суббота"};
  snprintf(daybuf, sizeof(daybuf), "%s", days[tm_now.tm_wday]);

  if (displayFontIsSmooth()) displayFontSetSmoothSize(tft, 1.55f);
  line(timebuf, 92);
  if (displayFontIsSmooth()) displayFontSetSmoothSize(tft, 0.72f);
  line(datebuf, 135);
  line(daybuf, 168);
  line("UTC+5  Алматы", 198);
}

void draw(Screen screen) {
  if (screen == Screen::Weather) drawWeather();
  else if (screen == Screen::Clock) drawClock();
}

}  // namespace ui::info
