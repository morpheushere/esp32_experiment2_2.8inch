#pragma once

#include <math.h>

#include <lvgl.h>

#define WEATHER_TREND_SERIES_MAX 10

// A trend's direction is one of "rising" | "falling" | "steady", or nullptr
// when the backend doesn't have enough data yet (e.g. a fresh sensor).
// `series` holds up to WEATHER_TREND_SERIES_MAX recent readings, oldest
// first, for the sparkline; `count` may be less than that (or 0).
struct WeatherTrend {
  const char *direction = nullptr;
  float series[WEATHER_TREND_SERIES_MAX] = {};
  int count = 0;
};

// Mirrors the shape of GET /api/weather/current. Any numeric field can be
// NAN and any trend direction can be nullptr — both render as "--".
struct WeatherReading {
  float temp_c = NAN;
  float humidity_pct = NAN;
  float pressure_hpa = NAN;
  float wind_speed = NAN;
  float rainfall = NAN;
  WeatherTrend temp_trend;
  WeatherTrend humidity_trend;
  WeatherTrend pressure_trend;
  WeatherTrend wind_trend;
};

// "RAIN" | "WINDY" | "PRESSURE RISING" | "PRESSURE FALLING" | "CLEAR".
// Ported from ESP32_experiment1/src/weather_client.cpp conditionLabel().
const char *condition_label(const WeatherReading &r);

// Creates the tab 1 scene: an animated canvas (Bauhaus-poster ambient
// weather display, matching ESP32_experiment1's visual design) plus a
// small status-line overlay. Call once from ui_tabview's setup.
void build_weather_tab(lv_obj_t *tab);

// Sets the animation's targets (temperature, wind, pressure trend) from a
// freshly parsed reading. The scene eases toward these rather than
// snapping, matching the source animation's smoothing. Safe to call with
// NAN/nullptr fields.
void weather_apply_reading(const WeatherReading &reading);

// Starts/stops the ~10fps animation timer. Call with `false` when tab 1
// isn't the active tab to save CPU/heat; `true` when it becomes active
// again. Does not affect the canvas's one-time memory allocation.
void weather_set_scene_running(bool running);

// Sets the small status-line overlay text (e.g. "WiFi connecting...",
// "Updated 12s ago", "Stale - last update 3m ago"). Purely cosmetic;
// weather_client owns the timing/state logic behind this string.
void weather_set_status_line(const char *text);
