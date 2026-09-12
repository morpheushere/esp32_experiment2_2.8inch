#include "tab_weather.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bauhaus_colors.h"
#include "rgb_led.h"
#include "temp_gradient.h"

// Ported from morpheushere/ESP32_experiment1 (src/scene.cpp, src/animation.cpp,
// src/weather_client.cpp) -- a sibling firmware for a different board that
// renders this same NAS weather feed as a full-bleed animated "Bauhaus
// poster." Re-implemented here on LVGL canvases (that project draws
// directly to one full-screen Arduino_GFX offscreen buffer, since it has no
// tabview to coexist with) so tab 1 matches its look while tabs 2/3 keep
// working.
//
// The scene is split across 4 small canvases rather than one big one: a
// single 320x208x16bpp buffer (~130KB) failed to allocate on this board --
// heap_caps_get_largest_free_block reported ~110KB as the largest
// contiguous block despite ~280KB total free (classic ESP32 heap
// fragmentation, not a real shortage). Splitting into a 320x100 "hero"
// canvas (pill/numeral/unit label/animated background) plus three small
// 320x24 stat-row canvases keeps every individual malloc comfortably under
// that limit.

namespace {

// ---- layout (320-wide landscape tab content area -- 240 screen height
// minus the 32px tab bar from ui_tabview.cpp gives 208px total) -----------
constexpr int SCENE_W = 320;
constexpr int HERO_H = 100;
constexpr int ROW_H = 24;
constexpr int ROW_GAP = 0;  // any gap here exposes the tab's own bg as a visible seam
constexpr int ROW_COUNT = 3;
// Full animated area across all 4 canvases -- particles roam this whole
// height (not just the hero canvas) so the scene feels alive throughout,
// not only where the numeral is.
constexpr int SCENE_H = HERO_H + ROW_COUNT * ROW_H;

constexpr int NUMERAL_Y = 10;
constexpr int UNIT_LABEL_Y = 76;
constexpr int STAT_ROW_X = 12;
constexpr int SPARK_W = 64;
constexpr int SPARK_H = 16;

constexpr uint32_t FRAME_INTERVAL_MS = 100;  // ~10fps, matches the ported easing constants
constexpr float SMOOTH_K = 0.06f;            // per-tick smoothing toward target, tuned for ~10fps

// Scaled up from the sibling's 22/50 base to keep a similar particle
// density now that particles roam the full SCENE_H (172px) instead of just
// the 100px hero canvas.
constexpr int BASE_PARTICLES = 38;
constexpr int MAX_PARTICLES = 90;

enum DotShape { DOT_CIRCLE, DOT_SQUARE, DOT_TRIANGLE };

struct Particle {
  float x, y, size, speed, twinkle;
};

// ---- widgets & state ------------------------------------------------
lv_obj_t *g_tab = nullptr;
lv_obj_t *g_hero_canvas = nullptr;
lv_color_t *g_hero_buf = nullptr;
lv_obj_t *g_row_canvas[ROW_COUNT] = {nullptr, nullptr, nullptr};
lv_color_t *g_row_buf[ROW_COUNT] = {nullptr, nullptr, nullptr};
lv_obj_t *g_status_label = nullptr;
lv_timer_t *g_timer = nullptr;

float g_current_temp_c = 15.0f, g_target_temp_c = 15.0f;
float g_current_wind = 0.0f, g_target_wind = 0.0f;
char g_pressure_direction[8] = "";

float g_t = 0.0f;
float g_glow_phase = 0.0f;
Particle g_particles[MAX_PARTICLES];
uint32_t g_last_frame_ms = 0;

WeatherReading g_last_reading;
bool g_has_reading = false;
bool g_use_fahrenheit = true;  // US station (mph/hPa) -- default per user decision

float randf() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }

void spawn_particle(Particle &p) {
  p.x = randf() * SCENE_W;
  p.y = randf() * SCENE_H;
  p.size = 1.0f + randf() * 1.6f;
  p.speed = 0.4f + randf() * 0.8f;
  p.twinkle = randf() * 6.28f;
}

// ---- tracked-text helpers -------------------------------------------
// LVGL's letter_space draw property does natively what the source project
// hand-rolls per-glyph for Arduino_GFX (which has no built-in tracking).

int text_width_tracked(const char *s, const lv_font_t *font, int letter_space) {
  return lv_txt_get_width(s, strlen(s), font, letter_space, LV_TEXT_FLAG_NONE);
}

