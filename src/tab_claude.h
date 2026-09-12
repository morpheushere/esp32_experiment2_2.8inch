#pragma once

#include <lvgl.h>

#define CLAUDE_REQUEST_ID_LEN 40
#define CLAUDE_PROJECT_LEN 32
#define CLAUDE_TOOL_NAME_LEN 24
#define CLAUDE_SUMMARY_LEN 88

struct ClaudePendingRequest {
  char request_id[CLAUDE_REQUEST_ID_LEN] = "";  // empty means "nothing pending"
  char project[CLAUDE_PROJECT_LEN] = "";
  char tool_name[CLAUDE_TOOL_NAME_LEN] = "";
  char summary[CLAUDE_SUMMARY_LEN] = "";
  int queue_count = 0;  // total pending including this one, for a "+N more" hint
};

// Creates tab 4's layout: a "No pending decisions" placeholder, and a
// (initially hidden) content view with the project name, tool name +
// truncated summary, a big Accept button and a smaller Deny button below
// it. Call once from ui_tabview's setup.
void build_claude_tab(lv_obj_t *tab);

// Shows the given pending request, or the placeholder if
// request.request_id is empty. Safe to call repeatedly with the same
// request (e.g. on every poll tick) -- only actually changes widget text
// when the request_id differs from what's already shown.
void claude_apply_pending(const ClaudePendingRequest &request);
