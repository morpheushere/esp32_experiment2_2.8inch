#pragma once

#include <lvgl.h>

// Builds a single centered label on `tab`. Used for tabs 2 and 3 until
// they're built out in a later session.
void build_placeholder_tab(lv_obj_t *tab, const char *text);
