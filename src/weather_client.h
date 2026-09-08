#pragma once

// Call once from setup(), after build_tabview() has created the weather
// tab widgets.
void weather_client_init();

// Call every loop() iteration. Non-blocking: advances the WiFi
// connect/retry state machine and performs the periodic HTTP poll
// internally via millis() — never calls delay().
void weather_client_tick();
