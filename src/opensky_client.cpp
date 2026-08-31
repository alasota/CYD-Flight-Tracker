#include "opensky_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <ArduinoJson.h>

namespace {
float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
}  // namespace

BoundingBox computeBoundingBox(float lat, float lon, float radius_deg) {
  BoundingBox bbox;
  bbox.lamin = clampf(lat - radius_deg, -90.0f, 90.0f);
  bbox.lamax = clampf(lat + radius_deg, -90.0f, 90.0f);
  bbox.lomin = clampf(lon - radius_deg, -180.0f, 180.0f);
  bbox.lomax = clampf(lon + radius_deg, -180.0f, 180.0f);
  return bbox;
}

bool tokenNeedsRefresh(const TokenState &token, uint32_t now_ms, uint32_t refresh_margin_s) {
  if (!token.valid) {
    return true;
  }
  // Unsigned subtraction wraps correctly around a millis() rollover.
  uint32_t elapsed_ms = now_ms - token.obtained_at_ms;
  uint32_t expires_in_ms = token.expires_in_s * 1000UL;
  uint32_t margin_ms = refresh_margin_s * 1000UL;
  return elapsed_ms + margin_ms >= expires_in_ms;
}

bool shouldUseOAuth(const std::string &client_id, const std::string &client_secret) {
  return !client_id.empty() && !client_secret.empty();
}

namespace {
std::string trimTrailingSpaces(const std::string &s) {
  size_t end = s.size();
  while (end > 0 && s[end - 1] == ' ') --end;
  return s.substr(0, end);
}
}  // namespace

std::vector<Aircraft> parseStatesResponse(const std::string &json) {
  std::vector<Aircraft> result;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return result;
  }

  // doc["states"] being missing/null yields an empty JsonArrayConst here
  // (ArduinoJson), so a response with no aircraft in range just produces
  // an empty result rather than needing a separate null check.
  for (JsonArrayConst state : doc["states"].as<JsonArrayConst>()) {
    // Indices per OpenSky's /states/all state vector: 0 icao24, 1 callsign,
    // 5 longitude, 6 latitude, 7 baro_altitude, 9 velocity, 10 true_track.
    if (state.size() < 11) continue;

    Aircraft ac;
    if (!state[0].isNull()) ac.icao24 = state[0].as<std::string>();
    if (!state[1].isNull()) ac.callsign = trimTrailingSpaces(state[1].as<std::string>());
    if (!state[5].isNull() && !state[6].isNull()) {
      ac.lon = state[5].as<float>();
      ac.lat = state[6].as<float>();
      ac.has_position = true;
    }
    if (!state[7].isNull()) ac.baro_altitude = state[7].as<float>();
    if (!state[9].isNull()) ac.velocity = state[9].as<float>();
    if (!state[10].isNull()) ac.true_track = state[10].as<float>();

    result.push_back(ac);
  }

  return result;
}

uint32_t parseRetryAfterSeconds(const std::string &header_value, uint32_t default_backoff_s) {
  if (header_value.empty()) {
    return default_backoff_s;
  }
  for (char c : header_value) {
    if (c < '0' || c > '9') {
      return default_backoff_s;  // not a plain non-negative integer
    }
  }
  long parsed = std::strtol(header_value.c_str(), nullptr, 10);
  if (parsed < 0) {
    return default_backoff_s;
  }
  return static_cast<uint32_t>(parsed);
}

std::string urlEncode(const std::string &value) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[(c >> 4) & 0xF];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config_store.h"

namespace {

constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";
constexpr char kRetryAfterHeader[] = "X-Rate-Limit-Retry-After-Seconds";

TokenState cachedToken;
uint32_t rateLimitedUntilMs = 0;
uint32_t lastPollMs = 0;
bool everPolled = false;

bool fetchToken(const std::string &client_id, const std::string &client_secret) {
  WiFiClientSecure client;
  // No CA bundle pinned — acceptable for a hobby device per CLAUDE.md's
  // scope; a future hardening pass could pin OpenSky/Keycloak's cert.
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, kTokenUrl)) {
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=client_credentials&client_id=" + String(urlEncode(client_id).c_str()) +
                "&client_secret=" + String(urlEncode(client_secret).c_str());

  int code = http.POST(body);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[opensky_client] token fetch failed (http=%d)\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok || doc["access_token"].isNull() ||
      doc["expires_in"].isNull()) {
    Serial.println("[opensky_client] token response missing access_token/expires_in");
    return false;
  }

  cachedToken.access_token = doc["access_token"].as<std::string>();
  cachedToken.expires_in_s = doc["expires_in"].as<uint32_t>();
  cachedToken.obtained_at_ms = millis();
  cachedToken.valid = true;
  return true;
}

