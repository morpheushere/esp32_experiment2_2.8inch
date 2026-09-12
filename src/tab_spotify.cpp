#include "tab_spotify.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "bauhaus_colors.h"

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

}  // namespace

lv_color_t *spotify_art_buffer() { return g_art_buf; }

void spotify_art_updated() {
  if (g_art_canvas) lv_obj_invalidate(g_art_canvas);
}

void spotify_apply_now_playing(const SpotifyNowPlaying &data) {
  bool has_track = data.is_playing && data.track[0] != '\0';

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

  // "Nothing playing" placeholder -- shown whenever is_playing is false or
  // the cache is empty, so the tab never just goes blank.
  g_placeholder_label = lv_label_create(tab);
  lv_label_set_text(g_placeholder_label, LV_SYMBOL_AUDIO "  Nothing playing");
  lv_obj_set_style_text_color(g_placeholder_label, lv_color_make(160, 160, 160), 0);
  lv_obj_center(g_placeholder_label);
}
