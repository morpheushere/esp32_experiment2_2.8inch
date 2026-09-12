#include "http_json.h"

#include <WiFiClient.h>

namespace {
constexpr size_t HTTP_BUF_SIZE = 2048;
char g_http_buf[HTTP_BUF_SIZE];
}  // namespace

DeserializationError http_read_json(HTTPClient &http, JsonDocument &doc) {
  int content_length = http.getSize();  // -1 if unknown (e.g. chunked encoding)
  WiFiClient *stream = http.getStreamPtr();

  size_t max_read = HTTP_BUF_SIZE - 1;
  size_t want = (content_length > 0 && static_cast<size_t>(content_length) < max_read)
                    ? static_cast<size_t>(content_length)
                    : max_read;

  size_t total = 0;
  while (total < want) {
    int n = stream->readBytes(g_http_buf + total, want - total);
    if (n <= 0) break;  // timeout or connection closed -- stop, don't spin forever
    total += static_cast<size_t>(n);
  }
  g_http_buf[total] = '\0';

  return deserializeJson(doc, g_http_buf, total);
}
