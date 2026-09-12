#include "tab_claude.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bauhaus_colors.h"
#include "claude_approval_client.h"

// Layout note: the tab content area is only ~208px tall (240px screen -
// 32px tab bar). An earlier version stacked a 48px logo + full-size name +
// wrapped quip + wrapped tool line + a separate queue-count line + two
// 52px buttons vertically -- that totaled well over 208px, so the flex
// column (vertically centered, not scrollable) clipped *both* ends: the
// logo above the visible area and the Deny button below it (confirmed
// live: neither was visible, matching this math exactly). Every element
// below is now either fixed-position outside the vertical stack (the
// logo) or forced to a single line via LV_LABEL_LONG_SCROLL_CIRCULAR
// (marquee-scrolls instead of wrapping) so the total stack height is
// small and predictable regardless of text length.

namespace {

// ---- quirky rotating copy -------------------------------------------------
// Bucketed by tool_name -- the only "context" we actually have per request
// (no per-project stage/history tracking exists yet). Picked once per new
// request_id, not on every poll tick, so it stays stable while showing.

const char *BASH_QUIPS[] = {
    "With great `rm` comes great responsibility.",
    "The terminal never lies. Usually.",
    "Bold commands for bold humans.",
    "This one might echo for a while.",
    "Somewhere, a shell script holds its breath.",
    "Permission is temporary. Consequences are not.",
};
constexpr int BASH_QUIP_COUNT = sizeof(BASH_QUIPS) / sizeof(BASH_QUIPS[0]);

const char *GENERAL_QUIPS[] = {
    "The code is ready. Are you?",
    "One tap closer to shipped.",
    "Big things await. Take the leap.",
    "Every masterpiece needs a green light.",
    "A small yes, a large consequence.",
    "Choose. The future is watching. Casually.",
};
constexpr int GENERAL_QUIP_COUNT = sizeof(GENERAL_QUIPS) / sizeof(GENERAL_QUIPS[0]);

// ---- logo mark --------------------------------------------------------
// A small procedurally-drawn pulsing asterisk/starburst -- a stylized
// original take on the general shape of Claude's mark, not a pixel-exact
// reproduction of Anthropic's actual logo asset. Fixed-positioned in the
// tab's top-right corner (a child of the tab itself, not the vertically
// stacked content below) so it never competes with that stack for the
// limited vertical space, and stays visible regardless of how much text
// is showing.

constexpr int LOGO_SIZE = 32;
constexpr int LOGO_RAYS = 8;
constexpr uint32_t LOGO_FRAME_INTERVAL_MS = 100;  // ~10fps, plenty for a slow decorative pulse

lv_obj_t *g_logo_canvas = nullptr;
lv_color_t *g_logo_buf = nullptr;
lv_timer_t *g_logo_timer = nullptr;
float g_logo_phase = 0.0f;

void logo_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (!g_logo_canvas) return;
  g_logo_phase += 0.15f;

  lv_canvas_fill_bg(g_logo_canvas, bauhaus_black(), LV_OPA_COVER);

  constexpr int cx = LOGO_SIZE / 2;
  constexpr int cy = LOGO_SIZE / 2;
  float pulse = 0.7f + 0.3f * sinf(g_logo_phase);
  int ray_len = static_cast<int>((LOGO_SIZE / 2 - 3) * pulse);

  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = claude_orange();
  dsc.width = 3;
  dsc.round_start = 1;
  dsc.round_end = 1;

  for (int i = 0; i < LOGO_RAYS; i++) {
    float angle = (2.0f * static_cast<float>(M_PI) * i) / LOGO_RAYS;
    int x2 = cx + static_cast<int>(cosf(angle) * ray_len);
    int y2 = cy + static_cast<int>(sinf(angle) * ray_len);
    lv_point_t pts[2] = {{static_cast<lv_coord_t>(cx), static_cast<lv_coord_t>(cy)},
                          {static_cast<lv_coord_t>(x2), static_cast<lv_coord_t>(y2)}};
    lv_canvas_draw_line(g_logo_canvas, pts, 2, &dsc);
  }

  lv_obj_invalidate(g_logo_canvas);
}

// ---- widgets ------------------------------------------------------------

lv_obj_t *g_placeholder = nullptr;
lv_obj_t *g_content = nullptr;
lv_obj_t *g_project_label = nullptr;
lv_obj_t *g_quip_label = nullptr;
lv_obj_t *g_tool_label = nullptr;

char g_shown_request_id[CLAUDE_REQUEST_ID_LEN] = "";

void accept_clicked_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (g_shown_request_id[0]) claude_approval_submit_decision(g_shown_request_id, "allow");
}

void deny_clicked_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (g_shown_request_id[0]) claude_approval_submit_decision(g_shown_request_id, "deny");
}

enum ButtonStyle { BTN_SOLID, BTN_OUTLINE };

