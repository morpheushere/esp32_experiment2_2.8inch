#include "rgb_led.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int PIN_R = 4;
constexpr int PIN_G = 16;
constexpr int PIN_B = 17;
constexpr int PWM_FREQ_HZ = 5000;
constexpr int PWM_RES_BITS = 8;  // duty range 0-255

// Common CYD wiring drives the LED active-low. Flip this if it looks
// inverted (dim when it should be bright) once verified on the real board.
constexpr bool ACTIVE_LOW = true;

float g_led_phase = 0.0f;

void write_channel(int pin, uint8_t value) {
  ledcWrite(pin, ACTIVE_LOW ? (255 - value) : value);
}

}  // namespace

void rgb_led_init() {
  ledcAttach(PIN_R, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_G, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_B, PWM_FREQ_HZ, PWM_RES_BITS);
  write_channel(PIN_R, 0);
  write_channel(PIN_G, 0);
  write_channel(PIN_B, 0);
}

void rgb_led_update(uint8_t r, uint8_t g, uint8_t b, float dt_seconds, float temp_c) {
  // Pulse rate scales with temperature: slow calm breathing when cold,
  // quickening toward a faster pulse as it gets hotter. Range matches
  // TEMP_STOPS' -10C..34C span.
  float t_norm = (temp_c - (-10.0f)) / (34.0f - (-10.0f));
  if (isnan(t_norm)) t_norm = 0.5f;
  if (t_norm < 0.0f) t_norm = 0.0f;
  if (t_norm > 1.0f) t_norm = 1.0f;
  float pulse_hz = 0.22f + (1.0f - 0.22f) * t_norm;

  g_led_phase += dt_seconds * pulse_hz * 2.0f * static_cast<float>(M_PI);
  float brightness = 0.35f + 0.65f * (0.5f + 0.5f * sinf(g_led_phase));

  write_channel(PIN_R, static_cast<uint8_t>(r * brightness));
  write_channel(PIN_G, static_cast<uint8_t>(g * brightness));
  write_channel(PIN_B, static_cast<uint8_t>(b * brightness));
}
