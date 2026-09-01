// route_lookup — flight-route (origin/destination airport) and
// airport-country enrichment via hexdb.io. Mirrors aircraft_lookup's
// design: an in-RAM cache per key so a given callsign/airport code is only
// ever looked up once. See CLAUDE.md "Aircraft identification" (route
// lookup listed there as a nice-to-have) and hexdb.io's documented REST
// API (https://hexdb.io/). No TFT/drawing code, no OpenSky polling logic.
//
// NOTE: CLAUDE.md's own text names hexdb.io's route endpoint as
// "route/icao/{callsign}" — that's what's implemented here (not
// "route/iata/"), since OpenSky callsigns (e.g. "DLH9LH") are ICAO-style
// and hexdb.io's /iata/ route variant expects an IATA-style callsign this
// project never has.
#pragma once

#include <string>

// Origin/destination airport for a flight, by callsign. hexdb.io's
// GET /api/v1/route/icao/{callsign} returns a single "route" string like
// "EIDW-EGLL" — both ends are ICAO (4-letter) airport codes, matching the
// callsign format used to look it up.
struct RouteInfo {
  bool found = false;
  std::string origin_icao;
  std::string dest_icao;
};

// Hard ceiling on how large a route-lookup response body
// parseRouteLookupResponse() will attempt to parse — a real response is a
// few dozen bytes (same defensive-cap pattern as aircraft_lookup — see
// CLAUDE.md review notes 4.3).
constexpr size_t kMaxRouteResponseBytes = 4096;

// Parses hexdb.io's GET /api/v1/route/icao/{callsign} response body, e.g.
// {"flight":"EIN17A","route":"EIDW-EGLL","updatetime":1397991739}, into a
// RouteInfo by splitting "route" on its '-' separator. A "not found"
// response, a "route" value with no separator (or empty on either side),
// or malformed/empty/oversized JSON all parse to found=false rather than
// erroring. Pure — uses ArduinoJson but no HTTP dependency — tested under
// `pio test -e native`.
RouteInfo parseRouteLookupResponse(const std::string &json);

// Country (+ IATA code, for display) for an airport, by **ICAO** code —
// this project uses hexdb.io's GET /api/v1/airport/icao/{code} endpoint,
// not /iata/, so it chains directly off RouteInfo::origin_icao/dest_icao
// above without needing an ICAO->IATA conversion this project doesn't
// have. hexdb.io's airport response carries both codes, so the IATA one
// is still available for display (e.g. featured_panel's "WAW (PL)").
struct AirportInfo {
  bool found = false;
  std::string iata_code;     // hexdb.io "iata", e.g. "LHR" — for display
  std::string country_code;  // hexdb.io "country_code", e.g. "GB"
};

// Same defensive size cap as kMaxRouteResponseBytes, for airport responses.
constexpr size_t kMaxAirportResponseBytes = 4096;

// Parses hexdb.io's GET /api/v1/airport/icao/{code} response body into an
// AirportInfo, reading "iata" and "country_code". A "not found" response,
// or malformed/empty/oversized JSON, parses to found=false. Pure.
AirportInfo parseAirportLookupResponse(const std::string &json);

// Result of one hexdb.io fetch attempt, shared shape for both lookups
// below. `response_ok` is true for any *definitive* answer from hexdb.io —
// a 200 (found) or a 404 (confirmed not found) both count, since either is
// safe to cache forever; anything else (timeout, 5xx, connection failure)
// is not a real answer and must not be cached — see CLAUDE.md review notes
// 1.2, applied here the same way it was retrofitted onto aircraft_lookup
// once the live API turned out to answer "not found" with a genuine 404
// rather than a 200-with-error-body.
struct HexdbFetchResult {
  bool response_ok = false;
  std::string body;  // may be empty for a 404
};

using RouteFetchFn = HexdbFetchResult (*)(const std::string &callsign);
using AirportFetchFn = HexdbFetchResult (*)(const std::string &airport_code);

// Core lookup logic for routes: consults (and fills) an in-RAM cache keyed
// by callsign first, so a given callsign is only ever fetched via `fetch`
// once — same "be a good citizen towards hexdb.io" reasoning as
// aircraft_lookup. A confirmed response (found or genuine miss) is cached;
// a failed fetch attempt is not, so a later call can retry. Pure aside
// from the injected `fetch` call — tested under `pio test -e native` with
// a stub.
RouteInfo lookupRouteWithFetcher(const std::string &callsign, RouteFetchFn fetch,
                                  bool *wasCacheHit = nullptr);

// True if `callsign` already has a cached route result (hit or confirmed
// miss) — lets a caller bound how many *new* route lookups it attempts in
// one go, same reasoning as aircraft_lookup's aircraftLookupIsCached() (see
// CLAUDE.md review notes 1.1). Pure — just a cache query.
bool routeLookupIsCached(const std::string &callsign);

// Same idea, keyed by (ICAO) airport code.
AirportInfo lookupAirportWithFetcher(const std::string &airport_code, AirportFetchFn fetch,
                                      bool *wasCacheHit = nullptr);

// Clears both in-RAM caches. Exposed for tests; production code has no
// real reason to call this.
void routeLookupClearCacheForTesting();
void airportLookupClearCacheForTesting();

// --- Hardware adapters: the real hexdb.io GET requests, i.e. the
// lookup*WithFetcher() functions above wired to actual HTTP fetches. Thin
// wrappers around WiFiClientSecure/HTTPClient — not covered by Unity (see
// CLAUDE.md "Testing"). These are what production callers use.
RouteInfo lookupRoute(const std::string &callsign);
AirportInfo lookupAirportCountry(const std::string &icao_code);
