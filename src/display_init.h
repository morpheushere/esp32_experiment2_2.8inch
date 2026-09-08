#pragma once

// Initializes the LovyanGFX panel, LVGL core, draw buffers, and the
// display/touch driver callbacks. Call once from setup() before building
// any LVGL UI.
void display_init();
