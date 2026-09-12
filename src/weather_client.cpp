#include "weather_client.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "secrets.h"
#include "tab_weather.h"

// Mock rendering/null-handling checklist passed; secrets.h now has real
// WiFi/NAS values, so this is off and the real HTTP path below is live.
// Re-enable to bypass WiFi/HTTP with a fixed sample payload again.
// #define USE_MOCK_WEATHER_DATA

namespace {

constexpr unsigned long POLL_INTERVAL_MS = 60000;
constexpr unsigned long MOCK_POLL_INTERVAL_MS = 5000;
constexpr unsigned long CONNECT_ATTEMPT_TIMEOUT_MS = 15000;
constexpr unsigned long BACKOFF_SCHEDULE_MS[] = {1000, 2000, 5000, 10000, 30000};
constexpr size_t BACKOFF_STEPS = sizeof(BACKOFF_SCHEDULE_MS) / sizeof(BACKOFF_SCHEDULE_MS[0]);

// Rainfall is deliberately non-null and above the RAIN threshold, and three
// of the four trends carry a real series -- between this and the nulls
// left on wind_trend, every condition_label() branch and both sparkline
// code paths (real series vs. count<2 placeholder) get exercised in mock
// mode before the real NAS host:port is even filled in.
const char *MOCK_WEATHER_JSON = R"json(
{
  "temp_c": 21.7, "humidity_pct": 46.0, "pressure_hpa": 1013.2,
  "wind_speed": 4.0, "wind_dir": null, "rainfall": 0.3,
  "temp_trend": {"direction": "rising", "delta": 0.3, "series": [20.1, 20.4, 20.9, 21.2, 21.7]},
  "humidity_trend": {"direction": "steady", "delta": 0.2, "series": [45.1, 45.6, 45.9, 46.2, 46.0]},
  "pressure_trend": {"direction": "falling", "delta": -1.1, "series": [1016.0, 1015.1, 1014.0, 1013.6, 1013.2]},
  "wind_trend": {"direction": null, "delta": null, "series": []}
}
)json";

enum class WifiState { CONNECTING, CONNECTED, BACKOFF };

WifiState g_wifi_state = WifiState::CONNECTING;
unsigned long g_wifi_state_change_ms = 0;
uint8_t g_backoff_index = 0;

unsigned long g_last_poll_ms = 0;
unsigned long g_last_success_ms = 0;  // 0 = never fetched successfully
bool g_stale = false;

void set_stale(bool stale) { g_stale = stale; }

unsigned long current_backoff_ms() {
  size_t idx = g_backoff_index < BACKOFF_STEPS ? g_backoff_index : BACKOFF_STEPS - 1;
  return BACKOFF_SCHEDULE_MS[idx];
}

void format_elapsed(char *buf, size_t len, unsigned long elapsed_s) {
  if (elapsed_s < 60) {
    snprintf(buf, len, "%lus", elapsed_s);
  } else {
    snprintf(buf, len, "%lum", elapsed_s / 60);
  }
}

// Throttled to once/sec so the "updated Xs ago" text ticks up smoothly
// without hammering LVGL with redundant redraws every loop() iteration.
// Only takes over once a first successful fetch has landed — before that,
// the WiFi state machine drives the status line directly.
void refresh_status_line() {
  static unsigned long last_render_ms = 0;
  if (millis() - last_render_ms < 1000) return;
  last_render_ms = millis();

  if (g_last_success_ms == 0) return;

  unsigned long elapsed_s = (millis() - g_last_success_ms) / 1000;
  char elapsed_buf[16];
  format_elapsed(elapsed_buf, sizeof(elapsed_buf), elapsed_s);

  char buf[48];
  if (g_stale) {
    snprintf(buf, sizeof(buf), "Stale - last update %s ago", elapsed_buf);
  } else {
    snprintf(buf, sizeof(buf), "Updated %s ago", elapsed_buf);
  }
  weather_set_status_line(buf);
}

// A trend object can be entirely absent, or present with a null direction
// and/or empty series -- every case degrades to WeatherTrend's own
// nullptr/count==0 defaults rather than crashing or misreading.
void parse_trend(JsonVariantConst v, WeatherTrend &t) {
  t.direction = v["direction"] | static_cast<const char *>(nullptr);
  t.count = 0;
  JsonArrayConst series = v["series"];
  if (series.isNull()) return;
  for (JsonVariantConst s : series) {
    if (t.count >= WEATHER_TREND_SERIES_MAX) break;
    t.series[t.count++] = s.as<float>();
  }
}

