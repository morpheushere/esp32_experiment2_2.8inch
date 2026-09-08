#pragma once

#include <lvgl.h>

// Ported from the web dashboard's TEMP_STOPS gradient (indigo/cold ->
// red/hot). Always evaluated on Celsius, independent of the display's F/C
// toggle, to match the source app's semantics.
lv_color_t color_for_temp_c(float temp_c);
