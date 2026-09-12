#include "tab_spotify.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "bauhaus_colors.h"
#include "spotify_client.h"

namespace {

constexpr int ART_X = (320 - SPOTIFY_ART_SIZE) / 2;
constexpr int ART_Y = 8;
constexpr int BADGE_R = 14;  // play/pause glyph backdrop, bottom-right corner of the art

lv_obj_t *g_tab = nullptr;
lv_obj_t *g_art_canvas = nullptr;
lv_color_t *g_art_buf = nullptr;
lv_obj_t *g_badge_bg = nullptr;
lv_obj_t *g_badge_icon = nullptr;
lv_obj_t *g_track_label = nullptr;
lv_obj_t *g_artist_label = nullptr;
lv_obj_t *g_placeholder_label = nullptr;
lv_obj_t *g_control_row = nullptr;
lv_obj_t *g_playpause_icon = nullptr;

// Tracks the last-known state so playpause_clicked_cb() knows which
// action to submit -- the button always submits whichever action would
// *change* the current state (mirrors every real player's play/pause
// button), which is the opposite convention from the passive status
// badge above (that one shows what's happening right now, not what
// tapping it would do -- but it isn't actually tappable, so no conflict).
bool g_is_playing = false;

// REVERTED a static-array version of this buffer back to malloc(). Tried
// a static array (no malloc at all) as a fragmentation-proof fix, since
// this canvas's *dynamic* allocation had been failing under heap
// fragmentation elsewhere on this device. Live testing showed that broke
// WiFi outright instead (STA config failed, every boot) -- confirmed by
// isolating the change: reverting just this one array fixed it. Root
// cause understood in hindsight: .bss and the heap share the same DRAM
// pool on ESP32, so a static reservation isn't "free" memory sitting
// unused next to the heap, it directly lowers the heap's total ceiling --
// an ~8KB static array was apparently enough to push whatever margin
// esp_wifi_set_config() needs at init below zero. Back to the
// known-working lazy-malloc approach (still deferred past WiFi init,
// still idempotent/retried on-demand from fetch_art()); the heap-
// fragmentation problem this was trying to fix is real but a fragile art
// thumbnail is a much smaller problem than a device that can't connect
// to WiFi at all.
void ensure_art_canvas() {
  if (g_art_canvas || !g_tab) return;

  size_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(SPOTIFY_ART_SIZE, SPOTIFY_ART_SIZE);
  g_art_buf = static_cast<lv_color_t *>(malloc(buf_size));
  Serial.printf(
      "[Mem] spotify art canvas %dx%d (%u bytes): %s -- free heap=%u, largest free block=%u\n",
      SPOTIFY_ART_SIZE, SPOTIFY_ART_SIZE, static_cast<unsigned>(buf_size),
      g_art_buf ? "OK" : "FAILED", ESP.getFreeHeap(),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  if (!g_art_buf) return;

  g_art_canvas = lv_canvas_create(g_tab);
  lv_canvas_set_buffer(g_art_canvas, g_art_buf, SPOTIFY_ART_SIZE, SPOTIFY_ART_SIZE,
                        LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(g_art_canvas, ART_X, ART_Y);
  lv_obj_set_style_border_width(g_art_canvas, 0, 0);
  lv_obj_set_style_pad_all(g_art_canvas, 0, 0);
  // Neutral placeholder fill until the first track's art arrives.
  lv_canvas_fill_bg(g_art_canvas, lv_color_make(0x2A, 0x2E, 0x3A), LV_OPA_COVER);
  lv_obj_add_flag(g_art_canvas, LV_OBJ_FLAG_HIDDEN);
  // NOTE: previously called lv_obj_move_background() here to keep the
  // canvas behind the badge/labels (created earlier, at boot). Confirmed
  // live that this was the cause of the art only ever showing a thin top
  // sliver: with the canvas sent to the back of g_tab's z-order, sibling
  // objects covered most of it. Leaving it in normal (topmost, since it's
  // created last) z-order and instead moving the badge to the front below
  // fixes this while still keeping the badge visible on top of the art.
  lv_obj_move_foreground(g_badge_bg);
}

// ---- playback control buttons --------------------------------------------
// Deferred to spotify_client_tick() via spotify_submit_control() -- never
// do blocking network I/O synchronously from a touch-event callback (see
// that function's header comment, and the reentrancy note on
// spotify_client_tick() above -- the same class of bug that once
// corrupted tabview touch tracking).

void prev_clicked_cb(lv_event_t *e) {
  LV_UNUSED(e);
  spotify_submit_control("previous");
}

void next_clicked_cb(lv_event_t *e) {
  LV_UNUSED(e);
  spotify_submit_control("next");
}

void playpause_clicked_cb(lv_event_t *e) {
  LV_UNUSED(e);
  spotify_submit_control(g_is_playing ? "pause" : "play");
}

// Small, square, icon-only, outlined in white -- deliberately neutral so
// the row doesn't compete with whatever colors are in the album art above
// it (unlike the Claude tab's buttons, which carry brand color since nothing
// else on that tab does).
lv_obj_t *make_control_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 52, 36);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, bauhaus_white(), 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, symbol);
  lv_obj_set_style_text_color(label, bauhaus_white(), 0);
  lv_obj_center(label);
  return label;  // caller only ever needs to update the play/pause icon's text
}

}  // namespace

