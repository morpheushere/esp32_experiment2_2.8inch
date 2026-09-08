#pragma once

#include <stdint.h>

// Sets up PWM on the CYD's discrete onboard RGB LED (R=GPIO4, G=GPIO16,
// B=GPIO17). Call once from setup().
void rgb_led_init();

// Advances the temperature-driven pulse and writes it to the LED. r/g/b
// select the hue (pass the same color the background gradient is using);
// dt_seconds/temp_c drive the breathing rate, ported from
// ESP32_experiment1's animation.cpp updateLED().
void rgb_led_update(uint8_t r, uint8_t g, uint8_t b, float dt_seconds, float temp_c);
