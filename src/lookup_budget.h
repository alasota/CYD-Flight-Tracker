// lookup_budget — decides which hexdb.io enrichment lookups (airline/type
// by icao24, route by callsign) main.cpp performs on a given poll cycle.
//
// Each *new* (uncached) lookup is a blocking HTTPS round trip; several
// back-to-back can freeze the cooperative loop() for seconds (touch,
// config_portal, wifi_manager all stall) — see CLAUDE.md review notes 1.1.
// So a cycle is allowed all its free cache hits plus a small number of new
// fetches, spent on the aircraft that matter most (nearest home first).
// Aircraft past the budget render as "--" and get their turn on a later
// poll.
//
// Pure — the "is this key cached?" tests are injected — so the whole
// prioritisation policy is covered by `pio test -e native` instead of
// living untested inside main.cpp (review notes 3.2).
#pragma once

#include <functional>
#include <string>
#include <vector>

// Default new-fetch ceiling per poll, shared between the two lookup kinds.
// Small on purpose (see above); a busy sky still fully resolves within a
// handful of polls. Was an ad-hoc `kMaxNewLookupsPerPoll = 5` buried in
// main.cpp (review notes 5.2).
constexpr int kDefaultLookupBudget = 2;

// One aircraft's lookup keys, in the caller's priority order.
struct LookupKeys {
  std::string icao24;    // aircraft-info lookup key
  std::string callsign;  // route lookup key ("" -> no route lookup)
};

// The lookups to actually run this cycle.
struct LookupPlan {
  std::vector<std::string> icao24;     // call lookupAircraft() for each
  std::vector<std::string> callsigns;  // call lookupRoute() for each
};

// Walks `byPriority` (nearest-first) and picks:
//   * every key already cached  — free, always included;
//   * up to `budget` NEW (uncached) fetches — shared across both kinds,
//     spent strictly in priority order.
// Empty keys are skipped (never fetched, never charged). Duplicate keys
// are de-duplicated. A non-positive `budget` still returns all cache hits
// (they cost nothing) but no new fetches.
LookupPlan planLookups(const std::vector<LookupKeys> &byPriority,
                       const std::function<bool(const std::string &)> &aircraftCached,
                       const std::function<bool(const std::string &)> &routeCached, int budget);
