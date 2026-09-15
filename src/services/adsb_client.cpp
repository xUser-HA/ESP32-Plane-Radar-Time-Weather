#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] =
    "https://opendata.adsb.fi/api/v3/lat/";

constexpr float kKmPerNm = 1.852f;
constexpr float kFeetToMeters = 0.3048f;

constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000UL;

constexpr unsigned long kDefaultBackgroundIntervalMs =
    5000UL;

constexpr uint32_t kTaskStackSize = 8192;
constexpr UBaseType_t kTaskPriority = 1;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;

PollFn s_poll_fn = nullptr;

TaskHandle_t s_adsb_task = nullptr;
SemaphoreHandle_t s_aircraft_mutex = nullptr;

double s_background_lat = 0.0;
double s_background_lon = 0.0;
float s_background_radius_km = 25.0f;
unsigned long s_background_interval_ms =
    kDefaultBackgroundIntervalMs;

bool s_background_started = false;

/*
 * Network task builds the new aircraft list here first.
 * The public display array is updated only after the complete
 * HTTP response has been parsed.
 */
Aircraft s_pending_aircraft[kMaxAircraft];
size_t s_pending_aircraft_count = 0;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(
      kConnectAttemptMs);

  const unsigned long deadline =
      millis() + kRequestTimeoutMs;

  while (millis() < deadline) {
    pollNetwork();

    const int code =
        http.GET();

    if (code > 0) {
      return code;
    }

    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }

    delay(5);
  }

  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(
    HTTPClient& http,
    String& payload) {
  WiFiClient* stream =
      http.getStreamPtr();

  if (stream == nullptr) {
    return false;
  }

  const int content_length =
      http.getSize();

  if (content_length > 0) {
    payload.reserve(
        static_cast<unsigned>(
            content_length + 1));
  }

  uint8_t buffer[512];

  const unsigned long deadline =
      millis() + kRequestTimeoutMs;

  while (millis() < deadline) {
    pollNetwork();

    const int available =
        stream->available();

    if (available > 0) {
      const int to_read =
          available >
                  static_cast<int>(
                      sizeof(buffer))
              ? static_cast<int>(
                    sizeof(buffer))
              : available;

      const int read_bytes =
          stream->readBytes(
              buffer,
              to_read);

      if (read_bytes > 0) {
        payload.concat(
            reinterpret_cast<
                const char*>(buffer),
            static_cast<unsigned>(
                read_bytes));
      }
    }

    if (content_length > 0 &&
        static_cast<int>(
            payload.length()) >=
            content_length) {
      break;
    }

    if (!http.connected() &&
        stream->available() <= 0) {
      break;
    }

    delay(1);
  }

  return payload.length() > 0;
}

float kmToNauticalMiles(float km) {
  return km / kKmPerNm;
}

bool readJsonFloat(
    const JsonObject& obj,
    const char* key,
    float* out) {
  if (obj[key].is<float>() ||
      obj[key].is<double>() ||
      obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }

  return false;
}

float pickNoseHeading(
    const JsonObject& plane) {
  float v = 0.0f;

  if (readJsonFloat(
          plane,
          "true_heading",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "mag_heading",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "track",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "dir",
          &v)) {
    return v;
  }

  return 0.0f;
}

float pickTrackHeading(
    const JsonObject& plane) {
  float v = 0.0f;

  if (readJsonFloat(
          plane,
          "track",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "true_heading",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "mag_heading",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "dir",
          &v)) {
    return v;
  }

  return 0.0f;
}

float pickGroundSpeed(
    const JsonObject& plane) {
  float v = 0.0f;

  if (readJsonFloat(
          plane,
          "gs",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "tas",
          &v)) {
    return v;
  }

  if (readJsonFloat(
          plane,
          "ias",
          &v)) {
    return v;
  }

  return 0.0f;
}

bool isOnGround(
    const JsonObject& plane) {
  if (!plane["alt_baro"]
           .is<const char*>()) {
    return false;
  }

  return strcmp(
             plane["alt_baro"]
                 .as<const char*>(),
             "ground") == 0;
}

void copyJsonStringTrimmed(
    const JsonObject& obj,
    const char* key,
    char* out,
    size_t out_len) {
  out[0] = '\0';

  if (out_len == 0 ||
      !obj[key].is<const char*>()) {
    return;
  }

  const char* s =
      obj[key].as<const char*>();

  size_t n =
      strnlen(
          s,
          out_len - 1);

  while (n > 0 &&
         s[n - 1] == ' ') {
    --n;
  }

  memcpy(
      out,
      s,
      n);

  out[n] = '\0';
}

void formatAltitudeTag(
    const JsonObject& plane,
    char* out,
    size_t out_len) {
  out[0] = '\0';

  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"]
          .is<const char*>()) {
    const char* s =
        plane["alt_baro"]
            .as<const char*>();

    if (strcmp(s, "ground") == 0) {
      strncpy(
          out,
          "GND",
          out_len - 1);

      out[out_len - 1] =
          '\0';

      return;
    }
  }

  float alt_ft = 0.0f;

  if (readJsonFloat(
          plane,
          "alt_baro",
          &alt_ft) ||
      readJsonFloat(
          plane,
          "alt_geom",
          &alt_ft)) {
    const float alt_m =
        alt_ft *
        kFeetToMeters;

    const int alt_m_rounded =
        static_cast<int>(
            lroundf(
                alt_m / 10.0f) *
            10);

    snprintf(
        out,
        out_len,
        "%d m",
        alt_m_rounded);
  }
}

