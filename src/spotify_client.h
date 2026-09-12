#pragma once

// Call once from setup(), after build_tabview() has created the Spotify
// tab widgets. WiFi bring-up itself is owned by weather_client.cpp (one
// connect/retry state machine for the whole device) -- this just starts
// polling once that's up.
void spotify_client_init();

// Call every loop() iteration. Non-blocking with respect to WiFi state
// (checks WiFi.status(), does nothing until connected); the HTTP calls
// themselves block briefly like weather_client's do, which is an existing,
// accepted tradeoff in this codebase for small LAN payloads.
void spotify_client_tick();

// Pauses/resumes polling -- call with false when tab 2 isn't the active
// tab (saves network/backend load while nobody's looking at it), true
// when it becomes active again.
void spotify_set_polling_running(bool running);

// Records a playback command ("play"/"pause"/"next"/"previous") tapped
// from a button's touch-event callback in tab_spotify.cpp. Deferred to
// the next spotify_client_tick() rather than sent immediately, same
// reasoning as claude_approval_submit_decision(): never do blocking
// network I/O synchronously inside a touch-event callback.
void spotify_submit_control(const char *action);
