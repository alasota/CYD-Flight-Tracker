// opensky_client — OpenSky Network REST API client: OAuth2 token fetch/
// refresh, bounding-box math, /states/all polling + parsing. See CLAUDE.md
// "Data source: OpenSky Network REST API". No TFT/drawing code here (see
// CLAUDE.md "Code conventions") — returns a plain Aircraft list for
// whatever view module ends up consuming it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---- Pure logic (no WiFi/HTTP dependency) — testable under
// `pio test -e native`.

struct BoundingBox {
  float lamin = 0.0f;
  float lomin = 0.0f;
  float lamax = 0.0f;
  float lomax = 0.0f;
};

// Bounding box for a ±radius_deg half-width square around (lat, lon),
// clamped to valid lat/lon ranges. Same half-width convention as
// config_store's Config::radius_deg / config_portal's bboxAreaSqDeg() — a
// radius_deg that crosses config_portal's 1-credit threshold (2.5°)
// produces a proportionally larger box here. Does not handle antimeridian
// wraparound (a home point near ±180° longitude just gets clipped, not
// wrapped) — an accepted limitation for a fixed home-location tracker.
BoundingBox computeBoundingBox(float lat, float lon, float radius_deg);

// Cached-token state as plain data, so tests can construct it directly
// without performing any HTTP call.
struct TokenState {
  std::string access_token;
  uint32_t obtained_at_ms = 0;
  uint32_t expires_in_s = 0;
  bool valid = false;
};

// True if `token` must be (re)fetched now. Refreshes proactively
// `refresh_margin_s` before expiry (default 60s, per CLAUDE.md
// "Authentication") rather than waiting for a 401. `now_ms` and
// `token.obtained_at_ms` are millis()-style values — unsigned subtraction
// keeps this correct across a millis() rollover (~49 days uptime).
bool tokenNeedsRefresh(const TokenState &token, uint32_t now_ms, uint32_t refresh_margin_s = 60);

// True if `now_ms` is still before the deadline `until_ms` (e.g. a 429
// rate-limit cooldown), correctly handling millis() rollover the same way
// tokenNeedsRefresh() does — see CLAUDE.md review notes 1.4 (the original
// `now < until` comparison here was NOT rollover-safe).
bool isBeforeDeadline(uint32_t now_ms, uint32_t until_ms);

// Whether a fresh /states/all poll is due: true if one has never run, or
// if at least `interval_ms` has elapsed since the last one. Unsigned
// subtraction keeps it correct across a millis() rollover. Factored out of
// openSkyClientPoll()'s adapter so the "never hammer the API faster than
// the configured interval" rule (CLAUDE.md "Rate limits") is native-tested
// rather than living only in device-only code — see review notes 3.3.
bool shouldPollNow(uint32_t now_ms, uint32_t last_poll_ms, bool ever_polled, uint32_t interval_ms);

// False (anonymous access) when either credential is empty — the fallback
// mode described in CLAUDE.md "Authentication".
bool shouldUseOAuth(const std::string &client_id, const std::string &client_secret);

struct Aircraft {
  std::string icao24;
  std::string callsign;
  float lat = 0.0f;
  float lon = 0.0f;
  bool has_position = false;  // false if lat/lon were null in the response
  float baro_altitude = 0.0f;
  float velocity = 0.0f;
  float true_track = 0.0f;
  bool on_ground = false;        // state vector index 8
  float vertical_rate = 0.0f;    // state vector index 11, m/s, positive = climbing
};

// Hard ceiling on how large a /states/all response body parseStatesResponse()
// will attempt to parse. A well-formed response for even a maxed-out (400
// sq deg, per config_store's radius clamp) bounding box over a busy
// airspace is a few tens of KB; this is a defensive cap against an
// unexpectedly huge or corrupted body consuming unbounded heap during JSON
// parsing — ArduinoJson's JsonDocument has no capacity limit of its own
// (see CLAUDE.md review notes 4.3).
constexpr size_t kMaxStatesResponseBytes = 65536;  // 64 KB

// Parses a /states/all JSON response body into an Aircraft list. Tolerant
// of a missing/null "states" array (-> empty result), of state vectors
// shorter than expected (-> skipped), and of null individual fields within
// a state vector (left at the struct's defaults). Callsigns are
// right-trimmed (OpenSky space-pads them to 8 chars). A body larger than
// kMaxStatesResponseBytes is rejected outright (-> empty result) without
// being parsed. Pure — uses ArduinoJson but no HTTP dependency.
std::vector<Aircraft> parseStatesResponse(const std::string &json);

// Backoff duration after a 429, per CLAUDE.md "Rate limits": the
// X-Rate-Limit-Retry-After-Seconds header value if present and a valid
// non-negative integer, otherwise `default_backoff_s`. The result is
// clamped to `max_backoff_s` so a bogus/hostile header value (e.g.
// "999999999") can't wedge the client into a multi-day silence — see
// review notes 1.4.
uint32_t parseRetryAfterSeconds(const std::string &header_value, uint32_t default_backoff_s,
                                uint32_t max_backoff_s = 3600);

// Percent-encodes a string for use in an application/x-www-form-urlencoded
// POST body (used for the OAuth token request's client_id/client_secret).
std::string urlEncode(const std::string &value);

// Outcome of one poll attempt. The distinction between "polled OK, nothing
// in range" and "poll failed" matters: main.cpp keeps the last good
// aircraft list on a failure instead of blanking the screen, and surfaces
// a header tag — see CLAUDE.md review notes 1.2/1.3/1.4.
enum class OpenSkyPollStatus {
  NotDue,        // interval hasn't elapsed — `aircraft` is empty, ignore it
  Ok,            // fresh data in `aircraft` (may legitimately be empty)
  RateLimited,   // 429 — credits exhausted; still inside the backoff window
  AuthError,     // 401 persisted through a token refresh — check credentials
  NetworkError,  // transport failure or an unexpected HTTP status
};

struct OpenSkyPollResult {
  OpenSkyPollStatus status = OpenSkyPollStatus::NotDue;
  std::vector<Aircraft> aircraft;      // meaningful only when status == Ok
  uint32_t retry_after_s = 0;          // set when status == RateLimited
};

// --- Hardware adapter: OAuth token fetch, /states/all HTTP call, 401 retry
// and 429 backoff. Thin wrapper around WiFiClientSecure/HTTPClient — not
// covered by Unity (see CLAUDE.md "Testing"). No TFT/drawing code.
struct Config;  // config_store.h — full type only needed by the .cpp

// Fetches aircraft currently in the bounding box around the configured
// home, using `cfg` for home/radius/credentials (no internal loadConfig()
// — the caller owns config now, see review notes 4.3). Ensures a fresh
// OAuth token first when credentials are set (falls back to anonymous
// otherwise), retries once on 401, and reports 429/auth/transport failures
// distinctly rather than collapsing them all to "empty".
OpenSkyPollResult fetchAircraftStates(const Config &cfg);

// Non-blocking poll scheduler: call every loop() iteration with the
// caller's cached Config. Runs at most one fetchAircraftStates() per
// cfg.poll_interval_s (shouldPollNow()), never faster even if called more
// often — CLAUDE.md's "never hammer the API faster than the configured
// interval". Returns {NotDue} without touching the network when a poll
// isn't due yet; otherwise returns the fetch result.
OpenSkyPollResult openSkyClientPoll(uint32_t now_ms, const Config &cfg);