void draw_tracked(lv_obj_t *canvas, int x, int y, const char *s, const lv_font_t *font,
                   lv_color_t color, int letter_space) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = font;
  dsc.color = color;
  dsc.letter_space = letter_space;
  int w = text_width_tracked(s, font, letter_space) + 4;
  lv_canvas_draw_text(canvas, x, y, w, &dsc, s);
}

void draw_centered(lv_obj_t *canvas, int canvas_w, int y, const char *s, const lv_font_t *font,
                    lv_color_t color, int letter_space) {
  int w = text_width_tracked(s, font, letter_space);
  int x = (canvas_w - w) / 2;
  if (x < 0) x = 0;
  draw_tracked(canvas, x, y, s, font, color, letter_space);
}

// ---- shape helpers ----------------------------------------------------

void fill_circle(lv_obj_t *canvas, int cx, int cy, int r, lv_color_t color,
                  lv_opa_t opa = LV_OPA_COVER) {
  if (r < 1) r = 1;
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.color = color;
  dsc.width = r;  // width == radius draws a filled disk, not a ring
  dsc.opa = opa;
  lv_canvas_draw_arc(canvas, cx, cy, r, 0, 360, &dsc);
}

void fill_rect(lv_obj_t *canvas, int x, int y, int w, int h, lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_canvas_draw_rect(canvas, x, y, w, h, &dsc);
}

void fill_triangle(lv_obj_t *canvas, int cx, int cy, lv_color_t color) {
  lv_point_t pts[3] = {{static_cast<lv_coord_t>(cx), static_cast<lv_coord_t>(cy - 7)},
                        {static_cast<lv_coord_t>(cx - 7), static_cast<lv_coord_t>(cy + 5)},
                        {static_cast<lv_coord_t>(cx + 7), static_cast<lv_coord_t>(cy + 5)}};
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_canvas_draw_polygon(canvas, pts, 3, &dsc);
}

void draw_dot(lv_obj_t *canvas, int cx, int cy, DotShape shape, lv_color_t color) {
  switch (shape) {
    case DOT_CIRCLE:
      fill_circle(canvas, cx, cy, 6, color);
      break;
    case DOT_SQUARE:
      fill_rect(canvas, cx - 5, cy - 5, 10, 10, color);
      break;
    case DOT_TRIANGLE:
      fill_triangle(canvas, cx, cy, color);
      break;
  }
}

// ---- sparkline --------------------------------------------------------

void draw_sparkline(lv_obj_t *canvas, int x, int y, int w, int h, const WeatherTrend &trend) {
  if (trend.count < 2) {
    // No trend data yet -- a faint flat dashed line as a placeholder.
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_make(120, 120, 120);
    dsc.width = 1;
    for (int dx = 0; dx < w; dx += 4) {
      lv_point_t pts[2] = {{static_cast<lv_coord_t>(x + dx), static_cast<lv_coord_t>(y + h / 2)},
                            {static_cast<lv_coord_t>(x + dx + 1), static_cast<lv_coord_t>(y + h / 2)}};
      lv_canvas_draw_line(canvas, pts, 2, &dsc);
    }
    return;
  }

  float min_v = trend.series[0], max_v = trend.series[0];
  for (int i = 1; i < trend.count; i++) {
    if (trend.series[i] < min_v) min_v = trend.series[i];
    if (trend.series[i] > max_v) max_v = trend.series[i];
  }
  float range = max_v - min_v;
  if (range < 0.0001f) range = 1.0f;

  lv_color_t color = bauhaus_yellow();  // steady, or unknown direction
  if (trend.direction && strcmp(trend.direction, "rising") == 0) {
    color = bauhaus_red();
  } else if (trend.direction && strcmp(trend.direction, "falling") == 0) {
    color = bauhaus_blue();
  }

  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = color;
  dsc.width = 2;
  dsc.round_start = 1;
  dsc.round_end = 1;

  const int pad = 3;
  float step_x = static_cast<float>(w - pad * 2) / (trend.count - 1);
  int prev_x = 0, prev_y = 0;
  for (int i = 0; i < trend.count; i++) {
    int px = x + pad + static_cast<int>(roundf(i * step_x));
    int py = y + h - pad -
             static_cast<int>(roundf(((trend.series[i] - min_v) / range) * (h - pad * 2)));
    if (i > 0) {
      lv_point_t pts[2] = {{static_cast<lv_coord_t>(prev_x), static_cast<lv_coord_t>(prev_y)},
                            {static_cast<lv_coord_t>(px), static_cast<lv_coord_t>(py)}};
      lv_canvas_draw_line(canvas, pts, 2, &dsc);
    }
    prev_x = px;
    prev_y = py;
  }
  fill_circle(canvas, prev_x, prev_y, 2, color);
}

