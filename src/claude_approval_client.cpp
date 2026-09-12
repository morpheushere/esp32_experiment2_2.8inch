#include "claude_approval_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#include "secrets.h"
#include "tab_claude.h"

// Backend deployed and verified live via curl round-trip (create/list/
// decide/get all confirmed against the real NAS) -- mock mode off, live
// HTTP path now in use. Re-enable to bypass HTTP with a fixed sample
// pending request again.
// #define USE_MOCK_CLAUDE_DATA

namespace {

// Was 3000 -- loosened to reduce HTTPClient/JSON allocation churn
// contributing to the heap fragmentation that broke the Spotify album
// art canvas elsewhere on this device. Still responsive enough for a
// physical approval device.
constexpr unsigned long POLL_INTERVAL_MS = 6000;

bool g_polling_running = true;
unsigned long g_last_poll_ms = 0;

// Set by claude_approval_submit_decision() (called from a touch-event
// callback in tab_claude.cpp), consumed by the next real_poll_tick()/
// mock_poll_tick() call from loop() -- see the header comment on why this
// is deferred rather than posting immediately from the button handler.
char g_pending_decision_request_id[CLAUDE_REQUEST_ID_LEN] = "";
char g_pending_decision[8] = "";  // "allow" or "deny"

#ifdef USE_MOCK_CLAUDE_DATA

// Three varied requests -- different tool types (exercises both quip
// buckets) and different text lengths (exercises label wrapping/spacing)
// -- to accept/deny through in sequence for a repeatable spacing/animation
// check, rather than a single one-shot request.
struct MockRequest {
  const char *project;
  const char *tool_name;
  const char *summary;
};
const MockRequest MOCK_REQUESTS[] = {
    {"esp32_2.8screen", "Bash", "rm -rf /tmp/build-cache"},
    {"strava-heatmap-pwa", "Write", "api/calendar_client.py"},
    {"unity_first_project", "Edit",
     "Assets/Unplugged/Scripts/Player/PlayerController.cs"},
};
constexpr int MOCK_REQUEST_COUNT = sizeof(MOCK_REQUESTS) / sizeof(MOCK_REQUESTS[0]);
int g_mock_index = 0;

void mock_poll_tick() {
  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  ClaudePendingRequest req;
  if (g_mock_index < MOCK_REQUEST_COUNT) {
    const MockRequest &m = MOCK_REQUESTS[g_mock_index];
    snprintf(req.request_id, sizeof(req.request_id), "mock-request-%d", g_mock_index);
    strlcpy(req.project, m.project, sizeof(req.project));
    strlcpy(req.tool_name, m.tool_name, sizeof(req.tool_name));
    strlcpy(req.summary, m.summary, sizeof(req.summary));
    req.queue_count = MOCK_REQUEST_COUNT - g_mock_index;
  }
  claude_apply_pending(req);

  if (g_pending_decision[0]) {
    Serial.printf("[Claude Mock] decision=%s for %s\n", g_pending_decision,
                  g_pending_decision_request_id);
    g_pending_decision[0] = '\0';
    g_mock_index++;
  }
}

#else

void real_poll_tick() {
  if (WiFi.status() != WL_CONNECTED) return;  // weather_client owns WiFi connect/retry

  // Submitting a decision takes priority over the regular poll, and
  // happens immediately rather than waiting for the next interval, so
  // Accept/Deny feels responsive.
  if (g_pending_decision[0]) {
    String url = String(API_BASE_URL) + "/api/claude/pending/" + g_pending_decision_request_id +
                 "/decide";
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    char body[32];
    snprintf(body, sizeof(body), "{\"decision\":\"%s\"}", g_pending_decision);
    int code = http.POST(body);
    Serial.printf("[Claude] decide %s -> code=%d\n", g_pending_decision_request_id, code);
    http.end();
    g_pending_decision[0] = '\0';
    g_last_poll_ms = 0;  // force an immediate re-poll next tick to reflect the change
    return;
  }

  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  String url = String(API_BASE_URL) + "/api/claude/pending";
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    // NOTE: briefly switched to deserializeJson(doc, http.getStream()) as
    // a heap-fragmentation mitigation -- reverted after it hung the whole
    // device on a live test, right after tapping Accept (see
    // weather_client.cpp for the fuller explanation: a known ArduinoJson+
    // HTTPClient incompatibility where stream-based parsing can block
    // indefinitely on a keep-alive connection). Back to getString().
    String body = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, body);
    if (err == DeserializationError::Ok) {
      JsonArrayConst items = doc.as<JsonArrayConst>();
      ClaudePendingRequest req;
      int count = items.size();
      if (count > 0) {
        JsonObjectConst first = items[0];
        strlcpy(req.request_id, first["request_id"] | "", sizeof(req.request_id));
        strlcpy(req.project, first["project"] | "", sizeof(req.project));
        strlcpy(req.tool_name, first["tool_name"] | "", sizeof(req.tool_name));
        strlcpy(req.summary, first["tool_summary"] | "", sizeof(req.summary));
        req.queue_count = count;
      }
      claude_apply_pending(req);
    } else {
      Serial.printf("[Claude] JSON parse failed: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[Claude] GET failed, code=%d\n", code);
  }
  http.end();
}

#endif  // USE_MOCK_CLAUDE_DATA

}  // namespace

void claude_approval_client_init() {
#ifdef USE_MOCK_CLAUDE_DATA
  Serial.println("[Claude Mock] USE_MOCK_CLAUDE_DATA enabled - skipping HTTP");
#else
  Serial.println("[Claude] waiting for WiFi (owned by weather_client)...");
#endif
}

void claude_approval_client_tick() {
  if (!g_polling_running) return;
#ifdef USE_MOCK_CLAUDE_DATA
  mock_poll_tick();
#else
  real_poll_tick();
#endif
}

void claude_approval_set_polling_running(bool running) { g_polling_running = running; }

void claude_approval_submit_decision(const char *request_id, const char *decision) {
  strlcpy(g_pending_decision_request_id, request_id, sizeof(g_pending_decision_request_id));
  strlcpy(g_pending_decision, decision, sizeof(g_pending_decision));
}
