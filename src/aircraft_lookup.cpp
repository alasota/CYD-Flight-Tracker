#include "aircraft_lookup.h"

#include "lookup_cache.h"

#include <ArduinoJson.h>

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

#ifdef ARDUINO

#include <Arduino.h>
#include <cstdio>

#include "hexdb_http.h"    // hexdbGet() — shared hexdb.io session (keep-alive)
#include "lookup_store.h"  // NVS-persisted cache (storeLoad*/storeSave*)

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

#endif  // ARDUINO
