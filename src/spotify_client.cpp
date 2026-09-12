#include "spotify_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#include "http_json.h"
#include "network_health.h"
#include "secrets.h"
#include "tab_spotify.h"

// Backend deployed and verified live via curl (real track/art data
// confirmed at /api/spotify/now-playing and /api/spotify/art.raw) --
// mock mode off, live HTTP path now in use. Re-enable to bypass HTTP with
// a fixed sample payload + checkerboard test pattern again.
// #define USE_MOCK_SPOTIFY_DATA

namespace {

// Was 5000 (matching the backend's own sync interval exactly) -- loosened
// to reduce HTTPClient/JSON allocation churn contributing to the heap
// fragmentation that broke the album art canvas. Still feels live for a
// "now playing" display; the backend's own cache updates independently.
constexpr unsigned long POLL_INTERVAL_MS = 8000;

const char *MOCK_NOW_PLAYING_JSON = R"json(
{
  "is_playing": true,
  "track": "A Very Long Song Title That Should Definitely Marquee-Scroll",
  "artist": "Some Artist",
  "album": "Mock Album",
  "art_id": "mock-album-1",
  "progress_ms": 42000,
  "duration_ms": 210000
}
)json";

bool g_polling_running = true;
bool g_canvas_pending = false;
unsigned long g_last_poll_ms = 0;
char g_last_art_id[40] = "";

// Set by spotify_submit_control() (called from a touch-event callback in
// tab_spotify.cpp), consumed by the next real_poll_tick() call from
// loop() -- same deferred pattern as claude_approval_client.cpp's
// g_pending_decision, and for the same reason: never do blocking network
// I/O synchronously inside a touch-event callback.
char g_pending_control[16] = "";

void fill_mock_art_pattern() {
  lv_color_t *buf = spotify_art_buffer();
  if (!buf) return;
  for (int y = 0; y < SPOTIFY_ART_SIZE; y++) {
    for (int x = 0; x < SPOTIFY_ART_SIZE; x++) {
      bool checker = ((x / 15) + (y / 15)) % 2 == 0;
      buf[y * SPOTIFY_ART_SIZE + x] =
          checker ? lv_color_make(30, 200, 120) : lv_color_make(20, 40, 60);
    }
  }
  spotify_art_updated();
  Serial.println("[Spotify Mock] filled test-pattern art");
}

// Shared by both the mock and real paths. Returns true if art_id changed,
// so the caller knows whether a follow-up art fetch is needed.
bool apply_now_playing_json(JsonDocument &doc) {
  SpotifyNowPlaying data;
  data.is_playing = doc["is_playing"] | false;
  strlcpy(data.track, doc["track"] | "", sizeof(data.track));
  strlcpy(data.artist, doc["artist"] | "", sizeof(data.artist));
  strlcpy(data.album, doc["album"] | "", sizeof(data.album));
  strlcpy(data.art_id, doc["art_id"] | "", sizeof(data.art_id));
  data.progress_ms = doc["progress_ms"] | -1L;
  data.duration_ms = doc["duration_ms"] | -1L;

  Serial.printf("[Spotify] is_playing=%d track=\"%s\" artist=\"%s\" art_id=%s\n", data.is_playing,
                data.track, data.artist, data.art_id[0] ? data.art_id : "(none)");

  spotify_apply_now_playing(data);

  bool art_changed = data.art_id[0] != '\0' && strcmp(data.art_id, g_last_art_id) != 0;
  if (art_changed) strlcpy(g_last_art_id, data.art_id, sizeof(g_last_art_id));
  return art_changed;
}

#ifdef USE_MOCK_SPOTIFY_DATA

void mock_poll_tick() {
  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, MOCK_NOW_PLAYING_JSON);
  if (err == DeserializationError::Ok) {
    if (apply_now_playing_json(doc)) fill_mock_art_pattern();
  } else {
    Serial.printf("[Spotify Mock] JSON parse failed: %s\n", err.c_str());
  }
}

#else

