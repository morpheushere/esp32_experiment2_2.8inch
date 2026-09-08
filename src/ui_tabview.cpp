#include "ui_tabview.h"

#include <lvgl.h>

#include "bauhaus_colors.h"
#include "tab_placeholder.h"
#include "tab_weather.h"

namespace {

constexpr lv_coord_t TAB_BAR_HEIGHT = 32;

void tabview_changed_cb(lv_event_t *e) {
  lv_obj_t *tabview = lv_event_get_target(e);
  uint16_t active = lv_tabview_get_tab_act(tabview);
  // Tab 1's animation only needs to run while it's on screen -- pausing it
  // otherwise saves CPU/heat without touching its one-time canvas
  // allocation.
  weather_set_scene_running(active == 0);
}

}  // namespace

void build_tabview() {
  lv_obj_t *tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, TAB_BAR_HEIGHT);

  // Dark, minimal tab bar so it doesn't clash with the full-bleed Bauhaus
  // scene beneath it -- red accent on the active tab matches that palette.
  lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, bauhaus_black(), 0);
  lv_obj_set_style_text_color(tab_btns, bauhaus_white(), 0);
  lv_obj_set_style_bg_color(tab_btns, bauhaus_red(),
                             static_cast<lv_style_selector_t>(LV_PART_ITEMS | LV_STATE_CHECKED));
  lv_obj_set_style_border_width(tab_btns, 0, 0);

  lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Weather");
  lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
  lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");

  build_weather_tab(tab1);
  build_placeholder_tab(tab2, "Coming soon");
  build_placeholder_tab(tab3, "Coming soon");

  lv_obj_add_event_cb(tabview, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}
