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

constexpr uint32_t kHttpTimeoutMs = 15000;

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

  Serial.println();
  Serial.println("========== WEATHER UPDATE ==========");

  /*
   * 1. Check Wi-Fi.
   */
  if (WiFi.status() != WL_CONNECTED) {

    Serial.println(
        "[WEATHER] ERROR: WiFi not connected"
    );

    return false;
  }

  Serial.print(
      "[WEATHER] WiFi OK, IP: "
  );

  Serial.println(
      WiFi.localIP()
  );


  /*
   * 2. Use cached data if it is still fresh.
   */
  const unsigned long now = millis();

  if (
      s_weather.valid &&
      now - s_last_fetch_ms < kRefreshMs
  ) {

    Serial.println(
        "[WEATHER] Using cached data"
    );

    return true;
  }


  /*
   * 3. Check coordinates.
   */
  if (
      isnan(lat) ||
      isnan(lon) ||
      lat < -90.0 ||
      lat > 90.0 ||
      lon < -180.0 ||
      lon > 180.0
  ) {

    Serial.printf(
        "[WEATHER] ERROR: invalid coordinates: %.6f, %.6f\n",
        lat,
        lon
    );

    return false;
  }


  Serial.printf(
      "[WEATHER] Coordinates: %.6f, %.6f\n",
      lat,
      lon
  );


  /*
   * 4. Build Open-Meteo URL.
   *
   * Open-Meteo supports all of these variables
   * through the current= parameter.
   */
  char url[512];

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
      lon
  );


  Serial.println(
      "[WEATHER] Request:"
  );

  Serial.println(
      url
  );


  /*
   * 5. HTTPS connection.
   *
   * Open-Meteo uses HTTPS.
   *
   * setInsecure() is intentionally used here because
   * ESP32 does not have a certificate store configured
   * by this project.
   */
  WiFiClientSecure client;

  client.setInsecure();

  client.setTimeout(
      kHttpTimeoutMs
  );


  HTTPClient http;

  http.setConnectTimeout(
      kHttpTimeoutMs
  );

  http.setTimeout(
      kHttpTimeoutMs
  );

  /*
   * Follow HTTPS redirects if the endpoint returns one.
   */
  http.setFollowRedirects(
      HTTPC_FORCE_FOLLOW_REDIRECTS
  );


  /*
   * 6. Start HTTP connection.
   */
  if (!http.begin(client, url)) {

    Serial.println(
        "[WEATHER] ERROR: http.begin() failed"
    );

    http.end();

    return false;
  }


  /*
   * Tell the server what we accept.
   */
  http.addHeader(
      "Accept",
      "application/json"
  );


  http.addHeader(
      "User-Agent",
      "ESP32-Plane-Radar/1.13"
  );


  Serial.println(
      "[WEATHER] Sending GET..."
  );


  /*
   * 7. Perform request.
   */
  const int code =
      http.GET();


  Serial.printf(
      "[WEATHER] HTTP result: %d\n",
      code
  );


  /*
   * Negative values are ESP32/HTTPClient errors.
   */
  if (code < 0) {

    Serial.print(
        "[WEATHER] HTTP error: "
    );

    Serial.println(
        HTTPClient::errorToString(code)
    );

    http.end();

    return false;
  }


  /*
   * 8. HTTP status must be 200.
   */
  if (code != HTTP_CODE_OK) {

    Serial.printf(
        "[WEATHER] ERROR: HTTP status %d\n",
        code
    );


    /*
     * Print server response for diagnostics.
     */
    String errorBody =
        http.getString();

    Serial.println(
        "[WEATHER] Server response:"
    );

    Serial.println(
        errorBody
    );


    http.end();

    return false;
  }


  /*
   * 9. Check response size.
   */
  const int contentLength =
      http.getSize();

  Serial.printf(
      "[WEATHER] Content-Length: %d\n",
      contentLength
  );


  /*
   * 10. Parse JSON.
   *
   * Open-Meteo's current response is small,
   * so 16 KB gives ArduinoJson plenty of room.
   */
  JsonDocument doc;

  const DeserializationError err =
      deserializeJson(
          doc,
          http.getStream()
      );


  if (err) {

    Serial.print(
        "[WEATHER] JSON error: "
    );

    Serial.println(
        err.c_str()
    );


    http.end();

    return false;
  }


  /*
   * HTTP connection can now be closed.
   */
  http.end();


  /*
   * 11. Check "current" object.
   */
  JsonObject cur =
      doc["current"];


  if (cur.isNull()) {

    Serial.println(
        "[WEATHER] ERROR: JSON has no 'current'"
    );

    return false;
  }


  /*
   * 12. Read weather data.
   */
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


  /*
   * Open-Meteo returns wind speed in km/h
   * by default.
   *
   * Convert km/h -> m/s.
   */
  w.wind_ms =
      (
          cur["wind_speed_10m"] |
          0.0f
      ) / 3.6f;


  w.wind_deg =
      cur["wind_direction_10m"] |
      0.0f;


  w.weather_code =
      cur["weather_code"] |
      -1;


  w.valid = true;


  /*
   * 13. Save data.
   */
  s_weather =
      w;


  s_last_fetch_ms =
      now;


  /*
   * 14. Print successful result.
   */
  Serial.println(
      "[WEATHER] SUCCESS"
  );


  Serial.printf(
      "  Temperature: %.1f C\n",
      w.temperature_c
  );


  Serial.printf(
      "  Feels like:  %.1f C\n",
      w.apparent_c
  );


  Serial.printf(
      "  Humidity:     %.0f %%\n",
      w.humidity_pct
  );


  Serial.printf(
      "  Pressure:     %.1f hPa\n",
      w.pressure_hpa
  );


  Serial.printf(
      "  Wind:         %.1f m/s\n",
      w.wind_ms
  );


  Serial.printf(
      "  Wind dir:     %.0f deg\n",
      w.wind_deg
  );


  Serial.printf(
      "  Weather code: %d (%s)\n",
      w.weather_code,
      descriptionRu(
          w.weather_code
      )
  );


  Serial.println(
      "===================================="
  );


  return true;
}


}  // namespace services::weather
