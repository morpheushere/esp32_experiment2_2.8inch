#include "calendar_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#include "http_json.h"
#include "network_health.h"
#include "secrets.h"
#include "tab_calendar.h"

// Backend deployed and verified live via curl (real events confirmed at
// /api/calendar/today) -- mock mode off, live HTTP path now in use.
// Re-enable to bypass HTTP with a fixed sample agenda again.
// #define USE_MOCK_CALENDAR_DATA

namespace {

// Backend syncs every 10 minutes; polling a bit faster than that just
// means we pick up a fresh cache write sooner after it happens, not that
// we call the Calendar API itself any more often.
constexpr unsigned long POLL_INTERVAL_MS = 5UL * 60UL * 1000UL;

const char *MOCK_CALENDAR_JSON = R"json(
{
  "events": [
    {"title": "Team standup", "time_label": "9:00 AM"},
    {"title": "Doctor appointment", "time_label": "11:30 AM"},
    {"title": "A Very Long Meeting Title That Should Wrap Onto A Second Line", "time_label": "2:00 PM"},
    {"title": "Family dinner", "time_label": "All day"}
  ]
}
)json";

bool g_polling_running = true;
unsigned long g_last_poll_ms = 0;

// Shared by both the mock and real paths.
void apply_calendar_json(JsonDocument &doc) {
  CalendarAgenda agenda;
  JsonArrayConst items = doc["events"];  // null-safe: iterating a null array is a no-op
  int i = 0;
  for (JsonObjectConst item : items) {
    if (i >= CALENDAR_MAX_EVENTS) break;
    strlcpy(agenda.events[i].title, item["title"] | "(No title)", CALENDAR_TITLE_LEN);
    strlcpy(agenda.events[i].time_label, item["time_label"] | "", CALENDAR_TIME_LABEL_LEN);
    i++;
  }
  agenda.count = i;

  Serial.printf("[Calendar] parsed %d event(s) for today\n", agenda.count);
  calendar_apply_agenda(agenda);
}

#ifdef USE_MOCK_CALENDAR_DATA

void mock_poll_tick() {
  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, MOCK_CALENDAR_JSON);
  if (err == DeserializationError::Ok) {
    apply_calendar_json(doc);
  } else {
    Serial.printf("[Calendar Mock] JSON parse failed: %s\n", err.c_str());
  }
}

#else

void real_poll_tick() {
  if (WiFi.status() != WL_CONNECTED) return;  // weather_client owns WiFi connect/retry
  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  // Fixed buffer + snprintf, not String concatenation -- see the comment
  // on this same pattern in weather_client.cpp's real_poll_tick().
  char url[96];
  snprintf(url, sizeof(url), "%s/api/calendar/today", API_BASE_URL);
  HTTPClient http;
  http.begin(url);
  // Keep-alive reuse is unsafe here: http_read_json() only reads up to its
  // buffer size, so a response bigger than that leaves unread bytes on the
  // socket -- HTTPClient would otherwise think that connection is still
  // clean and hand it back on the next request, corrupting it. Confirmed
  // live: this exact pattern produced a permanently broken socket (write()
  // failing forever on the same fd) after enough poll cycles. Forcing a
  // fresh connection per request avoids it entirely.
  http.setReuse(false);
  http.setTimeout(8000);
  int code = http.GET();

  if (code == 200) {
    // http_read_json() -- shared bounded-read helper, verified live to
    // avoid both the fragmentation getString() caused and the hang
    // stream-based parsing caused. See src/http_json.h.
    StaticJsonDocument<2048> doc;
    DeserializationError err = http_read_json(http, doc);
    if (err == DeserializationError::Ok) {
      apply_calendar_json(doc);
      network_health_record_result(true);
    } else {
      Serial.printf("[Calendar] JSON parse failed: %s\n", err.c_str());
      network_health_record_result(false);
    }
  } else {
    Serial.printf("[Calendar] GET failed, code=%d\n", code);
    network_health_record_result(false);
  }
  http.end();
}

#endif  // USE_MOCK_CALENDAR_DATA

}  // namespace

void calendar_client_init() {
#ifdef USE_MOCK_CALENDAR_DATA
  Serial.println("[Calendar Mock] USE_MOCK_CALENDAR_DATA enabled - skipping HTTP");
#else
  Serial.println("[Calendar] waiting for WiFi (owned by weather_client)...");
#endif
}

void calendar_client_tick() {
  if (!g_polling_running) return;
#ifdef USE_MOCK_CALENDAR_DATA
  mock_poll_tick();
#else
  real_poll_tick();
#endif
}

void calendar_set_polling_running(bool running) { g_polling_running = running; }
