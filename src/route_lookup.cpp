#include "route_lookup.h"

#include <map>

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
  info.found = !info.country_code.empty();
  return info;
}

namespace {
std::map<std::string, RouteInfo> routeCache;
std::map<std::string, AirportInfo> airportCache;
}  // namespace

RouteInfo lookupRouteWithFetcher(const std::string &callsign, RouteFetchFn fetch,
                                  bool *wasCacheHit) {
  auto it = routeCache.find(callsign);
  if (it != routeCache.end()) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return it->second;
  }
  if (wasCacheHit != nullptr) *wasCacheHit = false;

  HexdbFetchResult fetchResult = fetch(callsign);
  if (!fetchResult.response_ok) {
    return RouteInfo{};  // transient failure — not cached, see header comment
  }

  RouteInfo info = parseRouteLookupResponse(fetchResult.body);
  routeCache[callsign] = info;  // caches both real hits and confirmed misses
  return info;
}

AirportInfo lookupAirportWithFetcher(const std::string &airport_code, AirportFetchFn fetch,
                                      bool *wasCacheHit) {
  auto it = airportCache.find(airport_code);
  if (it != airportCache.end()) {
    if (wasCacheHit != nullptr) *wasCacheHit = true;
    return it->second;
  }
  if (wasCacheHit != nullptr) *wasCacheHit = false;

  HexdbFetchResult fetchResult = fetch(airport_code);
  if (!fetchResult.response_ok) {
    return AirportInfo{};
  }

  AirportInfo info = parseAirportLookupResponse(fetchResult.body);
  airportCache[airport_code] = info;
  return info;
}

bool routeLookupIsCached(const std::string &callsign) {
  return routeCache.find(callsign) != routeCache.end();
}

void routeLookupClearCacheForTesting() { routeCache.clear(); }
void airportLookupClearCacheForTesting() { airportCache.clear(); }

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>

namespace {

// See CLAUDE.md review notes 1.1/5.1 — bounds how long a single hexdb.io
// lookup can block loop().
constexpr int32_t kHttpConnectTimeoutMs = 5000;
constexpr uint16_t kHttpTimeoutMs = 8000;

// Both hexdb.io endpoints used here return a genuine HTTP 404 for "not
// found" (verified against the live API) — that's just as confirmed an
// answer as a 200, so both count as a definitive response worth caching.
bool isDefinitiveResponse(int code) { return code == HTTP_CODE_OK || code == HTTP_CODE_NOT_FOUND; }

HexdbFetchResult httpGet(const char *url) {
  HexdbFetchResult result;

  WiFiClientSecure client;
  // No CA bundle pinned — same rationale as aircraft_lookup/opensky_client:
  // acceptable for a hobby device, not hardened.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return result;
  }

  int code = http.GET();
  if (isDefinitiveResponse(code)) {
    result.response_ok = true;
    result.body = std::string(http.getString().c_str());  // may be empty for a 404
  } else {
    Serial.printf("[route_lookup] request failed for %s (http=%d)\n", url, code);
  }
  http.end();
  return result;
}

HexdbFetchResult httpFetchRoute(const std::string &callsign) {
  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see CLAUDE.md review notes 4.2) — callsigns are at most 8 chars.
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

RouteInfo lookupRoute(const std::string &callsign) {
  return lookupRouteWithFetcher(callsign, httpFetchRoute);
}

AirportInfo lookupAirportCountry(const std::string &icao_code) {
  return lookupAirportWithFetcher(icao_code, httpFetchAirport);
}

#endif  // ARDUINO
