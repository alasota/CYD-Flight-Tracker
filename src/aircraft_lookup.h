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

// Parses a hexdb.io GET /api/v1/aircraft/{icao24} JSON response body into
// an AircraftInfo. A "not found" response (hexdb.io's 404-style body, e.g.
// {"status":"404","error":"Aircraft not found."}) — or any response
// missing "RegisteredOwners" — parses to found=false rather than erroring;
// malformed/empty JSON does too. Pure — uses ArduinoJson but no HTTP
// dependency — tested under `pio test -e native`.
AircraftInfo parseAircraftLookupResponse(const std::string &json);

// Injectable HTTP fetch used by lookupAircraftWithFetcher() below — takes
// an icao24, returns the raw response body ("" on any transport failure,
// which parses to found=false same as a real miss).
using AircraftFetchFn = std::string (*)(const std::string &icao24);

// Core lookup logic: consults (and fills) an in-RAM cache first so a given
// icao24 is only ever fetched via `fetch` once per boot — see CLAUDE.md
// "be a good citizen" towards hexdb.io's free service. Both hits and
// misses are cached: a miss (military/private/very new registration)
// won't start resolving mid-boot either, so there's no reason to keep
// re-fetching it. Pure aside from the injected `fetch` call — no direct
// HTTP/WiFi dependency — tested under `pio test -e native` with a stub.
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

// Clears the in-RAM cache. Exposed for tests; production code has no real
// reason to call this (an icao24's airline/type/registration essentially
// never changes).
void aircraftLookupClearCacheForTesting();

// --- Hardware adapter: the real hexdb.io GET request, i.e.
// lookupAircraftWithFetcher() wired to an actual HTTP fetch. Thin wrapper
// around WiFiClientSecure/HTTPClient — not covered by Unity (see CLAUDE.md
// "Testing"). This is what production callers use.
AircraftInfo lookupAircraft(const std::string &icao24);
