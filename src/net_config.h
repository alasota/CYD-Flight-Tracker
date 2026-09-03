// net_config — shared bounds on a single blocking HTTP request. loop() is
// non-blocking cooperative (touch, config_portal, wifi_manager all run in
// it), so every HTTPClient call it can trigger must have a hard deadline
// rather than inheriting HTTPClient's untuned default — see CLAUDE.md
// review notes 1.1 / 5.1. One definition here instead of a private copy in
// opensky_client, aircraft_lookup and route_lookup.
#pragma once

#include <cstdint>

// TCP connect phase — same for every host.
constexpr int32_t kHttpConnectTimeoutMs = 5000;

// OpenSky's token + /states/all endpoints: the primary data path, worth
// waiting a bit longer on a slow response.
constexpr uint16_t kOpenSkyHttpTimeoutMs = 8000;

// hexdb.io airline/route/airport enrichment is optional polish. A slow
// lookup must not stall the whole poll cycle (the row just renders "--"
// and is retried later), so it gets a tighter read deadline.
constexpr uint16_t kHexdbHttpTimeoutMs = 4000;