lv_obj_t *make_button(lv_obj_t *parent, const char *text, ButtonStyle style, lv_color_t color,
                       lv_coord_t width_pct, lv_coord_t height) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, LV_PCT(width_pct), height);
  lv_obj_set_style_radius(btn, 8, 0);
  if (style == BTN_SOLID) {
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
  } else {
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, color, 0);
  }

  // No icon prefix -- an icon+text combo reads visually left-heavy even
  // when the bounding box is geometrically centered (confirmed live: the
  // checkmark+"Accept" looked off-center). Plain text centers correctly.
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, style == BTN_SOLID ? bauhaus_white() : color, 0);
  lv_obj_center(label);
  return btn;
}

// Forces a label to a single row regardless of content length -- long
// text marquee-scrolls instead of wrapping, which would otherwise blow
// past our tight vertical budget.
void make_single_line(lv_obj_t *label, lv_coord_t width_pct) {
  lv_obj_set_width(label, LV_PCT(width_pct));
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

}  // namespace

void build_claude_tab(lv_obj_t *tab) {
  lv_obj_set_style_pad_all(tab, 10, 0);
  lv_obj_set_style_pad_bottom(tab, 16, 0);  // gutter so buttons never sit edge-to-edge
  lv_obj_set_style_bg_color(tab, bauhaus_black(), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab, 0, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

  g_placeholder = lv_label_create(tab);
  lv_label_set_text(g_placeholder, "No pending decisions");
  lv_obj_set_style_text_color(g_placeholder, lv_color_make(160, 160, 160), 0);
  lv_obj_center(g_placeholder);

  // Logo: fixed in the corner, outside the vertical content stack -- see
  // the file header comment for why.
  size_t logo_buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(LOGO_SIZE, LOGO_SIZE);
  g_logo_buf = static_cast<lv_color_t *>(malloc(logo_buf_size));
  if (g_logo_buf) {
    g_logo_canvas = lv_canvas_create(tab);
    lv_canvas_set_buffer(g_logo_canvas, g_logo_buf, LOGO_SIZE, LOGO_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_style_border_width(g_logo_canvas, 0, 0);
    lv_canvas_fill_bg(g_logo_canvas, bauhaus_black(), LV_OPA_COVER);
    lv_obj_align(g_logo_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    g_logo_timer = lv_timer_create(logo_timer_cb, LOGO_FRAME_INTERVAL_MS, nullptr);
  }

  g_content = lv_obj_create(tab);
  lv_obj_set_size(g_content, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(g_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_content, 0, 0);
  lv_obj_set_style_pad_all(g_content, 0, 0);
  lv_obj_set_style_pad_row(g_content, 8, 0);
  lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_content, LV_OBJ_FLAG_HIDDEN);

  g_project_label = lv_label_create(g_content);
  lv_obj_set_style_text_font(g_project_label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(g_project_label, bauhaus_yellow(), 0);
  make_single_line(g_project_label, 80);  // narrower than the others -- leaves room for the logo
  lv_label_set_text(g_project_label, "");

  g_quip_label = lv_label_create(g_content);
  lv_obj_set_style_text_color(g_quip_label, lv_color_make(200, 200, 200), 0);
  make_single_line(g_quip_label, 92);
  lv_label_set_text(g_quip_label, "");

  g_tool_label = lv_label_create(g_content);
  lv_obj_set_style_text_color(g_tool_label, bauhaus_white(), 0);
  make_single_line(g_tool_label, 92);
  lv_label_set_text(g_tool_label, "");

  lv_obj_t *accept_btn = make_button(g_content, "Accept", BTN_SOLID, claude_orange(), 84, 42);
  lv_obj_add_event_cb(accept_btn, accept_clicked_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *deny_btn = make_button(g_content, "Deny", BTN_OUTLINE, bauhaus_red(), 84, 42);
  lv_obj_add_event_cb(deny_btn, deny_clicked_cb, LV_EVENT_CLICKED, nullptr);
}

void claude_apply_pending(const ClaudePendingRequest &request) {
  bool has_request = request.request_id[0] != '\0';

  if (!has_request) {
    if (g_shown_request_id[0] != '\0') {
      g_shown_request_id[0] = '\0';
      lv_obj_clear_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_content, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (strcmp(g_shown_request_id, request.request_id) == 0) return;  // nothing changed
  strlcpy(g_shown_request_id, request.request_id, sizeof(g_shown_request_id));

  lv_obj_add_flag(g_placeholder, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_content, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(g_project_label, request.project);

  bool is_bash = strcmp(request.tool_name, "Bash") == 0;
  const char *quip = is_bash ? BASH_QUIPS[rand() % BASH_QUIP_COUNT]
                              : GENERAL_QUIPS[rand() % GENERAL_QUIP_COUNT];
  lv_label_set_text(g_quip_label, quip);

  // Queue count folded into this line (instead of a separate row) to keep
  // the total stack height small -- see the file header comment.
  char tool_line[CLAUDE_TOOL_NAME_LEN + CLAUDE_SUMMARY_LEN + 24];
  if (request.queue_count > 1) {
    snprintf(tool_line, sizeof(tool_line), "%s: %s  (+%d more waiting)", request.tool_name,
              request.summary, request.queue_count - 1);
  } else {
    snprintf(tool_line, sizeof(tool_line), "%s: %s", request.tool_name, request.summary);
  }
  lv_label_set_text(g_tool_label, tool_line);
}
