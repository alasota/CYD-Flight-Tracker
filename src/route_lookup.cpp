#include "route_lookup.h"

#include "lookup_cache.h"
#include "place_names.h"  // asciiFold()

#include <ArduinoJson.h>

RouteInfo parseRouteLookupResponse(const std::string &json) {
  RouteInfo info;

  if (json.size() > kMaxRouteResponseBytes) {
    return info;  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return info;  // malformed/empty body -> found=false
  }
  if (doc["route"].isNull()) {
    return info;  // hexdb.io's "not found" body, or any response missing this field
  }

  std::string route = doc["route"].as<std::string>();
  size_t dash = route.find('-');
  if (dash == std::string::npos || dash == 0 || dash == route.size() - 1) {
    return info;  // malformed "route" value: no separator, or empty on either side
  }

  info.origin_icao = route.substr(0, dash);
  info.dest_icao = route.substr(dash + 1);
  info.found = true;
  return info;
}

AirportInfo parseAirportLookupResponse(const std::string &json) {
  AirportInfo info;

  if (json.size() > kMaxAirportResponseBytes) {
    return info;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return info;
  }
  if (doc["country_code"].isNull()) {
    return info;
  }

  info.country_code = doc["country_code"].as<std::string>();
  info.iata_code = doc["iata"].isNull() ? std::string() : doc["iata"].as<std::string>();
  // Folded to ASCII once here (the bitmap fonts only cover 0x20-0x7E), so the
  // cache and every screen get a drawable string.
  info.name = doc["airport"].isNull() ? std::string() : asciiFold(doc["airport"].as<std::string>());
  info.found = !info.country_code.empty();
  return info;
}

namespace {
LookupCache<RouteInfo> routeCache(kRouteCacheCapacity);
LookupCache<AirportInfo> airportCache(kAirportCacheCapacity);
}  // namespace

RouteInfo lookupRouteWithFetcher(const std::string &callsign, RouteFetchFn fetch,
                                  bool *wasCacheHit) {
  if (const RouteInfo *cached = routeCache.find(callsign)) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return *cached;
  }
  if (wasCacheHit != nullptr) *wasCacheHit = false;

  HexdbFetchResult fetchResult = fetch(callsign);
  if (!fetchResult.response_ok) {
    return RouteInfo{};  // transient failure — not cached, see header comment
  }

  RouteInfo info = parseRouteLookupResponse(fetchResult.body);
  routeCache.put(callsign, info);  // caches both real hits and confirmed misses
  return info;
}

AirportInfo lookupAirportWithFetcher(const std::string &airport_code, AirportFetchFn fetch,
                                      bool *wasCacheHit) {
  if (const AirportInfo *cached = airportCache.find(airport_code)) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return *cached;
  }
  if (wasCacheHit != nullptr) *wasCacheHit = false;

  HexdbFetchResult fetchResult = fetch(airport_code);
  if (!fetchResult.response_ok) {
    return AirportInfo{};
  }

  AirportInfo info = parseAirportLookupResponse(fetchResult.body);
  airportCache.put(airport_code, info);
  return info;
}

bool routeLookupIsCached(const std::string &callsign) {
  return routeCache.contains(callsign);
}

bool airportLookupIsCached(const std::string &airport_code) {
  return airportCache.contains(airport_code);
}

void routeLookupSeedCache(const std::string &callsign, const RouteInfo &info) {
  routeCache.put(callsign, info);
}

void airportLookupSeedCache(const std::string &airport_code, const AirportInfo &info) {
  airportCache.put(airport_code, info);
}

size_t routeLookupCacheSize() { return routeCache.size(); }
size_t airportLookupCacheSize() { return airportCache.size(); }

void routeLookupClearCacheForTesting() { routeCache.clear(); }
void airportLookupClearCacheForTesting() { airportCache.clear(); }

#ifdef ARDUINO

#include <Arduino.h>
#include <cstdio>

#include "hexdb_http.h"    // hexdbGet() — shared hexdb.io session (keep-alive)
#include "lookup_store.h"  // NVS-persisted cache (storeLoad*/storeSave*)

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

#endif  // ARDUINO
