#include "temp_gradient.h"

#include <math.h>

struct TempStop {
  float temp_c;
  uint8_t r, g, b;
};

static const TempStop TEMP_STOPS[] = {
    {-10, 0x1B, 0x1F, 0x3B},  // deep indigo
    {0, 0x2E, 0x6F, 0x95},    // cold blue
    {12, 0x2E, 0x86, 0xAB},   // cool blue
    {20, 0x4E, 0xCD, 0xC4},   // teal
    {26, 0xFF, 0x9F, 0x1C},   // amber
    {34, 0xFF, 0x5A, 0x36},   // hot red
};
static constexpr size_t NUM_STOPS = sizeof(TEMP_STOPS) / sizeof(TEMP_STOPS[0]);

static uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
  return static_cast<uint8_t>(a + (b - a) * t);
}

lv_color_t color_for_temp_c(float temp_c) {
  if (isnan(temp_c)) {
    return lv_color_hex(0x2A2E3A);  // neutral gray when no reading yet
  }

  if (temp_c <= TEMP_STOPS[0].temp_c) {
    const auto &s = TEMP_STOPS[0];
    return lv_color_make(s.r, s.g, s.b);
  }
  if (temp_c >= TEMP_STOPS[NUM_STOPS - 1].temp_c) {
    const auto &s = TEMP_STOPS[NUM_STOPS - 1];
    return lv_color_make(s.r, s.g, s.b);
  }

  for (size_t i = 0; i + 1 < NUM_STOPS; i++) {
    const auto &lo = TEMP_STOPS[i];
    const auto &hi = TEMP_STOPS[i + 1];
    if (temp_c >= lo.temp_c && temp_c <= hi.temp_c) {
      float t = (temp_c - lo.temp_c) / (hi.temp_c - lo.temp_c);
      return lv_color_make(lerp_u8(lo.r, hi.r, t), lerp_u8(lo.g, hi.g, t),
                            lerp_u8(lo.b, hi.b, t));
    }
  }

  // Unreachable given the bounds checks above.
  return lv_color_hex(0x2A2E3A);
}
