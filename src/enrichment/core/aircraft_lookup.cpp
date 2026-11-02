#include "enrichment/core/aircraft_lookup.h"

#include "enrichment/core/lookup_cache.h"

#include <ArduinoJson.h>

AircraftInfo::AircraftInfo() = default;

AircraftInfo parseAircraftLookupResponse(const std::string &json) {
  AircraftInfo info;

  if (json.size() > kMaxLookupResponseBytes) {
    return info;  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return info;  // malformed/empty body -> found=false
  }

  if (doc["RegisteredOwners"].isNull()) {
    return info;  // hexdb.io's "not found" body, or any response missing this field
  }

  info.airline = doc["RegisteredOwners"].as<std::string>();
  info.aircraft_type = doc["Type"].isNull() ? std::string() : doc["Type"].as<std::string>();
  info.icao_type =
      doc["ICAOTypeCode"].isNull() ? std::string() : doc["ICAOTypeCode"].as<std::string>();
  info.registration =
      doc["Registration"].isNull() ? std::string() : doc["Registration"].as<std::string>();
  info.found = true;
  return info;
}

namespace {
LookupCache<AircraftInfo> cache(kAircraftCacheCapacity);
}  // namespace

AircraftInfo lookupAircraftWithFetcher(const std::string &icao24, AircraftFetchFn fetch,
                                        bool *wasCacheHit) {
  if (const AircraftInfo *cached = cache.find(icao24)) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return *cached;
  }

  if (wasCacheHit != nullptr) *wasCacheHit = false;

  FetchResult fetchResult = fetch(icao24);
  if (!fetchResult.response_ok) {
    // Transient failure (timeout, WiFi drop, hexdb.io unreachable, non-200
    // status) — do NOT cache. A confirmed answer (hit or genuine miss) is
    // what's safe to remember forever; a failed attempt isn't one, so a
    // later poll gets to try again — see docs/review-notes-2026.md 1.2.
    return AircraftInfo{};
  }

  AircraftInfo info = parseAircraftLookupResponse(fetchResult.body);
  cache.put(icao24, info);  // cache both real hits and confirmed misses
  return info;
}

bool aircraftLookupIsCached(const std::string &icao24) {
  return cache.contains(icao24);
}

void aircraftLookupSeedCache(const std::string &icao24, const AircraftInfo &info) {
  cache.put(icao24, info);
}

size_t aircraftLookupCacheSize() { return cache.size(); }

void aircraftLookupClearCacheForTesting() { cache.clear(); }
