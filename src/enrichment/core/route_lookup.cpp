#include "enrichment/core/route_lookup.h"

#include "enrichment/core/lookup_cache.h"
#include "enrichment/core/place_names.h"  // asciiFold()

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
