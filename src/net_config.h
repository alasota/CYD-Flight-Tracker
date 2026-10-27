// net_config — shared bounds on a single blocking HTTP request. All of them
// run in net_task on core 0 (spec/done/04-dual-core.md), so they no longer freeze
// the screen — but the web page, the OpenSky schedule and the GitHub check
// share that one task, so every HTTPClient call still has a hard deadline
// rather than inheriting HTTPClient's untuned default — see
// docs/review-notes-2026.md 1.1 / 5.1. One definition here instead of a private copy in
// opensky_client, aircraft_lookup, route_lookup and hexdb_http.
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

// GitHub's API (release_check): one small answer a day, but behind a TLS
// handshake with certificate checks.
constexpr uint16_t kGithubHttpTimeoutMs = 8000;
// The TCP connect to GitHub sometimes took over the shared 5 s above on the
// board (a lost SYN is only resent after 3 s), failing the check — this
// leaves room for two resends. The TLS handshake (certificate chain checked,
// ~2-3 s on the ESP32) has its own limit, down from the core's 120 s.
constexpr int32_t kGithubConnectTimeoutMs = 15000;
constexpr unsigned long kGithubHandshakeTimeoutS = 30;
// The firmware download: no data for this long = the connection is dead.
constexpr uint32_t kGithubDownloadStallMs = 15000;
// ...and the whole download must be done within this (~1.4 MB, ~20-40 s).
constexpr uint32_t kGithubDownloadTotalMs = 180000;
