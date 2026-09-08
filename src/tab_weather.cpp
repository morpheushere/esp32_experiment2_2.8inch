#include "tab_weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "temp_gradient.h"

namespace {

struct Widgets {
  lv_obj_t *accent_bar = nullptr;
  lv_obj_t *temp_label = nullptr;
  lv_obj_t *humidity_value = nullptr;
  lv_obj_t *humidity_arrow = nullptr;
  lv_obj_t *pressure_value = nullptr;
  lv_obj_t *pressure_arrow = nullptr;
  lv_obj_t *wind_value = nullptr;
  lv_obj_t *wind_arrow = nullptr;
  lv_obj_t *status_label = nullptr;
};

Widgets g_widgets;
WeatherReading g_last_reading;
bool g_has_reading = false;
bool g_use_fahrenheit = true;  // US station (mph/hPa) — default per user decision

const char *arrow_glyph(const char *direction) {
  if (direction == nullptr) return "--";
  if (strcmp(direction, "rising") == 0) return LV_SYMBOL_UP;
  if (strcmp(direction, "falling") == 0) return LV_SYMBOL_DOWN;
  if (strcmp(direction, "steady") == 0) return LV_SYMBOL_MINUS;
  return "--";
}

void format_number(char *buf, size_t buf_len, float value, const char *unit, int decimals) {
  if (isnan(value)) {
    snprintf(buf, buf_len, "--");
  } else {
    snprintf(buf, buf_len, "%.*f%s", decimals, value, unit);
  }
}

void format_temp(char *buf, size_t buf_len, float temp_c) {
  if (isnan(temp_c)) {
    snprintf(buf, buf_len, "--");
    return;
  }
  if (g_use_fahrenheit) {
    float f = temp_c * 9.0f / 5.0f + 32.0f;
    snprintf(buf, buf_len, "%.0fF", f);
  } else {
    snprintf(buf, buf_len, "%.1fC", temp_c);
  }
}

lv_obj_t *build_card(lv_obj_t *parent, const char *title, lv_obj_t **value_label_out,
                      lv_obj_t **arrow_label_out) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_set_style_pad_gap(card, 2, 0);

  lv_obj_t *title_label = lv_label_create(card);
  lv_label_set_text(title_label, title);

  lv_obj_t *row = lv_obj_create(card);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 4, 0);
  lv_obj_set_style_border_width(row, 0, 0);

  lv_obj_t *value_label = lv_label_create(row);
  lv_label_set_text(value_label, "--");

  lv_obj_t *arrow_label = lv_label_create(row);
  lv_label_set_text(arrow_label, "--");

  *value_label_out = value_label;
  *arrow_label_out = arrow_label;
  return card;
}

void render_from_cache() {
  char buf[24];

  format_temp(buf, sizeof(buf), g_last_reading.temp_c);
  lv_label_set_text(g_widgets.temp_label, buf);
  lv_obj_set_style_bg_color(g_widgets.accent_bar, color_for_temp_c(g_last_reading.temp_c), 0);

  format_number(buf, sizeof(buf), g_last_reading.humidity_pct, "%", 0);
  lv_label_set_text(g_widgets.humidity_value, buf);
  lv_label_set_text(g_widgets.humidity_arrow, arrow_glyph(g_last_reading.humidity_trend.direction));

  format_number(buf, sizeof(buf), g_last_reading.pressure_hpa, "", 0);
  lv_label_set_text(g_widgets.pressure_value, buf);
  lv_label_set_text(g_widgets.pressure_arrow, arrow_glyph(g_last_reading.pressure_trend.direction));

  format_number(buf, sizeof(buf), g_last_reading.wind_speed, "mph", 0);
  lv_label_set_text(g_widgets.wind_value, buf);
  lv_label_set_text(g_widgets.wind_arrow, arrow_glyph(g_last_reading.wind_trend.direction));
}

void temp_label_click_cb(lv_event_t *e) {
  LV_UNUSED(e);
  g_use_fahrenheit = !g_use_fahrenheit;
  if (g_has_reading) {
    render_from_cache();
  }
}

}  // namespace

void build_weather_tab(lv_obj_t *tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(tab, 0, 0);
  lv_obj_set_style_pad_gap(tab, 4, 0);

  // Accent bar: recolored per-reading via color_for_temp_c().
  g_widgets.accent_bar = lv_obj_create(tab);
  lv_obj_set_size(g_widgets.accent_bar, LV_PCT(100), 8);
  lv_obj_set_style_border_width(g_widgets.accent_bar, 0, 0);
  lv_obj_set_style_radius(g_widgets.accent_bar, 0, 0);
  lv_obj_set_style_bg_color(g_widgets.accent_bar, color_for_temp_c(NAN), 0);

  // Big tappable temperature readout.
  g_widgets.temp_label = lv_label_create(tab);
  lv_label_set_text(g_widgets.temp_label, "--");
  lv_obj_set_style_text_font(g_widgets.temp_label, &lv_font_montserrat_32, 0);
  lv_obj_add_flag(g_widgets.temp_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_widgets.temp_label, temp_label_click_cb, LV_EVENT_CLICKED, nullptr);

  // Humidity / Pressure / Wind cards, left to right.
  lv_obj_t *cards_row = lv_obj_create(tab);
  lv_obj_set_size(cards_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cards_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cards_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(cards_row, 0, 0);

  build_card(cards_row, "Humidity", &g_widgets.humidity_value, &g_widgets.humidity_arrow);
  build_card(cards_row, "Pressure", &g_widgets.pressure_value, &g_widgets.pressure_arrow);
  build_card(cards_row, "Wind", &g_widgets.wind_value, &g_widgets.wind_arrow);

  // Consumes remaining vertical space so the status line below it stays
  // pinned to the bottom of the tab.
  lv_obj_set_flex_grow(cards_row, 1);

  // Status line: doubles as WiFi-connecting/down message before first
  // fetch, and as the "updated Xs ago" / stale indicator afterward.
  g_widgets.status_label = lv_label_create(tab);
  lv_label_set_text(g_widgets.status_label, "Starting...");
}

void weather_apply_reading(const WeatherReading &reading) {
  g_last_reading = reading;
  g_has_reading = true;
  render_from_cache();
}

void weather_set_status_line(const char *text) {
  lv_label_set_text(g_widgets.status_label, text);
}
