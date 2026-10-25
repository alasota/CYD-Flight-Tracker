// hexdb_http — the one HTTPS GET to hexdb.io shared by aircraft_lookup and
// route_lookup (spec/04-dual-core.md §5.8).
//
// With keep-alive one TLS session serves a whole batch of lookups (one poll's
// phase B) instead of a fresh handshake per request; hexdbSessionEnd() closes
// it after the batch, so no TLS context (~40-50 KB of heap) is held between
// polls. A request on a connection the server has meanwhile closed is retried
// once on a new one.
//
// Device-only adapter (WiFiClientSecure / HTTPClient) — the lookup logic and
// its caches stay pure in aircraft_lookup / route_lookup.
#pragma once

#ifdef ARDUINO

#include <string>

struct HexdbHttpResult {
  int code = 0;          // HTTP status, <= 0 = transport error
  std::string body;
};

// GET `url` (https://hexdb.io/...), reusing the open session if there is one.
HexdbHttpResult hexdbGet(const char *url);

// Closes the session (end of a batch). Cheap when nothing is open.
void hexdbSessionEnd();

#endif  // ARDUINO
