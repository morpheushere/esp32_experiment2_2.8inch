#pragma once

#include <lvgl.h>

// Ported 1:1 from strava-heatmap-pwa/pwa/src/weather-scene.js and
// ESP32_experiment1/include/colors.h so every ambient display in this
// family (PWA, C6 board, this CYD) reads as the same palette.
static inline lv_color_t bauhaus_red() { return lv_color_make(0xE4, 0x03, 0x2E); }
static inline lv_color_t bauhaus_yellow() { return lv_color_make(0xFF, 0xC9, 0x00); }
static inline lv_color_t bauhaus_blue() { return lv_color_make(0x00, 0x5E, 0xB8); }
static inline lv_color_t bauhaus_white() { return lv_color_make(0xFF, 0xFF, 0xFF); }
static inline lv_color_t bauhaus_black() { return lv_color_make(0x00, 0x00, 0x00); }
