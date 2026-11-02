// hexdb_sources — the device half of aircraft_lookup and route_lookup: the
// hexdb.io GETs (through hexdb_http's shared session) wired into the pure
// lookup*WithFetcher() functions, with the NVS copy (lookup_store) checked
// before hexdb.io and a fresh confirmed answer persisted. Hardware adapter,
// not covered by Unity (CLAUDE.md "Testing").
#include <Arduino.h>
#include <cstdio>

#include "enrichment/adapters/hexdb_http.h"  // hexdbGet() — shared hexdb.io session (keep-alive)
#include "enrichment/core/aircraft_lookup.h"
#include "enrichment/core/lookup_store.h"    // NVS-persisted cache (storeLoad*/storeSave*)
#include "enrichment/core/route_lookup.h"

// ---- Aircraft (icao24 -> airline / type / registration) ---------------------

namespace {

FetchResult httpFetchAircraftJson(const std::string &icao24) {
  FetchResult result;

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see docs/review-notes-2026.md 4.2) — icao24 is always exactly 6 hex
  // chars, so this buffer has generous headroom.
  char url[64];
  int written = std::snprintf(url, sizeof(url), "https://hexdb.io/api/v1/aircraft/%s",
                               icao24.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return result;
  }

  const HexdbHttpResult r = hexdbGet(url);
  // hexdb.io's aircraft endpoint returns a genuine HTTP 404 for a missing
  // aircraft (verified against the live API) — that's just as definitive
  // an answer as a 200, and must be cached the same way (see
  // lookupAircraftWithFetcher()'s header comment); a 404 is NOT a
  // transient failure. Anything else (timeout, 5xx, connection failure)
  // is not a confirmed answer and correctly falls through uncached.
  if (r.code == 200 || r.code == 404) {
    result.response_ok = true;
    result.body = r.body;
  } else {
    Serial.printf("[aircraft_lookup] hexdb.io request failed for %s (http=%d)\n", icao24.c_str(),
                  r.code);
  }
  return result;
}

}  // namespace

AircraftInfo lookupAircraft(const std::string &icao24) {
  // RAM cache miss -> try the copy persisted in NVS before asking hexdb.io.
  if (!aircraftLookupIsCached(icao24)) {
    AircraftInfo stored;
    if (storeLoadAircraft(icao24, stored)) aircraftLookupSeedCache(icao24, stored);
  }

  bool wasCacheHit = false;
  AircraftInfo info = lookupAircraftWithFetcher(icao24, httpFetchAircraftJson, &wasCacheHit);

  // A fresh, confirmed hexdb.io answer (hit or definite miss) -> persist it.
  if (!wasCacheHit && aircraftLookupIsCached(icao24)) storeSaveAircraft(icao24, info);

  Serial.printf("[aircraft_lookup] %s: %s (found=%s, airline=%s, type=%s)\n", icao24.c_str(),
                wasCacheHit ? "cache hit" : "HTTP request", info.found ? "true" : "false",
                info.airline.c_str(), info.aircraft_type.c_str());

  return info;
}

// ---- Routes and airports ------------------------------------------------------

namespace {

// Both hexdb.io endpoints used here return a genuine HTTP 404 for "not
// found" (verified against the live API) — that's just as confirmed an
// answer as a 200, so both count as a definitive response worth caching.
bool isDefinitiveResponse(int code) { return code == 200 || code == 404; }

HexdbFetchResult httpGet(const char *url) {
  HexdbFetchResult result;
  const HexdbHttpResult r = hexdbGet(url);
  if (isDefinitiveResponse(r.code)) {
    result.response_ok = true;
    result.body = r.body;  // may be empty for a 404
  } else {
    Serial.printf("[route_lookup] request failed for %s (http=%d)\n", url, r.code);
  }
  return result;
}

HexdbFetchResult httpFetchRoute(const std::string &callsign) {
  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see docs/review-notes-2026.md 4.2) — callsigns are at most 8 chars.
  char url[96];
  int written =
      std::snprintf(url, sizeof(url), "https://hexdb.io/api/v1/route/icao/%s", callsign.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return HexdbFetchResult{};
  }
  return httpGet(url);
}

HexdbFetchResult httpFetchAirport(const std::string &icao_code) {
  char url[96];
  int written = std::snprintf(url, sizeof(url), "https://hexdb.io/api/v1/airport/icao/%s",
                               icao_code.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return HexdbFetchResult{};
  }
  return httpGet(url);
}

}  // namespace

// Both: RAM cache miss -> try NVS (lookup_store) before hexdb.io; persist a
// fresh confirmed answer.
RouteInfo lookupRoute(const std::string &callsign) {
  if (!routeLookupIsCached(callsign)) {
    RouteInfo stored;
    if (storeLoadRoute(callsign, stored)) routeLookupSeedCache(callsign, stored);
  }
  bool wasCacheHit = false;
  RouteInfo info = lookupRouteWithFetcher(callsign, httpFetchRoute, &wasCacheHit);
  if (!wasCacheHit && routeLookupIsCached(callsign)) storeSaveRoute(callsign, info);
  return info;
}

AirportInfo lookupAirportCountry(const std::string &icao_code) {
  if (!airportLookupIsCached(icao_code)) {
    AirportInfo stored;
    if (storeLoadAirport(icao_code, stored)) airportLookupSeedCache(icao_code, stored);
  }
  bool wasCacheHit = false;
  AirportInfo info = lookupAirportWithFetcher(icao_code, httpFetchAirport, &wasCacheHit);
  if (!wasCacheHit && airportLookupIsCached(icao_code)) storeSaveAirport(icao_code, info);
  return info;
}