// ---- one stat row, drawn into its own small canvas -----------------------
// Stat rows don't animate -- only redrawn when a new WeatherReading lands,
// not on every 10fps tick, unlike the hero canvas below.

void draw_stat_row(lv_obj_t *canvas, DotShape shape, lv_color_t dot_color, bool has_value,
                    float value, const char *unit, const char *label, const WeatherTrend &trend) {
  const int y = 2;
  int x = STAT_ROW_X;
  int row_mid_y = y + 8;

  draw_dot(canvas, x + 6, row_mid_y, shape, dot_color);
  x += 20;

  char value_str[16];
  if (has_value) {
    snprintf(value_str, sizeof(value_str), "%.1f%s", value, unit);
  } else {
    snprintf(value_str, sizeof(value_str), "--%s", unit);
  }
  draw_tracked(canvas, x, y, value_str, &lv_font_montserrat_16, bauhaus_white(), 0);
  x += text_width_tracked(value_str, &lv_font_montserrat_16, 0) + 10;

  draw_sparkline(canvas, x, y - 2, SPARK_W, SPARK_H, trend);
  x += SPARK_W + 10;

  draw_tracked(canvas, x, y + 4, label, &lv_font_montserrat_14, lv_color_make(230, 230, 230), 1);
}

// Fills each row canvas with the *same* live background color as the hero
// canvas (not a fixed black) -- this is what makes the stat rows read as a
// continuation of one continuous scene instead of a separate strip. Split
// out from drawing the rows' content so the glow (drawn in between, via
// draw_disk_into_scene) lands behind the dot/value/sparkline/label text
// rather than on top of it or being erased by this fill.
void fill_row_backgrounds(lv_color_t bg_color) {
  for (lv_obj_t *row : g_row_canvas) {
    if (row) lv_canvas_fill_bg(row, bg_color, LV_OPA_COVER);
  }
}

void draw_row_contents(const WeatherReading &data) {
  if (g_row_canvas[0]) {
    draw_stat_row(g_row_canvas[0], DOT_CIRCLE, bauhaus_red(), !isnan(data.humidity_pct),
                  data.humidity_pct, "%", "RH", data.humidity_trend);
    lv_obj_invalidate(g_row_canvas[0]);
  }
  if (g_row_canvas[1]) {
    draw_stat_row(g_row_canvas[1], DOT_SQUARE, bauhaus_blue(), !isnan(data.pressure_hpa),
                  data.pressure_hpa, "", "HPA", data.pressure_trend);
    lv_obj_invalidate(g_row_canvas[1]);
  }
  if (g_row_canvas[2]) {
    draw_stat_row(g_row_canvas[2], DOT_TRIANGLE, bauhaus_yellow(), !isnan(data.wind_speed),
                  data.wind_speed, "", "MPH", data.wind_trend);
    lv_obj_invalidate(g_row_canvas[2]);
  }
}

// ---- hero overlay: condition pill, numeral, unit label -------------------
// Draws on top of whatever background animation_tick() already put in the
// hero canvas this frame -- never touches the background itself.

void draw_hero_overlay(const WeatherReading &data) {
  const char *label = condition_label(data);
  int text_w = text_width_tracked(label, &lv_font_montserrat_14, 2);
  int pad_x = 6, pad_y = 4;
  int w = text_w + pad_x * 2;
  int h = 14 + pad_y * 2;
  int x = SCENE_W - w - 8;
  int y = 6;
  fill_rect(g_hero_canvas, x, y, w, h, bauhaus_red());
  draw_tracked(g_hero_canvas, x + pad_x, y + pad_y, label, &lv_font_montserrat_14, bauhaus_white(),
               2);

  char temp_str[8] = "--";
  if (!isnan(data.temp_c)) {
    float shown = g_use_fahrenheit ? (data.temp_c * 9.0f / 5.0f + 32.0f) : data.temp_c;
    snprintf(temp_str, sizeof(temp_str), "%.1f", shown);
  }
  int numeral_w = text_width_tracked(temp_str, &lv_font_montserrat_48, 0);
  int numeral_x = (SCENE_W - numeral_w) / 2;
  if (numeral_x < 4) numeral_x = 4;
  draw_tracked(g_hero_canvas, numeral_x, NUMERAL_Y, temp_str, &lv_font_montserrat_48,
               bauhaus_white(), 0);

  draw_centered(g_hero_canvas, SCENE_W, UNIT_LABEL_Y, g_use_fahrenheit ? "FAHRENHEIT" : "CELSIUS",
                &lv_font_montserrat_14, bauhaus_white(), 3);
}