// One /states/all attempt. Returns false only on a transport-level failure
// (no HTTP response at all); on true, *outCode carries the HTTP status.
bool requestStatesOnce(const BoundingBox &bbox, const std::string &bearerToken, int *outCode,
                        std::string *outBody, std::string *outRetryAfter) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(kStatesUrlBase) + "?lamin=" + String(bbox.lamin, 4) +
               "&lomin=" + String(bbox.lomin, 4) + "&lamax=" + String(bbox.lamax, 4) +
               "&lomax=" + String(bbox.lomax, 4);

  if (!http.begin(client, url)) {
    return false;
  }
  if (!bearerToken.empty()) {
    http.addHeader("Authorization", "Bearer " + String(bearerToken.c_str()));
  }
  const char *collectHeaders[] = {kRetryAfterHeader};
  http.collectHeaders(collectHeaders, 1);

  int code = http.GET();
  if (code <= 0) {
    http.end();
    return false;
  }

  *outCode = code;
  *outBody = std::string(http.getString().c_str());
  if (http.hasHeader(kRetryAfterHeader)) {
    *outRetryAfter = std::string(http.header(kRetryAfterHeader).c_str());
  }
  http.end();
  return true;
}

}  // namespace

std::vector<Aircraft> fetchAircraftStates(float home_lat, float home_lon, float radius_deg) {
  uint32_t now = millis();
  if (rateLimitedUntilMs != 0 && now < rateLimitedUntilMs) {
    return {};  // still backing off after a previous 429
  }

  Config cfg = loadConfig();
  bool useAuth = shouldUseOAuth(cfg.opensky_client_id, cfg.opensky_client_secret);

  if (useAuth && tokenNeedsRefresh(cachedToken, now)) {
    if (!fetchToken(cfg.opensky_client_id, cfg.opensky_client_secret)) {
      // Couldn't get a token this round; fall back to anonymous for this
      // poll rather than failing outright.
      useAuth = false;
    }
  }

  BoundingBox bbox = computeBoundingBox(home_lat, home_lon, radius_deg);

  int httpCode = 0;
  std::string body;
  std::string retryAfter;
  bool ok = requestStatesOnce(bbox, useAuth ? cachedToken.access_token : std::string(), &httpCode,
                               &body, &retryAfter);

  if (ok && httpCode == 401 && useAuth) {
    // Treat 401 as "refresh once and retry", not fatal — see CLAUDE.md.
    if (fetchToken(cfg.opensky_client_id, cfg.opensky_client_secret)) {
      ok = requestStatesOnce(bbox, cachedToken.access_token, &httpCode, &body, &retryAfter);
    }
  }

  if (!ok) {
    Serial.println("[opensky_client] /states/all request failed (no HTTP response)");
    return {};
  }

  if (httpCode == 429) {
    uint32_t backoff_s = parseRetryAfterSeconds(retryAfter, 60);
    rateLimitedUntilMs = now + backoff_s * 1000UL;
    Serial.printf("[opensky_client] rate limited (429), backing off %us\n",
                  static_cast<unsigned>(backoff_s));
    return {};
  }

  if (httpCode != 200) {
    Serial.printf("[opensky_client] /states/all failed (http=%d)\n", httpCode);
    return {};
  }

  return parseStatesResponse(body);
}

bool openSkyClientPoll(uint32_t now_ms, std::vector<Aircraft> *out) {
  Config cfg = loadConfig();
  uint32_t interval_ms = cfg.poll_interval_s * 1000UL;

  if (everPolled && (now_ms - lastPollMs) < interval_ms) {
    return false;
  }

  std::vector<Aircraft> aircraft = fetchAircraftStates(cfg.home_lat, cfg.home_lon, cfg.radius_deg);
  lastPollMs = now_ms;
  everPolled = true;

  if (out != nullptr) {
    *out = aircraft;
  }
  return true;
}

#endif  // ARDUINO
