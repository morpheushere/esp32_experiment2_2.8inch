#pragma once

#include <stdint.h>

#include <lvgl.h>

// Ported from the web dashboard's TEMP_STOPS gradient (indigo/cold ->
// red/hot). Always evaluated on Celsius, independent of the display's F/C
// toggle, to match the source app's semantics.
lv_color_t color_for_temp_c(float temp_c);

// Same gradient, raw components -- needed for the glow/particle lightening
// math in the animated scene, which operates on r/g/b directly.
void temp_to_rgb(float temp_c, uint8_t &r, uint8_t &g, uint8_t &b);

// Lightens a single channel toward white by `amount` (0..1), matching the
// PWA/sibling scene's lighten() helper.
uint8_t lighten_channel(uint8_t c, float amount);
