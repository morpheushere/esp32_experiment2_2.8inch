#pragma once

#include <lvgl.h>

#define CALENDAR_MAX_EVENTS 12
#define CALENDAR_TITLE_LEN 64
#define CALENDAR_TIME_LABEL_LEN 16

struct CalendarEvent {
  char title[CALENDAR_TITLE_LEN] = "";
  char time_label[CALENDAR_TIME_LABEL_LEN] = "";  // "9:00 AM" or "All day"
};

struct CalendarAgenda {
  CalendarEvent events[CALENDAR_MAX_EVENTS];
  int count = 0;
};

// Creates tab 3's layout: a vertically scrollable list of up to
// CALENDAR_MAX_EVENTS rows (time + title), pre-created and toggled
// hidden/visible on each update rather than recreated, plus a "No events
// today" placeholder. Call once from ui_tabview's setup.
void build_calendar_tab(lv_obj_t *tab);

// Updates the visible rows from a freshly parsed agenda. Safe to call with
// agenda.count == 0 (shows the placeholder instead).
void calendar_apply_agenda(const CalendarAgenda &agenda);
