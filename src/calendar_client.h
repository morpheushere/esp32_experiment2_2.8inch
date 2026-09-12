#pragma once

// Call once from setup(), after build_tabview() has created the calendar
// tab widgets. WiFi bring-up itself is owned by weather_client.cpp (one
// connect/retry state machine for the whole device) -- this just starts
// polling once that's up.
void calendar_client_init();

// Call every loop() iteration. Non-blocking with respect to WiFi state
// (checks WiFi.status(), does nothing until connected).
void calendar_client_tick();

// Pauses/resumes polling -- call with false when tab 3 isn't the active
// tab (saves network/backend load while nobody's looking at it), true
// when it becomes active again.
void calendar_set_polling_running(bool running);