// Routes a particle drawn at a *global* scene coordinate (0..SCENE_H, i.e.
// hero canvas followed by the 3 stacked row canvases) to whichever of the
// 4 separate canvases actually covers that y, translating to that canvas's
// local coordinates. This is what lets particles roam the full tab height
// instead of stopping at the hero canvas's bottom edge.
void draw_particle_into_scene(float gx, float gy, int r_px, lv_color_t color) {
  int y = static_cast<int>(gy);
  if (y < 0 || y >= SCENE_H) return;  // between-frame overshoot before wrap; skip this frame

  if (y < HERO_H) {
    fill_circle(g_hero_canvas, static_cast<int>(gx), y, r_px, color);
    return;
  }
  int row = (y - HERO_H) / ROW_H;
  if (row >= ROW_COUNT) row = ROW_COUNT - 1;
  if (!g_row_canvas[row]) return;
  int local_y = y - HERO_H - row * ROW_H;
  fill_circle(g_row_canvas[row], static_cast<int>(gx), local_y, r_px, color);
}

// Like draw_particle_into_scene, but for disks large enough (the glow) to
// span more than one canvas at once -- draws the same circle (same global
// center/radius) into *every* canvas whose vertical range overlaps its
// bounding box, each at its own translated local y. Each canvas clips to
// its own bounds independently, so the union reads as one continuous disk
// instead of being cut off at the hero canvas's bottom edge.
void draw_disk_into_scene(int gx, int gy, int r, lv_color_t color) {
  if (gy + r >= 0 && gy - r < HERO_H && g_hero_canvas) {
    fill_circle(g_hero_canvas, gx, gy, r, color);
  }
  for (int row = 0; row < ROW_COUNT; row++) {
    int row_start = HERO_H + row * ROW_H;
    int row_end = row_start + ROW_H;
    if (gy + r >= row_start && gy - r < row_end && g_row_canvas[row]) {
      fill_circle(g_row_canvas[row], gx, gy - row_start, r, color);
    }
  }
}

// ---- animated background: temp-colored fill + drifting glow + particles --