// Streams the backend's raw RGB565 dump directly into the art canvas's
// pixel buffer -- no decoding, since the bytes are already in the exact
// format lv_color_t expects (see api/spotify_client.py's _rgb565_bytes()).
void fetch_art() {
  lv_color_t *buf = spotify_art_buffer();
  if (!buf) {
    // The first allocation attempt (on tab activation) can fail under
    // heap fragmentation from concurrent WiFi/HTTP/JSON activity -- worth
    // retrying right when there's actually new art to show, not just
    // waiting for the next tab-switch. ensure_art_canvas() is idempotent
    // and cheap to call speculatively.
    spotify_ensure_art_canvas();
    buf = spotify_art_buffer();
    if (!buf) return;
  }

  // Fixed buffer + snprintf, not String concatenation -- see the comment
  // on this same pattern in weather_client.cpp's real_poll_tick().
  char url[96];
  snprintf(url, sizeof(url), "%s/api/spotify/art.raw", API_BASE_URL);
  HTTPClient http;
  http.begin(url);
  // Keep-alive reuse is unsafe here: the size-mismatch and incomplete-read
  // branches below can both leave part or all of the body unread on the
  // socket -- HTTPClient would otherwise think that connection is still
  // clean and hand it back on the next request, corrupting it. Confirmed
  // live: this exact pattern produced a permanently broken socket (write()
  // failing forever on the same fd) after enough poll cycles. Forcing a
  // fresh connection per request avoids it entirely.
  http.setReuse(false);
  http.setTimeout(8000);
  int code = http.GET();

  if (code == 200) {
    size_t expected =
        static_cast<size_t>(SPOTIFY_ART_SIZE) * SPOTIFY_ART_SIZE * sizeof(lv_color_t);
    int len = http.getSize();
    if (len != static_cast<int>(expected)) {
      Serial.printf("[Spotify] art size mismatch: got %d bytes, expected %u -- skipping\n", len,
                    static_cast<unsigned>(expected));
      network_health_record_result(false);
    } else {
      WiFiClient *stream = http.getStreamPtr();
      size_t read_total = stream->readBytes(reinterpret_cast<uint8_t *>(buf), expected);
      if (read_total == expected) {
        spotify_art_updated();
        Serial.printf("[Spotify] art updated (%u bytes)\n", static_cast<unsigned>(expected));
        network_health_record_result(true);
      } else {
        Serial.printf("[Spotify] art read incomplete: %u/%u bytes\n",
                       static_cast<unsigned>(read_total), static_cast<unsigned>(expected));
        network_health_record_result(false);
      }
    }
  } else if (code != 404) {
    // 404 is the expected response before any track change has happened
    // since the backend last started -- not a real failure.
    Serial.printf("[Spotify] art GET failed, code=%d\n", code);
    network_health_record_result(false);
  }
  http.end();
}

// Sends whichever action was queued by a button tap to the backend's
// proxy route. Fire-and-forget with respect to the *result* -- errors
// (most commonly "no active device", per the backend's own control()
// docstring) are logged, not surfaced in the UI, matching this codebase's
// existing pattern of degrading quietly rather than showing error state
// for things the user can't act on from this screen anyway. The next
// regular poll (forced immediate via g_last_poll_ms = 0) reflects
// whatever actually happened on Spotify's side.
void send_pending_control() {
  // Fixed buffer + snprintf, not String concatenation -- see the comment
  // on this same pattern in weather_client.cpp's real_poll_tick().
  char url[96];
  snprintf(url, sizeof(url), "%s/api/spotify/control/%s", API_BASE_URL, g_pending_control);
  HTTPClient http;
  http.begin(url);
  http.setReuse(false);
  http.setTimeout(5000);
  // uint8_t* overload, not POST(String) -- an empty string literal
  // implicitly converts to a temporary String otherwise, one more small
  // allocation on every button tap. No body needed either way -- the
  // action is in the URL path.
  int code = http.POST(nullptr, 0);
  Serial.printf("[Spotify] control '%s' -> code=%d\n", g_pending_control, code);
  // The backend route always answers 200 (with ok:false inside the body
  // for app-level outcomes like "no active device") -- so a non-2xx code
  // here specifically means the socket-level request itself failed, which
  // is exactly what network health cares about, not whether Spotify had
  // something to act on.
  network_health_record_result(code >= 200 && code < 300);
  http.end();
  g_pending_control[0] = '\0';
  g_last_poll_ms = 0;  // force an immediate re-poll next tick to reflect the change
}

