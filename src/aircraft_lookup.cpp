#include "aircraft_lookup.h"

#include <map>

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
  info.registration =
      doc["Registration"].isNull() ? std::string() : doc["Registration"].as<std::string>();
  info.found = true;
  return info;
}

namespace {
std::map<std::string, AircraftInfo> cache;
}  // namespace

AircraftInfo lookupAircraftWithFetcher(const std::string &icao24, AircraftFetchFn fetch,
                                        bool *wasCacheHit) {
  auto it = cache.find(icao24);
  if (it != cache.end()) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return it->second;
  }

  if (wasCacheHit != nullptr) *wasCacheHit = false;

  FetchResult fetchResult = fetch(icao24);
  if (!fetchResult.response_ok) {
    // Transient failure (timeout, WiFi drop, hexdb.io unreachable, non-200
    // status) — do NOT cache. A confirmed answer (hit or genuine miss) is
    // what's safe to remember forever; a failed attempt isn't one, so a
    // later poll gets to try again — see CLAUDE.md review notes 1.2.
    return AircraftInfo{};
  }

  AircraftInfo info = parseAircraftLookupResponse(fetchResult.body);
  cache[icao24] = info;  // cache both real hits and confirmed misses
  return info;
}

bool aircraftLookupIsCached(const std::string &icao24) {
  return cache.find(icao24) != cache.end();
}

void aircraftLookupClearCacheForTesting() { cache.clear(); }

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>

#include "net_config.h"  // shared HTTP timeouts (review notes 5.1)

namespace {

FetchResult httpFetchAircraftJson(const std::string &icao24) {
  FetchResult result;

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see CLAUDE.md review notes 4.2) — icao24 is always exactly 6 hex
  // chars, so this buffer has generous headroom.
  char url[64];
  int written = std::snprintf(url, sizeof(url), "https://hexdb.io/api/v1/aircraft/%s",
                               icao24.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return result;
  }

  WiFiClientSecure client;
  // No CA bundle pinned — same rationale as opensky_client: acceptable for
  // a hobby device, not hardened.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kHexdbHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return result;
  }

  int code = http.GET();
  // hexdb.io's aircraft endpoint returns a genuine HTTP 404 for a missing
  // aircraft (verified against the live API) — that's just as definitive
  // an answer as a 200, and must be cached the same way (see
  // lookupAircraftWithFetcher()'s header comment); a 404 is NOT a
  // transient failure. Anything else (timeout, 5xx, connection failure)
  // is not a confirmed answer and correctly falls through uncached.
  if (code == HTTP_CODE_OK || code == HTTP_CODE_NOT_FOUND) {
    result.response_ok = true;
    result.body = std::string(http.getString().c_str());
  } else {
    Serial.printf("[aircraft_lookup] hexdb.io request failed for %s (http=%d)\n", icao24.c_str(),
                  code);
  }
  http.end();
  return result;
}

}  // namespace

AircraftInfo lookupAircraft(const std::string &icao24) {
  bool wasCacheHit = false;
  AircraftInfo info = lookupAircraftWithFetcher(icao24, httpFetchAircraftJson, &wasCacheHit);

  Serial.printf("[aircraft_lookup] %s: %s (found=%s, airline=%s, type=%s)\n", icao24.c_str(),
                wasCacheHit ? "cache hit" : "HTTP request", info.found ? "true" : "false",
                info.airline.c_str(), info.aircraft_type.c_str());

  return info;
}

#endif  // ARDUINO