// Shared by both the mock path and the real HTTP path: extracts every
// field null-safely (any of them can legitimately be absent/null per the
// API's own documented degradation behavior) and pushes the result into
// the weather tab's widgets.
void apply_weather_json(JsonDocument &doc) {
  WeatherReading r;
  r.temp_c = doc["temp_c"] | NAN;
  r.humidity_pct = doc["humidity_pct"] | NAN;
  r.pressure_hpa = doc["pressure_hpa"] | NAN;
  r.wind_speed = doc["wind_speed"] | NAN;
  r.rainfall = doc["rainfall"] | NAN;
  parse_trend(doc["temp_trend"], r.temp_trend);
  parse_trend(doc["humidity_trend"], r.humidity_trend);
  parse_trend(doc["pressure_trend"], r.pressure_trend);
  parse_trend(doc["wind_trend"], r.wind_trend);

  Serial.printf(
      "[JSON] parsed OK: temp_c=%.1f humidity_pct=%.1f pressure_hpa=%.1f wind_speed=%.1f "
      "wind_dir=%s rainfall=%s\n",
      r.temp_c, r.humidity_pct, r.pressure_hpa, r.wind_speed,
      doc["wind_dir"].isNull() ? "null" : "set", doc["rainfall"].isNull() ? "null" : "set");

  weather_apply_reading(r);

  float display_f = isnan(r.temp_c) ? NAN : (r.temp_c * 9.0f / 5.0f + 32.0f);
  Serial.printf("[UI] scene updated: %.0fF (%.1fC), condition=%s\n", display_f, r.temp_c,
                condition_label(r));
}

#ifdef USE_MOCK_WEATHER_DATA

void mock_poll_tick() {
  static unsigned long last_mock_ms = 0;
  if (last_mock_ms != 0 && millis() - last_mock_ms < MOCK_POLL_INTERVAL_MS) return;
  last_mock_ms = millis();

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, MOCK_WEATHER_JSON);
  if (err == DeserializationError::Ok) {
    apply_weather_json(doc);
    g_last_success_ms = millis();
    set_stale(false);
  } else {
    Serial.printf("[Mock] JSON parse failed: %s\n", err.c_str());
  }
}

#else

void advance_wifi_state_machine() {
  switch (g_wifi_state) {
    case WifiState::CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
        g_wifi_state = WifiState::CONNECTED;
        g_backoff_index = 0;
        if (g_last_success_ms == 0) weather_set_status_line("Loading realtime weather...");
      } else if (millis() - g_wifi_state_change_ms > CONNECT_ATTEMPT_TIMEOUT_MS) {
        unsigned long backoff = current_backoff_ms();
        Serial.printf("[WiFi] connect failed, retrying in %lus\n", backoff / 1000);
        if (g_last_success_ms == 0) {
          char buf[48];
          snprintf(buf, sizeof(buf), "WiFi down, retrying in %lus", backoff / 1000);
          weather_set_status_line(buf);
        } else {
          set_stale(true);
        }
        g_wifi_state = WifiState::BACKOFF;
        g_wifi_state_change_ms = millis();
        if (g_backoff_index < BACKOFF_STEPS - 1) g_backoff_index++;
      }
      break;

    case WifiState::BACKOFF:
      if (millis() - g_wifi_state_change_ms > current_backoff_ms()) {
        Serial.printf("[WiFi] connecting to SSID '%s'...\n", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        g_wifi_state = WifiState::CONNECTING;
        g_wifi_state_change_ms = millis();
        if (g_last_success_ms == 0) weather_set_status_line("Loading realtime weather...");
      }
      break;

    case WifiState::CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] connection lost, reconnecting...");
        if (g_last_success_ms != 0) set_stale(true);
        g_wifi_state = WifiState::CONNECTING;
        g_wifi_state_change_ms = millis();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
      break;
  }
}

void real_poll_tick() {
  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  String url = String(API_BASE_URL) + "/api/weather/current";
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err == DeserializationError::Ok) {
      Serial.printf("[HTTP] GET %s -> 200 OK (%d bytes)\n", url.c_str(), body.length());
      apply_weather_json(doc);
      g_last_success_ms = millis();
      set_stale(false);
    } else {
      Serial.printf("[JSON] parse failed: %s (keeping last-known values, stale=true)\n",
                     err.c_str());
      set_stale(true);
    }
  } else {
    Serial.printf("[HTTP] GET failed, code=%d (keeping last-known values, stale=true)\n", code);
    set_stale(true);
  }
  http.end();
}

#endif  // USE_MOCK_WEATHER_DATA

}  // namespace

void weather_client_init() {
#ifdef USE_MOCK_WEATHER_DATA
  Serial.println("[Mock] USE_MOCK_WEATHER_DATA enabled - skipping WiFi/HTTP");
  weather_set_status_line("Mock mode starting...");
#else
  Serial.printf("[WiFi] connecting to SSID '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_wifi_state_change_ms = millis();
  weather_set_status_line("Loading realtime weather...");
#endif
}

void weather_client_tick() {
#ifdef USE_MOCK_WEATHER_DATA
  mock_poll_tick();
#else
  advance_wifi_state_machine();
  if (g_wifi_state == WifiState::CONNECTED) {
    real_poll_tick();
  }
#endif
  refresh_status_line();
}
