#include "tab_calendar.h"

#include "bauhaus_colors.h"

namespace {

struct RowWidgets {
  lv_obj_t *row = nullptr;
  lv_obj_t *time_label = nullptr;
  lv_obj_t *title_label = nullptr;
};

RowWidgets g_rows[CALENDAR_MAX_EVENTS];
lv_obj_t *g_placeholder = nullptr;

}  // namespace

void build_calendar_tab(lv_obj_t *tab) {
  lv_obj_set_style_pad_all(tab, 8, 0);
  lv_obj_set_style_pad_row(tab, 4, 0);
  lv_obj_set_style_bg_color(tab, bauhaus_black(), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab, 0, 0);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  // Deliberately leave the tab scrollable (LVGL objects default to this) --
  // unlike tabs 1/2, the number of events varies day to day, so this one
  // needs native touch-drag vertical scrolling instead of a fixed layout.

  for (int i = 0; i < CALENDAR_MAX_EVENTS; i++) {
    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_make(50, 50, 60), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *time_label = lv_label_create(row);
    lv_obj_set_width(time_label, 88);
    lv_obj_set_style_text_color(time_label, bauhaus_yellow(), 0);
    lv_label_set_text(time_label, "");

    lv_obj_t *title_label = lv_label_create(row);
    lv_obj_set_flex_grow(title_label, 1);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title_label, bauhaus_white(), 0);
    lv_label_set_text(title_label, "");

    g_rows[i] = {row, time_label, title_label};
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
  }

  g_placeholder = lv_label_create(tab);
  lv_label_set_text(g_placeholder, "No events today");
  lv_obj_set_style_text_color(g_placeholder, lv_color_make(160, 160, 160), 0);
}

void calendar_apply_agenda(const CalendarAgenda &agenda) {
  int n = agenda.count;
  if (n > CALENDAR_MAX_EVENTS) n = CALENDAR_MAX_EVENTS;

  for (int i = 0; i < CALENDAR_MAX_EVENTS; i++) {
    if (i < n) {
      lv_label_set_text(g_rows[i].time_label, agenda.events[i].time_label);
      lv_label_set_text(g_rows[i].title_label, agenda.events[i].title);
      lv_obj_clear_flag(g_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (g_placeholder) {
    if (n == 0) {
      lv_obj_clear_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
  }
}