void animation_tick(float dt_seconds) {
  g_current_temp_c += (g_target_temp_c - g_current_temp_c) * SMOOTH_K;
  g_current_wind += (g_target_wind - g_current_wind) * SMOOTH_K;

  g_t += dt_seconds * 0.35f;

  float pulse_speed = 1.5f;
  if (strcmp(g_pressure_direction, "rising") == 0) {
    pulse_speed = 2.4f;
  } else if (strcmp(g_pressure_direction, "falling") == 0) {
    pulse_speed = 0.9f;
  }
  g_glow_phase += dt_seconds * pulse_speed;
  float pulse = 0.85f + 0.15f * sinf(g_glow_phase);

  uint8_t r, g, b;
  temp_to_rgb(g_current_temp_c, r, g, b);

  rgb_led_update(r, g, b, dt_seconds, g_current_temp_c);

  // Plain opaque full-brightness fill -- matching ESP32_experiment1's own
  // animation.cpp (`canvas->fillScreen(RGB565(r,g,b))`), not the PWA web
  // canvas's low-alpha darkened-trail technique. The two ambient scenes
  // diverge here: the web version darkens for a moody, muted look; the
  // physical sibling device (the actual reference for this build) is a
  // solid, vivid field with a subtle lightened glow on top.
  lv_color_t bg_color = lv_color_make(r, g, b);
  lv_canvas_fill_bg(g_hero_canvas, bg_color, LV_OPA_COVER);
  fill_row_backgrounds(bg_color);
  // Keeps the small uncovered strip below the stat rows (where the status
  // label sits, outside any canvas) in sync with the same live color.
  if (g_tab) lv_obj_set_style_bg_color(g_tab, bg_color, 0);

  // Drifting glow -- a slow Lissajous wander, opaque concentric circles
  // lightened toward white (not alpha blending), matching animation.cpp's
  // lightenChannel-based rings exactly. Wanders across most of the full
  // scene height (not just the hero canvas) via draw_disk_into_scene, which
  // draws each ring into every canvas its bounding box overlaps -- without
  // that, a 55px-radius disk drawn only on the 100px-tall hero canvas gets
  // hard-clipped at its bottom edge instead of continuing behind the stat
  // rows. Drawn after the background fills but before each row's own
  // content, so it sits behind the dot/value/sparkline/label text.
  int glow_cx = SCENE_W / 2 + static_cast<int>(sinf(g_t * 0.8f) * SCENE_W * 0.22f);
  int glow_cy = SCENE_H / 2 + static_cast<int>(cosf(g_t * 0.55f) * (SCENE_H * 0.28f));
  int max_r = static_cast<int>(55.0f * pulse);
  const int glow_steps = 16;
  for (int i = glow_steps; i >= 1; i--) {
    float ft = static_cast<float>(i) / glow_steps;
    float amount = (1.0f - ft) * 0.35f;
    uint8_t lr = lighten_channel(r, amount);
    uint8_t lg = lighten_channel(g, amount);
    uint8_t lb = lighten_channel(b, amount);
    draw_disk_into_scene(glow_cx, glow_cy, static_cast<int>(max_r * ft),
                          lv_color_make(lr, lg, lb));
  }

  draw_row_contents(g_last_reading);

  // Particle count scales gently with wind -- calm days stay near the base
  // count so the scene is never static, windy days get a busier field.
  int active_particles = BASE_PARTICLES + static_cast<int>(g_current_wind * 1.1f);
  if (active_particles > MAX_PARTICLES) active_particles = MAX_PARTICLES;
  if (active_particles < BASE_PARTICLES) active_particles = BASE_PARTICLES;

  float wind_mag = 0.35f + g_current_wind * 0.05f;  // always > 0: baseline drift even when calm
  uint8_t pr = lighten_channel(r, 0.7f);
  uint8_t pg = lighten_channel(g, 0.7f);
  uint8_t pb = lighten_channel(b, 0.7f);

  for (int i = 0; i < active_particles; i++) {
    Particle &p = g_particles[i];
    float angle = sinf(p.x * 0.02f + g_t) + cosf(p.y * 0.02f - g_t * 0.8f);
    float spd = p.speed * wind_mag;
    p.x += cosf(angle) * spd;
    p.y += sinf(angle) * spd;

    if (p.x < -4) {
      p.x = SCENE_W + 4;
    } else if (p.x > SCENE_W + 4) {
      p.x = -4;
    }
    if (p.y < -4) {
      p.y = SCENE_H + 4;
    } else if (p.y > SCENE_H + 4) {
      p.y = -4;
    }

    p.twinkle += 0.12f;
    float twinkle_amt = 0.5f + 0.5f * sinf(p.twinkle);
    uint8_t tr = lighten_channel(pr, twinkle_amt * 0.3f);
    uint8_t tg = lighten_channel(pg, twinkle_amt * 0.3f);
    uint8_t tb = lighten_channel(pb, twinkle_amt * 0.3f);
    draw_particle_into_scene(p.x, p.y, static_cast<int>(p.size), lv_color_make(tr, tg, tb));
  }
}

void timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  uint32_t now = lv_tick_get();
  float dt = (g_last_frame_ms == 0) ? (FRAME_INTERVAL_MS / 1000.0f)
                                     : (now - g_last_frame_ms) / 1000.0f;
  g_last_frame_ms = now;

  animation_tick(dt);
  if (g_has_reading) {
    draw_hero_overlay(g_last_reading);
  }
  lv_obj_invalidate(g_hero_canvas);
}

void canvas_click_cb(lv_event_t *e) {
  LV_UNUSED(e);
  g_use_fahrenheit = !g_use_fahrenheit;
}

