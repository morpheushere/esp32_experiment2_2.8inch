#pragma once

#include <ArduinoJson.h>
#include <HTTPClient.h>

// Reads an already-GET'd HTTPClient response into a small shared static
// buffer and parses it as JSON, instead of the http.getString() pattern
// every client used before (each call allocating and freeing a full-body
// String -- confirmed live to be the source of heap fragmentation severe
// enough to break the Spotify art canvas's allocation over a session's
// uptime). This is NOT a switch to deserializeJson(doc, http.getStream())
// -- that was tried and confirmed to hang the whole device (a known
// ArduinoJson+HTTPClient incompatibility with stream-based parsing on a
// keep-alive connection). This does its own bounded, timeout-safe read
// loop instead, sidestepping that code path entirely.
//
// The buffer is intentionally small (2KB) and static: today's lesson was
// that .bss and the heap share the same DRAM pool on the ESP32, and an
// 8KB static reservation was enough to break WiFi's own init margin.
// 2KB is comfortably under that, and this buffer is verified empirically
// (see the architecture plan) before being trusted, not assumed safe.
//
// Call this only after http.GET() has already returned 200 -- it doesn't
// call GET() itself, just reads and parses whatever response is pending.
DeserializationError http_read_json(HTTPClient &http, JsonDocument &doc);
