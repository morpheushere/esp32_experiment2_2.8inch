#pragma once

#include <lvgl.h>

// Must match the backend's ART_SIZE constant (strava-heatmap-pwa's
// api/spotify_client.py) exactly -- the raw RGB565 pixel dump it serves is
// memcpy'd straight into this canvas's buffer with no resizing/decoding on
// this end, so a mismatch here would just scramble the image.
//
// Sized down from an initial 120 after measuring live on this board: once
// WiFi is connected (tab 2's canvas is allocated lazily, after WiFi init --
// see spotify_ensure_art_canvas()), free heap drops to ~60KB but the
// *largest contiguous block* is only ~19-20KB (fragmentation, not real
// exhaustion) -- a 120x120 canvas (28800 bytes) fails to allocate outright.
// 80x80 (12800 bytes) fits with real margin.
constexpr int SPOTIFY_ART_SIZE = 80;

struct SpotifyNowPlaying {
  bool is_playing = false;
  char track[64] = "";
  char artist[64] = "";
  char album[64] = "";
  char art_id[40] = "";  // Spotify album id; empty when nothing is playing
  long progress_ms = -1;
  long duration_ms = -1;
};

// Creates tab 2's layout: track/artist labels (long titles marquee-scroll)
// and a play/pause glyph. Call once from ui_tabview's setup. Deliberately
// does NOT allocate the album-art canvas -- see spotify_ensure_art_canvas().
void build_spotify_tab(lv_obj_t *tab);

// Lazily allocates the album-art canvas on first call (idempotent after
// that). Call this when tab 2 first becomes active, not at boot -- see the
// comment on ensure_art_canvas() in tab_spotify.cpp for why: allocating it
// alongside tab 1's canvases at startup left too little heap for WiFi's
// own one-time driver init, which crashed the board on this hardware.
void spotify_ensure_art_canvas();

// Updates the track/artist/play-pause labels. Does not touch the art
// canvas -- that's updated separately via spotify_art_buffer() +
// spotify_art_updated(), only when art_id actually changes.
void spotify_apply_now_playing(const SpotifyNowPlaying &data);

// Raw pixel buffer for the album-art canvas (SPOTIFY_ART_SIZE x
// SPOTIFY_ART_SIZE, lv_color_t/RGB565) -- spotify_client.cpp streams the
// backend's raw HTTP response straight into this buffer (no decoding),
// then calls spotify_art_updated() to redraw. Returns nullptr if the
// canvas failed to allocate.
lv_color_t *spotify_art_buffer();
void spotify_art_updated();