lv_color_t *spotify_art_buffer() { return g_art_buf; }

void spotify_art_updated() {
  if (g_art_canvas) lv_obj_invalidate(g_art_canvas);
}

void spotify_apply_now_playing(const SpotifyNowPlaying &data) {
  // Track-loaded, not is_playing -- a paused track must still show its
  // art/labels/controls (most importantly the Play button) or tapping
  // Pause would hide the only way to resume, stranding the user. Before
  // the playback buttons existed, this distinction didn't matter (nothing
  // on this tab could change is_playing), so it silently conflated "paused"
  // with "nothing loaded".
  bool has_track = data.track[0] != '\0';
  g_is_playing = data.is_playing;

  if (g_placeholder_label) {
    if (has_track) {
      lv_obj_add_flag(g_placeholder_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(g_placeholder_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_art_canvas) {
    if (has_track) {
      lv_obj_clear_flag(g_art_canvas, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_art_canvas, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (g_track_label) {
    lv_label_set_text(g_track_label, has_track ? data.track : "");
  }
  if (g_artist_label) {
    lv_label_set_text(g_artist_label, has_track ? data.artist : "");
  }
  if (g_badge_bg && g_badge_icon) {
    if (has_track) {
      lv_obj_clear_flag(g_badge_bg, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(g_badge_icon, data.is_playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    } else {
      lv_obj_add_flag(g_badge_bg, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_control_row) {
    if (has_track) {
      lv_obj_clear_flag(g_control_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_control_row, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_playpause_icon) {
    // Opposite convention from the badge above: this shows the *next*
    // action (tap to pause while playing, tap to play while paused), not
    // the current state -- see the g_is_playing comment up top.
    lv_label_set_text(g_playpause_icon, data.is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  }
}

void spotify_ensure_art_canvas() { ensure_art_canvas(); }

void build_spotify_tab(lv_obj_t *tab) {
  g_tab = tab;
  lv_obj_set_style_pad_all(tab, 0, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tab, bauhaus_black(), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab, 0, 0);

  // Play/pause badge, bottom-right corner of the art.
  g_badge_bg = lv_obj_create(tab);
  lv_obj_set_size(g_badge_bg, BADGE_R * 2, BADGE_R * 2);
  lv_obj_set_pos(g_badge_bg, ART_X + SPOTIFY_ART_SIZE - BADGE_R * 2 + 4,
                 ART_Y + SPOTIFY_ART_SIZE - BADGE_R * 2 + 4);
  lv_obj_set_style_radius(g_badge_bg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_badge_bg, bauhaus_red(), 0);
  lv_obj_set_style_bg_opa(g_badge_bg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_badge_bg, 0, 0);
  lv_obj_clear_flag(g_badge_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_badge_bg, LV_OBJ_FLAG_HIDDEN);

  g_badge_icon = lv_label_create(g_badge_bg);
  lv_label_set_text(g_badge_icon, LV_SYMBOL_PLAY);
  lv_obj_set_style_text_color(g_badge_icon, bauhaus_white(), 0);
  lv_obj_center(g_badge_icon);

  // Track/artist, below the art. Fixed width + LV_LABEL_LONG_SCROLL_CIRCULAR
  // marquee-scrolls titles too long to fit instead of clipping/wrapping.
  g_track_label = lv_label_create(tab);
  lv_label_set_text(g_track_label, "");  // else LVGL's default "Text" placeholder shows
  lv_obj_set_width(g_track_label, 280);
  lv_label_set_long_mode(g_track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(g_track_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_track_label, bauhaus_white(), 0);
  lv_obj_set_style_text_font(g_track_label, &lv_font_montserrat_16, 0);
  lv_obj_align(g_track_label, LV_ALIGN_TOP_MID, 0, ART_Y + SPOTIFY_ART_SIZE + 10);

  g_artist_label = lv_label_create(tab);
  lv_label_set_text(g_artist_label, "");
  lv_obj_set_width(g_artist_label, 280);
  lv_label_set_long_mode(g_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(g_artist_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_artist_label, lv_color_make(200, 200, 200), 0);
  lv_obj_align(g_artist_label, LV_ALIGN_TOP_MID, 0, ART_Y + SPOTIFY_ART_SIZE + 34);

  // Previous / Play-Pause / Next, below the artwork and track/artist labels.
  // Hidden together with the art/labels above whenever nothing is loaded
  // (see the has_track comment in spotify_apply_now_playing()).
  g_control_row = lv_obj_create(tab);
  lv_obj_set_size(g_control_row, 190, 36);
  lv_obj_align(g_control_row, LV_ALIGN_TOP_MID, 0, ART_Y + SPOTIFY_ART_SIZE + 60);
  lv_obj_set_style_bg_opa(g_control_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_control_row, 0, 0);
  lv_obj_set_style_pad_all(g_control_row, 0, 0);
  lv_obj_set_style_pad_column(g_control_row, 10, 0);
  lv_obj_set_flex_flow(g_control_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_control_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(g_control_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_control_row, LV_OBJ_FLAG_HIDDEN);

  make_control_button(g_control_row, LV_SYMBOL_PREV, prev_clicked_cb);
  g_playpause_icon = make_control_button(g_control_row, LV_SYMBOL_PLAY, playpause_clicked_cb);
  make_control_button(g_control_row, LV_SYMBOL_NEXT, next_clicked_cb);

  // "Nothing playing" placeholder -- shown whenever is_playing is false or
  // the cache is empty, so the tab never just goes blank.
  g_placeholder_label = lv_label_create(tab);
  lv_label_set_text(g_placeholder_label, LV_SYMBOL_AUDIO "  Nothing playing");
  lv_obj_set_style_text_color(g_placeholder_label, lv_color_make(160, 160, 160), 0);
  lv_obj_center(g_placeholder_label);

  // TRIED allocating the art canvas's 8KB buffer right here, eagerly at
  // boot, to grab a contiguous block before WiFi/HTTP activity had a
  // chance to fragment the heap. Confirmed live this broke WiFi outright:
  // right after this malloc succeeded (heap still looked fine --
  // free=104828, largest free block=30708), WiFi's own connection
  // sequence failed with "[E][NetworkEvents.cpp] postEvent(): Arduino
  // Event Malloc Failed!" and never recovered -- no [WiFi] connected
  // message, ever. Same root cause as the earlier static-array regression
  // (see the malloc() comment above ensure_art_canvas()): WiFi/the network
  // event system needs its own margin in a specific memory region at
  // connect time, and total-free-heap or largest-free-block numbers don't
  // capture that constraint. Reverted -- back to the lazy, deferred
  // allocation (on tab activation / first art fetch), safely after WiFi
  // is already up. The album art fragmentation problem this was trying to
  // fix is real but a fragile art thumbnail is a much smaller problem than
  // a device that can't connect to WiFi at all.
}
