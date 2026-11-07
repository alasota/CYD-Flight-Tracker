// opensky_settings — OpenSky's settings objects (spec 12 §7): the account it
// logs in with and how often it is asked. Settings (admin/core/config_store.h)
// holds them as `opensky` and `poll`; opensky_client's adapter reads only
// these (plus airspace's Home for the bounding box).
//
// Header-only, no logic: the clamps live in config_store (the aggregate keeps
// its invariants). ⚠️ env:cyd is gnu++11 — with default member initializers
// these structs aren't aggregates there: assign members, never `X{…}`.
#pragma once

#include <cstdint>
#include <string>

// OAuth2 client credentials (docs/data-sources.md "Authentication"). Both
// empty = anonymous; wrong ones fall back to anonymous (opensky_auth).
struct OpenSkyCredentials {
  std::string client_id;
  std::string client_secret;
};

struct PollSettings {
  // Seconds between /states/all requests; never polled faster
  // (docs/data-sources.md "Rate limits"), floor in clampPollIntervalS().
  uint32_t interval_s = 15;
};
