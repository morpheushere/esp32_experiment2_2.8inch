#include "network_health.h"

#include <Arduino.h>

namespace {

// Chosen to comfortably absorb a handful of transient failures (one bad
// response, one momentary WiFi blip) without restarting, while still
// recovering within a couple of minutes of poll cycles once genuine
// fragmentation-driven failures start -- Spotify's 8s interval means 10
// consecutive failures there is under 90s before recovery kicks in.
constexpr int FAILURE_THRESHOLD = 10;

int g_consecutive_failures = 0;

}  // namespace

void network_health_record_result(bool ok) {
  if (ok) {
    g_consecutive_failures = 0;
    return;
  }

  g_consecutive_failures++;
  Serial.printf("[NetHealth] consecutive failures=%d/%d\n", g_consecutive_failures,
                FAILURE_THRESHOLD);

  if (g_consecutive_failures >= FAILURE_THRESHOLD) {
    Serial.println("[NetHealth] threshold reached -- restarting to clear heap fragmentation");
    Serial.flush();
    delay(100);  // let the serial line finish flushing before reset
    ESP.restart();
  }
}
