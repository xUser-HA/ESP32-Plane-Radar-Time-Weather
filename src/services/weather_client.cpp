#include "services/weather_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace services::weather {

namespace {

CurrentWeather s_weather;

unsigned long s_last_fetch_ms = 0;

constexpr unsigned long kRefreshMs =
    10UL * 60UL * 1000UL;

bool s_timezone_valid = false;

char s_timezone[40] = {0};

}  // namespace

const CurrentWeather& current() {
  return s_weather;
}

const char* descriptionRu(int c) {
  switch (c) {
    case 0:
      return "Ясно";

    case 1:
      return "Преимущ. ясно";

    case 2:
      return "Переменная обл.";

    case 3:
      return "Пасмурно";

    case 45:
    case 48:
      return "Туман";

    case 51:
    case 53:
    case 55:
      return "Морось";

    case 56:
    case 57:
      return "Лед. морось";

    case 61:
    case 63:
    case 65:
      return "Дождь";

    case 66:
    case 67:
      return "Лед. дождь";

    case 71:
    case 73:
    case 75:
      return "Снег";

    case 77:
      return "Снежные зерна";

    case 80:
    case 81:
    case 82:
      return "Ливень";

    case 85:
    case 86:
      return "Снегопад";

    case 95:
      return "Гроза";

    case 96:
    case 99:
      return "Гроза с градом";

    default:
      return "Нет данных";
  }
}

bool update(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  const unsigned long now =
      millis();

  if (s_weather.valid &&
      now - s_last_fetch_ms < kRefreshMs) {
    return true;
  }

  char url[320];

  snprintf(
      url,
      sizeof(url),
      "https://api.open-meteo.com/v1/forecast"
      "?latitude=%.6f"
      "&longitude=%.6f"
      "&current="
      "temperature_2m,"
      "relative_humidity_2m,"
      "apparent_temperature,"
      "precipitation,"
      "weather_code,"
      "surface_pressure,"
      "wind_speed_10m,"
      "wind_direction_10m"
      "&timezone=auto",
      lat,
      lon);

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(8000);

  if (!http.begin(
          client,
          url)) {
    return false;
  }

  const int code =
      http.GET();

  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;

  const DeserializationError err =
      deserializeJson(
          doc,
          http.getStream());

  http.end();

  if (err) {
    return false;
  }

  /*
   * Open-Meteo returns the IANA timezone
   * corresponding to the requested coordinates.
   *
   * Example:
   *   Asia/Almaty
   *   Europe/Moscow
   *   Asia/Tokyo
   */
  const char* timezone =
      doc["timezone"];

  if (timezone != nullptr &&
      timezone[0] != '\0') {
    strncpy(
        s_timezone,
        timezone,
        sizeof(s_timezone) - 1);

    s_timezone[
        sizeof(s_timezone) - 1] =
        '\0';

    s_timezone_valid = true;

    Serial.printf(
        "weather: timezone = %s\n",
        s_timezone);
  }

  JsonObject cur =
      doc["current"];

  if (cur.isNull()) {
    return false;
  }

  CurrentWeather w;

  w.temperature_c =
      cur["temperature_2m"] |
      0.0f;

  w.apparent_c =
      cur["apparent_temperature"] |
      0.0f;

  w.humidity_pct =
      cur["relative_humidity_2m"] |
      0.0f;

  w.pressure_hpa =
      cur["surface_pressure"] |
      0.0f;

  w.wind_ms =
      (cur["wind_speed_10m"] |
       0.0f) /
      3.6f;

  w.wind_deg =
      cur["wind_direction_10m"] |
      0.0f;

  w.weather_code =
      cur["weather_code"] |
      -1;

  w.valid = true;

  s_weather = w;

  s_last_fetch_ms = now;

  return true;
}

bool updateTimezone(
    double lat,
    double lon) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (s_timezone_valid) {
    return true;
  }

  s_last_fetch_ms = 0;

  return update(lat, lon) &&
         s_timezone_valid;
}

const char* timezone() {
  if (!s_timezone_valid) {
    return "";
  }

  return s_timezone;
}

}  // namespace services::weather