void real_poll_tick() {
  if (WiFi.status() != WL_CONNECTED) return;  // weather_client owns WiFi connect/retry

  // A button tap takes priority over the regular poll, and happens
  // immediately rather than waiting for the next interval -- same
  // reasoning as claude_approval_client.cpp's decision-priority check.
  if (g_pending_control[0]) {
    send_pending_control();
    return;
  }

  if (g_last_poll_ms != 0 && millis() - g_last_poll_ms < POLL_INTERVAL_MS) return;
  g_last_poll_ms = millis();

  bool art_changed = false;
  {
    // Fixed buffer + snprintf, not String concatenation -- see the
    // comment on this same pattern in weather_client.cpp's
    // real_poll_tick().
    char url[96];
    snprintf(url, sizeof(url), "%s/api/spotify/now-playing", API_BASE_URL);
    HTTPClient http;
    http.begin(url);
    // See the setReuse() comment in fetch_art() below -- same reasoning
    // applies to this endpoint's response body.
    http.setReuse(false);
    http.setTimeout(5000);
    int code = http.GET();
    if (code == 200) {
      // http_read_json() -- shared bounded-read helper, verified live to
      // avoid both the fragmentation getString() caused and the hang
      // stream-based parsing caused. See src/http_json.h.
      StaticJsonDocument<512> doc;
      DeserializationError err = http_read_json(http, doc);
      if (err == DeserializationError::Ok) {
        art_changed = apply_now_playing_json(doc);
        network_health_record_result(true);
      } else {
        Serial.printf("[Spotify] JSON parse failed: %s\n", err.c_str());
        network_health_record_result(false);
      }
    } else {
      Serial.printf("[Spotify] now-playing GET failed, code=%d\n", code);
      network_health_record_result(false);
    }
    http.end();
  }
  // Separate request, started only after the metadata one above has fully
  // closed -- sequential, not concurrent, HTTP connections.
  if (art_changed) fetch_art();
}

#endif  // USE_MOCK_SPOTIFY_DATA

}  // namespace

void spotify_client_init() {
#ifdef USE_MOCK_SPOTIFY_DATA
  Serial.println("[Spotify Mock] USE_MOCK_SPOTIFY_DATA enabled - skipping HTTP");
#else
  Serial.println("[Spotify] waiting for WiFi (owned by weather_client)...");
#endif
}

void spotify_client_tick() {
  // Deliberately done here, not inside spotify_set_polling_running(), even
  // though that's called exactly once when this becomes relevant. That
  // function runs *inside* the tab button's touch-event callback chain --
  // creating a new LVGL widget (malloc + lv_canvas_create + several style
  // calls) synchronously from there, while LVGL is still mid-way through
  // dispatching that same touch event, corrupted the tabview's touch/state
  // tracking badly enough that no further tap could switch tabs at all
  // (reproduced live on this board). Deferring the actual creation to here
  // -- loop()'s own call, entirely outside any input-event dispatch --
  // avoids that reentrancy risk.
  if (g_canvas_pending) {
    g_canvas_pending = false;
    spotify_ensure_art_canvas();
  }

  if (!g_polling_running) return;
#ifdef USE_MOCK_SPOTIFY_DATA
  mock_poll_tick();
#else
  real_poll_tick();
#endif
}

void spotify_set_polling_running(bool running) {
  g_polling_running = running;
  if (running) g_canvas_pending = true;
}

void spotify_submit_control(const char *action) {
  strlcpy(g_pending_control, action, sizeof(g_pending_control));
}
