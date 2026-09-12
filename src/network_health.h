#pragma once

// Call after every HTTP request across every client, with whether it
// succeeded (2xx from the server, JSON parsed cleanly). Once enough
// CONSECUTIVE failures accumulate across the whole device, this restarts
// the ESP32.
//
// Why: heap fragmentation severe enough to break the socket/TLS layer
// itself (confirmed live -- "write(): fail", "fillBuffer(): Not enough
// memory to allocate buffer", "postEvent(): Arduino Event Malloc Failed!")
// cannot be reversed at runtime on this platform; only a restart clears
// it. Confirmed live that some episodes self-recover within ~10s and
// others persist indefinitely with no other recovery path -- there's no
// way to tell which is happening in advance, so this is the backstop for
// the ones that don't self-heal.
void network_health_record_result(bool ok);
