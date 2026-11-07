// feed_health — how healthy the aircraft data feed is, so a silent failure
// isn't invisible: the header's status dot and the screens' empty-state
// text read it (docs/screens.md "Screen chrome — header bar and tab bar").
//
// Header-only, pure — no Arduino dependency.
#pragma once

// Health of the OpenSky data feed, surfaced as a short header tag so a
// silent failure (persistent 401, exhausted rate-limit credits, a dead
// network) isn't invisible — the aircraft list on screen may be stale
// (net_task keeps the last good AirspaceSnapshot rather than blanking it). See
// docs/review-notes-2026.md 1.2/1.3/1.4.
enum class FeedHealth {
  Ok,            // last poll succeeded
  RateLimited,   // 429 — credits exhausted, backing off
  AuthError,     // 401 persisted after a token refresh — check credentials
  NetworkError,  // transport / other HTTP failure
};
