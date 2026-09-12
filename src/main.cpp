#include <Arduino.h>
#include <lvgl.h>

#include "calendar_client.h"
#include "display_init.h"
#include "spotify_client.h"
#include "ui_tabview.h"
#include "weather_client.h"

static uint32_t g_last_tick_ms = 0;

void setup() {
  Serial.begin(115200);

  display_init();
  build_tabview();
  weather_client_init();
  spotify_client_init();
  calendar_client_init();

  g_last_tick_ms = millis();
}

void loop() {
  uint32_t now = millis();
  lv_tick_inc(now - g_last_tick_ms);
  g_last_tick_ms = now;

  lv_timer_handler();
  weather_client_tick();
  spotify_client_tick();
  calendar_client_tick();

  delay(5);
}
