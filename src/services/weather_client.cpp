#include "services/weather_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace services::weather {

namespace {

CurrentWeather s_weather;

unsigned long s_last_fetch_ms = 0;

/*
 * Successful weather data is cached for 10 minutes.
 */
constexpr unsigned long kRefreshMs =
    10UL * 60UL * 1000UL;

/*
 * Retry failed requests after 30 seconds,
 * not every second.
 */
constexpr unsigned long kRetryMs =
    30UL * 1000UL;

constexpr uint32_t kHttpTimeoutMs =
    15000;

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
    Serial.println(
        "========== WEATHER UPDATE =========="
    );


    /*
     * Check WiFi.
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
     * Time since last attempt.
     */
    const unsigned long now =
        millis();


    /*
     * Successful data is cached for 10 minutes.
     */
    if (
        s_weather.valid &&
        now - s_last_fetch_ms <
            kRefreshMs
    ) {

        Serial.println(
            "[WEATHER] Using cached data"
        );

        return true;
    }


    /*
     * Failed requests are retried every 30 seconds.
     */
    if (
        !s_weather.valid &&
        s_last_fetch_ms != 0 &&
        now - s_last_fetch_ms <
            kRetryMs
    ) {

        return false;
    }


    /*
     * Validate coordinates.
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
            "[WEATHER] ERROR: invalid coordinates: "
            "%.6f, %.6f\n",
            lat,
            lon
        );

        s_last_fetch_ms = now;

        return false;
    }


    Serial.printf(
        "[WEATHER] Coordinates: %.6f, %.6f\n",
        lat,
        lon
    );


    /*
     * Open-Meteo request.
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
     * HTTPS client.
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
     * IMPORTANT:
     *
     * Open-Meteo may return a chunked HTTP response.
     *
     * ArduinoJson cannot directly parse the raw chunked
     * stream returned by HTTPClient::getStream().
     *
     * HTTP/1.0 prevents chunked transfer encoding.
     */
    http.useHTTP10(true);


    /*
     * Start HTTPS connection.
     */
    if (
        !http.begin(
            client,
            url
        )
    ) {

        Serial.println(
            "[WEATHER] ERROR: http.begin() failed"
        );

        s_last_fetch_ms = now;

        http.end();

        return false;
    }


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
     * Perform GET.
     */
    const int code =
        http.GET();


    Serial.printf(
        "[WEATHER] HTTP result: %d\n",
        code
    );


    /*
     * Negative code = ESP32 HTTP error.
     */
    if (code < 0) {

        Serial.print(
            "[WEATHER] HTTP error: "
        );

        Serial.println(
            HTTPClient::errorToString(
                code
            )
        );

        s_last_fetch_ms = now;

        http.end();

        return false;
    }


    /*
     * HTTP must return 200.
     */
    if (
        code != HTTP_CODE_OK
    ) {

        Serial.printf(
            "[WEATHER] ERROR: HTTP status %d\n",
            code
        );


        String body =
            http.getString();


        Serial.println(
            "[WEATHER] Server response:"
        );

        Serial.println(
            body
        );


        s_last_fetch_ms = now;

        http.end();

        return false;
    }


    /*
     * Get response size.
     */
    const int contentLength =
        http.getSize();


    Serial.printf(
        "[WEATHER] Content-Length: %d\n",
        contentLength
    );


    /*
     * Parse JSON from the HTTP stream.
     *
     * HTTP/1.0 above prevents chunked encoding.
     */
    JsonDocument doc;


    Stream& response =
        http.getStream();


    const DeserializationError err =
        deserializeJson(
            doc,
            response
        );


    if (err) {

        Serial.print(
            "[WEATHER] JSON error: "
        );

        Serial.println(
            err.c_str()
        );


        /*
         * Diagnostic fallback:
         * read any remaining response.
         */
        String remaining =
            response.readString();

        if (
            remaining.length() > 0
        ) {

            Serial.println(
                "[WEATHER] Remaining response:"
            );

            Serial.println(
                remaining
            );
        }


        s_last_fetch_ms = now;

        http.end();

        return false;
    }


    /*
     * Close HTTP connection.
     */
    http.end();


    /*
     * Check current object.
     */
    JsonObject cur =
        doc["current"];


    if (
        cur.isNull()
    ) {

        Serial.println(
            "[WEATHER] ERROR: "
            "JSON has no 'current'"
        );

        s_last_fetch_ms = now;

        return false;
    }


    /*
     * Build weather structure.
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
     * Open-Meteo default wind speed is km/h.
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
     * Store weather data.
     */
    s_weather =
        w;


    s_last_fetch_ms =
        now;


    /*
     * Diagnostics.
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
        "  Humidity:    %.0f %%\n",
        w.humidity_pct
    );


    Serial.printf(
        "  Pressure:    %.1f hPa\n",
        w.pressure_hpa
    );


    Serial.printf(
        "  Wind:        %.1f m/s\n",
        w.wind_ms
    );


    Serial.printf(
        "  Wind dir:    %.0f deg\n",
        w.wind_deg
    );


    Serial.printf(
        "  Weather:     %d (%s)\n",
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