// Allocates one canvas, logging heap headroom around the call so a failure
// on a more memory-constrained board shows up clearly in the serial log
// instead of silently leaving a blank widget.
lv_obj_t *create_canvas(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t **buf_out) {
  size_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h);
  *buf_out = static_cast<lv_color_t *>(malloc(buf_size));
  Serial.printf(
      "[Mem] canvas %dx%d (%u bytes): %s -- free heap=%u, largest free block=%u\n", w, h,
      static_cast<unsigned>(buf_size), *buf_out ? "OK" : "FAILED", ESP.getFreeHeap(),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  if (!*buf_out) return nullptr;

  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, *buf_out, w, h, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(canvas, x, y);
  // The default theme puts a border/outline on plain lv_obj-derived widgets
  // (canvas included) -- without clearing it, each of the 4 canvases shows
  // a thin separator line, breaking the illusion of one continuous scene.
  lv_obj_set_style_border_width(canvas, 0, 0);
  lv_obj_set_style_outline_width(canvas, 0, 0);
  lv_obj_set_style_pad_all(canvas, 0, 0);
  lv_obj_set_style_radius(canvas, 0, 0);
  return canvas;
}

}  // namespace

const char *condition_label(const WeatherReading &r) {
  if (!isnan(r.rainfall) && r.rainfall > 0.1f) return "RAIN";
  if (!isnan(r.wind_speed) && r.wind_speed > 20.0f) return "WINDY";
  if (r.pressure_trend.direction && strcmp(r.pressure_trend.direction, "rising") == 0) {
    return "PRESSURE RISING";
  }
  if (r.pressure_trend.direction && strcmp(r.pressure_trend.direction, "falling") == 0) {
    return "PRESSURE FALLING";
  }
  return "CLEAR";
}

void build_weather_tab(lv_obj_t *tab) {
  g_tab = tab;
  lv_obj_set_style_pad_all(tab, 0, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  // The default theme's light background shows through in the ~30px strip
  // below the stat rows (where the status label sits, not covered by any
  // canvas) unless the tab is explicitly made opaque -- bg_color alone
  // doesn't render without bg_opa, since a plain child object defaults to
  // transparent. Its color is kept in sync with the live scene color every
  // tick (see animation_tick) so that strip blends in rather than showing
  // a fixed color that drifts from the animated one above it.
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tab, bauhaus_black(), 0);
  lv_obj_set_style_border_width(tab, 0, 0);

  g_hero_canvas = create_canvas(tab, SCENE_W, HERO_H, 0, 0, &g_hero_buf);
  if (g_hero_canvas) {
    lv_canvas_fill_bg(g_hero_canvas, lv_color_make(0x2A, 0x2E, 0x3A), LV_OPA_COVER);
    lv_obj_add_flag(g_hero_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_hero_canvas, canvas_click_cb, LV_EVENT_CLICKED, nullptr);
  }

  for (int i = 0; i < ROW_COUNT; i++) {
    int y = HERO_H + i * (ROW_H + ROW_GAP);
    g_row_canvas[i] = create_canvas(tab, SCENE_W, ROW_H, 0, y, &g_row_buf[i]);
    if (g_row_canvas[i]) lv_canvas_fill_bg(g_row_canvas[i], bauhaus_black(), LV_OPA_COVER);
  }

  g_status_label = lv_label_create(tab);
  lv_label_set_text(g_status_label, "Loading realtime weather...");
  lv_obj_set_style_text_color(g_status_label, lv_color_white(), 0);
  lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_LEFT, 4, -2);

  for (auto &p : g_particles) spawn_particle(p);
  rgb_led_init();

  if (g_hero_canvas) g_timer = lv_timer_create(timer_cb, FRAME_INTERVAL_MS, nullptr);
}

void weather_apply_reading(const WeatherReading &reading) {
  g_last_reading = reading;
  g_has_reading = true;

  if (!isnan(reading.temp_c)) g_target_temp_c = reading.temp_c;
  if (!isnan(reading.wind_speed)) g_target_wind = reading.wind_speed;

  const char *dir = reading.pressure_trend.direction;
  strncpy(g_pressure_direction, dir ? dir : "", sizeof(g_pressure_direction) - 1);
  g_pressure_direction[sizeof(g_pressure_direction) - 1] = '\0';

  // Row canvases are redrawn every animation tick (see animation_tick's
  // call to refresh_stat_rows), not here -- this just updates the cached
  // data/targets they and the hero overlay read from.
}

void weather_set_scene_running(bool running) {
  if (!g_timer) return;
  if (running) {
    lv_timer_resume(g_timer);
  } else {
    lv_timer_pause(g_timer);
  }
}

void weather_set_status_line(const char *text) {
  if (g_status_label) lv_label_set_text(g_status_label, text);
}