void fillTagFields(
    Aircraft* ac,
    const JsonObject& plane) {
  copyJsonStringTrimmed(
      plane,
      "flight",
      ac->callsign,
      sizeof(ac->callsign));

  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(
        plane,
        "hex",
        ac->callsign,
        sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(
      plane,
      "t",
      ac->type,
      sizeof(ac->type));

  formatAltitudeTag(
      plane,
      ac->alt,
      sizeof(ac->alt));
}

/*
 * Publish a completely prepared aircraft list.
 *
 * The network task never writes directly into the array that
 * the display is currently reading until the complete list
 * is ready.
 */
void publishAircraftList() {
  if (s_aircraft_mutex == nullptr) {
    return;
  }

  if (xSemaphoreTake(
          s_aircraft_mutex,
          pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  memcpy(
      s_aircraft,
      s_pending_aircraft,
      sizeof(s_aircraft));

  s_aircraft_count =
      s_pending_aircraft_count;

  xSemaphoreGive(
      s_aircraft_mutex);
}

void adsbBackgroundTask(
    void* parameter) {
  (void)parameter;

  Serial.println(
      "adsb: background task started");

  for (;;) {
    if (WiFi.status() ==
        WL_CONNECTED) {
      const bool ok =
          fetchUpdate(
              s_background_lat,
              s_background_lon,
              s_background_radius_km);

      if (!ok) {
        Serial.println(
            "adsb: background update failed");
      }
    }

    vTaskDelay(
        pdMS_TO_TICKS(
            s_background_interval_ms));
  }
}

}  // namespace

void setPollFn(PollFn fn) {
  s_poll_fn = fn;
}

size_t aircraftCount() {
  return s_aircraft_count;
}

const Aircraft* aircraftList() {
  return s_aircraft;
}

bool fetchUpdate(
    double center_lat,
    double center_lon,
    float fetch_radius_km) {
  const float dist_nm =
      kmToNauticalMiles(
          fetch_radius_km);

  String url = kApiBase;

  url +=
      String(
          center_lat,
          6);

  url +=
      "/lon/";

  url +=
      String(
          center_lon,
          6);

  url +=
      "/dist/";

  url +=
      String(
          dist_nm,
          1);

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  if (!http.begin(
          client,
          url)) {
    Serial.println(
        "adsb: http.begin failed");

    return false;
  }

  http.useHTTP10(true);

  http.setTimeout(
      kRequestTimeoutMs);

  const int code =
      performGetWithPoll(
          http);

  if (code != HTTP_CODE_OK) {
    Serial.printf(
        "adsb: HTTP %d\n",
        code);

    http.end();

    return false;
  }

  String payload;

  if (!readResponseBodyWithPoll(
          http,
          payload)) {
    Serial.println(
        "adsb: empty response");

    http.end();

    return false;
  }

  http.end();

  JsonDocument doc;

  const DeserializationError err =
      deserializeJson(
          doc,
          payload);

  if (err) {
    Serial.printf(
        "adsb: JSON parse error: %s\n",
        err.c_str());

    return false;
  }

  JsonArray ac =
      doc["ac"].as<JsonArray>();

  if (ac.isNull()) {
    s_pending_aircraft_count = 0;

    publishAircraftList();

    Serial.println(
        "adsb: 0 aircraft");

    return true;
  }

  size_t n = 0;

  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }

    if (!plane["lat"].is<float>() ||
        !plane["lon"].is<float>()) {
      continue;
    }

    if (isOnGround(plane) &&
        !config::kAdsbShowGroundAircraft) {
      continue;
    }

    s_pending_aircraft[n].lat =
        plane["lat"].as<float>();

    s_pending_aircraft[n].lon =
        plane["lon"].as<float>();

    s_pending_aircraft[n].nose_deg =
        pickNoseHeading(
            plane);

    s_pending_aircraft[n].track_deg =
        pickTrackHeading(
            plane);

    s_pending_aircraft[n].gs_knots =
        pickGroundSpeed(
            plane);

    fillTagFields(
        &s_pending_aircraft[n],
        plane);

    ++n;
  }

  s_pending_aircraft_count =
      n;

  publishAircraftList();

  Serial.printf(
      "adsb: %u aircraft\n",
      static_cast<unsigned>(
          n));

  return true;
}

void startBackgroundUpdates(
    double center_lat,
    double center_lon,
    float fetch_radius_km,
    unsigned long interval_ms) {
  /*
   * Do not create the task twice.
   */
  if (s_background_started) {
    return;
  }

  /*
   * Store the parameters used by the background task.
   */
  s_background_lat =
      center_lat;

  s_background_lon =
      center_lon;

  s_background_radius_km =
      fetch_radius_km;

  if (interval_ms < 1000UL) {
    interval_ms = 1000UL;
  }

  s_background_interval_ms =
      interval_ms;

  /*
   * Mutex protects publication of the aircraft list.
   */
  s_aircraft_mutex =
      xSemaphoreCreateMutex();

  if (s_aircraft_mutex == nullptr) {
    Serial.println(
        "adsb: mutex creation failed");

    return;
  }

  s_aircraft_count = 0;
  s_pending_aircraft_count = 0;

  /*
   * ESP32-C3 is a single-core chip, so there is no need
   * to pin this task to a particular core.
   *
   * 8192 bytes gives the HTTPS/JSON task enough stack space.
   */
  const BaseType_t result =
      xTaskCreate(
          adsbBackgroundTask,
          "adsbTask",
          kTaskStackSize,
          nullptr,
          kTaskPriority,
          &s_adsb_task);

  if (result != pdPASS) {
    Serial.println(
        "adsb: task creation failed");

    vSemaphoreDelete(
        s_aircraft_mutex);

    s_aircraft_mutex =
        nullptr;

    s_adsb_task =
        nullptr;

    return;
  }

  s_background_started = true;

  Serial.println(
      "adsb: background updates enabled");
}

}  // namespace services::adsb
