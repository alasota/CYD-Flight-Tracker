#include "aircraft_lookup.h"

#include <map>

#include <ArduinoJson.h>

AircraftInfo parseAircraftLookupResponse(const std::string &json) {
  AircraftInfo info;

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

  std::string body = fetch(icao24);
  AircraftInfo info = parseAircraftLookupResponse(body);

  cache[icao24] = info;  // cache both hits and misses — see header comment
  return info;
}

void aircraftLookupClearCacheForTesting() { cache.clear(); }

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

std::string httpFetchAircraftJson(const std::string &icao24) {
  WiFiClientSecure client;
  // No CA bundle pinned — same rationale as opensky_client: acceptable for
  // a hobby device, not hardened.
  client.setInsecure();

  HTTPClient http;
  String url = String("https://hexdb.io/api/v1/aircraft/") + String(icao24.c_str());
  if (!http.begin(client, url)) {
    return "";
  }

  int code = http.GET();
  std::string body;
  if (code > 0) {
    body = std::string(http.getString().c_str());
  } else {
    Serial.printf("[aircraft_lookup] hexdb.io request failed for %s (http=%d)\n", icao24.c_str(),
                  code);
  }
  http.end();
  return body;
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
