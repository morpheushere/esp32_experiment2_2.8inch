#pragma once

#include <math.h>

#include <lvgl.h>

// A trend's direction is one of "rising" | "falling" | "steady", or nullptr
// when the backend doesn't have enough data yet (e.g. a fresh sensor).
struct WeatherTrend {
  const char *direction = nullptr;
};

// Mirrors the shape of GET /api/weather/current. Any numeric field can be
// NAN and any trend direction can be nullptr — both render as "--".
struct WeatherReading {
  float temp_c = NAN;
  float humidity_pct = NAN;
  float pressure_hpa = NAN;
  float wind_speed = NAN;
  WeatherTrend temp_trend;
  WeatherTrend humidity_trend;
  WeatherTrend pressure_trend;
  WeatherTrend wind_trend;
};

// Creates the tab 1 layout (accent bar, temperature readout, sensor cards,
// status line) on `tab`. Call once from ui_tabview's setup.
void build_weather_tab(lv_obj_t *tab);

// Updates all value/trend widgets and the temperature-gradient accent bar
// from a freshly parsed reading. Safe to call with NAN/nullptr fields.
void weather_apply_reading(const WeatherReading &reading);

// Sets the bottom status line text (e.g. "WiFi connecting...",
// "Updated 12s ago", "Stale - last update 3m ago"). Purely cosmetic;
// weather_client owns the timing/state logic behind this string.
void weather_set_status_line(const char *text);
