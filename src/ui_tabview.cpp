#include "ui_tabview.h"

#include <lvgl.h>

#include "bauhaus_colors.h"
#include "calendar_client.h"
#include "claude_approval_client.h"
#include "spotify_client.h"
#include "tab_calendar.h"
#include "tab_claude.h"
#include "tab_spotify.h"
#include "tab_weather.h"

namespace {

constexpr lv_coord_t TAB_BAR_HEIGHT = 32;

// Bound to *two* different objects below (the tabview itself, and its tab
// button matrix) -- LVGL's own lv_tabview.c only sends LV_EVENT_VALUE_CHANGED
// on the tabview object for a *swipe*-driven tab change (cont_scroll_end_
// event_cb). A *tap* on a tab button goes through a completely different
// internal path (btns_value_changed_event_cb -> lv_tabview_set_act(...,
// LV_ANIM_OFF)) that never fires that event on the tabview at all -- so a
// listener bound only to the tabview silently never runs on tap, which is
// how this went unnoticed until tested by hand. Reading `tabview` back from
// user_data (not lv_event_get_target(e)) is what lets the same callback work
// correctly no matter which of the two objects it's actually firing on.
void tabview_changed_cb(lv_event_t *e) {
  lv_obj_t *tabview = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  uint16_t active = lv_tabview_get_tab_act(tabview);
  // Each tab's background work only needs to run while it's on screen --
  // pausing it otherwise saves CPU/network/backend load without touching
  // either tab's one-time canvas allocation.
  weather_set_scene_running(active == 0);
  spotify_set_polling_running(active == 1);
  calendar_set_polling_running(active == 2);
  claude_approval_set_polling_running(active == 3);
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
  lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Spotify");
  lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Calendar");
  lv_obj_t *tab4 = lv_tabview_add_tab(tabview, "Claude");

  build_weather_tab(tab1);
  build_spotify_tab(tab2);
  build_calendar_tab(tab3);
  build_claude_tab(tab4);

  lv_obj_add_event_cb(tabview, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, tabview);   // swipe
  lv_obj_add_event_cb(tab_btns, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, tabview);  // tap

  // lv_tabview_add_tab() doesn't fire an initial VALUE_CHANGED event for the
  // tab (index 0) it starts on, so without this, spotify_set_polling_running
  // would keep its own default (true) and poll from boot even while tab 1 is
  // showing. Sync explicitly to the actual initial active tab instead of
  // relying on a callback that hasn't fired yet.
  weather_set_scene_running(true);
  spotify_set_polling_running(false);
  calendar_set_polling_running(false);
  claude_approval_set_polling_running(false);
}
