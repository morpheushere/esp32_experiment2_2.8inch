#pragma once

// Call once from setup(), after build_tabview() has created the Claude
// tab widgets. WiFi bring-up itself is owned by weather_client.cpp (one
// connect/retry state machine for the whole device) -- this just starts
// polling once that's up.
void claude_approval_client_init();

// Call every loop() iteration. Non-blocking with respect to WiFi state.
void claude_approval_client_tick();

// Pauses/resumes polling -- call with false when tab 4 isn't the active
// tab, true when it becomes active again.
void claude_approval_set_polling_running(bool running);

// Called from tab_claude.cpp's Accept/Deny button handlers. Just records
// the decision to send -- the actual HTTP POST happens later from
// claude_approval_client_tick() (loop()'s call, outside any touch-event
// dispatch). A blocking HTTP call made directly inside an LVGL touch
// callback risks the same tabview touch/state corruption already found
// and fixed once in this codebase (see spotify_client.cpp's
// g_canvas_pending and the comment on spotify_set_polling_running()).
void claude_approval_submit_decision(const char *request_id, const char *decision);
