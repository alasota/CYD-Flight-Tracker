// aircraft_lookup — icao24 -> airline/aircraft_type/registration enrichment
// via hexdb.io, with an in-RAM cache so a given icao24 is only ever looked
// up once. See CLAUDE.md "Aircraft identification: airline + flight +
// model". No TFT/drawing code, no OpenSky polling logic — sits purely
// between opensky_client's output and the view modules, and takes only a
// plain icao24 string as input (no dependency on opensky_client itself).
#pragma once

#include <string>

struct AircraftInfo {
  bool found = false;
  std::string airline;        // hexdb.io "RegisteredOwners"
  std::string aircraft_type;  // hexdb.io "Type"
  std::string registration;   // hexdb.io "Registration"
};

// Hard ceiling on how large a hexdb.io response body
// parseAircraftLookupResponse() will attempt to parse. A real response is
// a few hundred bytes; this is a defensive cap against an unexpectedly
// huge or corrupted body (see CLAUDE.md review notes 4.3).
constexpr size_t kMaxLookupResponseBytes = 4096;

// Parses a hexdb.io GET /api/v1/aircraft/{icao24} JSON response body into
// an AircraftInfo. A "not found" response (hexdb.io's 404-style body, e.g.
// {"status":"404","error":"Aircraft not found."}) — or any response
// missing "RegisteredOwners" — parses to found=false rather than erroring;
// malformed/empty JSON does too, and a body over kMaxLookupResponseBytes
// is rejected outright without being parsed. Pure — uses ArduinoJson but
// no HTTP dependency — tested under `pio test -e native`.
AircraftInfo parseAircraftLookupResponse(const std::string &json);

// Result of one fetch attempt, used by lookupAircraftWithFetcher() below.
// `response_ok` distinguishes "we got a definitive HTTP 200 back" from any
// other outcome (timeout, WiFi drop, DNS failure, non-200 status) — see
// CLAUDE.md review notes 1.2: only a confirmed response is safe to cache
// forever, since caching a *transient* failure the same way as a genuine
// hexdb.io miss would permanently blacklist an aircraft that simply had
// bad luck on its first lookup.
struct FetchResult {
  bool response_ok = false;
  std::string body;
};

// Injectable HTTP fetch used by lookupAircraftWithFetcher() below — takes
// an icao24, returns a FetchResult.
using AircraftFetchFn = FetchResult (*)(const std::string &icao24);

// Core lookup logic: consults (and fills) an in-RAM cache first so a given
// icao24 is only ever fetched via `fetch` once per boot — see CLAUDE.md
// "be a good citizen" towards hexdb.io's free service. A confirmed
// response (fetch()'s response_ok == true) is cached whether it's a real
// hit or a genuine hexdb.io miss — a miss (military/private/very new
// registration) won't start resolving mid-boot either, so there's no
// reason to keep re-fetching it. A *failed* fetch attempt (response_ok ==
// false) is deliberately NOT cached — see CLAUDE.md review notes 1.2 — so
// a later poll can retry once conditions improve. Pure aside from the
// injected `fetch` call — no direct HTTP/WiFi dependency — tested under
// `pio test -e native` with a stub.
//
// TODO: persist the cache to NVS (config_store-style Preferences) so it
// survives a reboot too, not just the current session. RAM-only is enough
// for now — see CLAUDE.md "Testing": don't complicate a step beyond what
// it needs.
//
// `wasCacheHit`, if non-null, is set to true/false so a caller (e.g. the
// lookupAircraft() adapter below) can log whether this call served from
// cache or made a real HTTP request.
AircraftInfo lookupAircraftWithFetcher(const std::string &icao24, AircraftFetchFn fetch,
                                        bool *wasCacheHit = nullptr);

// True if `icao24` already has a cached result (hit or confirmed miss) —
// i.e. calling lookupAircraft()/lookupAircraftWithFetcher() for it would
// NOT perform an HTTP request. Lets a caller bound how many *new* lookups
// it attempts in one go (each is a blocking HTTP round trip) without
// skipping ones that are already free — see CLAUDE.md review notes 1.1,
// and main.cpp's per-poll lookup budget. Pure — just a cache query.
bool aircraftLookupIsCached(const std::string &icao24);

// Clears the in-RAM cache. Exposed for tests; production code has no real
// reason to call this (an icao24's airline/type/registration essentially
// never changes).
void aircraftLookupClearCacheForTesting();

// --- Hardware adapter: the real hexdb.io GET request, i.e.
// lookupAircraftWithFetcher() wired to an actual HTTP fetch. Thin wrapper
// around WiFiClientSecure/HTTPClient — not covered by Unity (see CLAUDE.md
// "Testing"). This is what production callers use.
AircraftInfo lookupAircraft(const std::string &icao24);
