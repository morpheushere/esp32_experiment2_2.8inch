#include "ui_tabview.h"

#include <lvgl.h>

#include "tab_placeholder.h"
#include "tab_weather.h"

static constexpr lv_coord_t TAB_BAR_HEIGHT = 32;

void build_tabview() {
  lv_obj_t *tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, TAB_BAR_HEIGHT);

  lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Weather");
  lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
  lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");

  build_weather_tab(tab1);
  build_placeholder_tab(tab2, "Coming soon");
  build_placeholder_tab(tab3, "Coming soon");
}
