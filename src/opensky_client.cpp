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

bool isBeforeDeadline(uint32_t now_ms, uint32_t until_ms) {
  // (until_ms - now_ms), reinterpreted as signed, is positive exactly when
  // now_ms precedes until_ms in wraparound-correct millis() time — same
  // trick tokenNeedsRefresh() uses.
  return static_cast<int32_t>(until_ms - now_ms) > 0;
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

  if (json.size() > kMaxStatesResponseBytes) {
    return result;  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return result;
  }

  // doc["states"] being missing/null yields an empty JsonArrayConst here
  // (ArduinoJson), so a response with no aircraft in range just produces
  // an empty result rather than needing a separate null check.
  for (JsonArrayConst state : doc["states"].as<JsonArrayConst>()) {
    // Indices per OpenSky's /states/all state vector: 0 icao24, 1 callsign,
    // 5 longitude, 6 latitude, 7 baro_altitude, 8 on_ground, 9 velocity,
    // 10 true_track, 11 vertical_rate.
    if (state.size() < 12) continue;

    Aircraft ac;
    if (!state[0].isNull()) ac.icao24 = state[0].as<std::string>();
    if (!state[1].isNull()) ac.callsign = trimTrailingSpaces(state[1].as<std::string>());
    if (!state[5].isNull() && !state[6].isNull()) {
      ac.lon = state[5].as<float>();
      ac.lat = state[6].as<float>();
      ac.has_position = true;
    }
    if (!state[7].isNull()) ac.baro_altitude = state[7].as<float>();
    if (!state[8].isNull()) ac.on_ground = state[8].as<bool>();
    if (!state[9].isNull()) ac.velocity = state[9].as<float>();
    if (!state[10].isNull()) ac.true_track = state[10].as<float>();
    if (!state[11].isNull()) ac.vertical_rate = state[11].as<float>();

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

#include <cstdio>

#include "config_store.h"

namespace {

constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";
constexpr char kRetryAfterHeader[] = "X-Rate-Limit-Retry-After-Seconds";
constexpr uint32_t kDefaultRetryAfterS = 60;

// Bounds on a single blocking HTTP call (see CLAUDE.md review notes 1.1 /
// 5.1) — without these, loop() could stall for however long HTTPClient's
// own default happens to be, which is neither documented here nor tuned
// for staying responsive to touch/config_portal.
constexpr int32_t kHttpConnectTimeoutMs = 5000;
constexpr uint16_t kHttpTimeoutMs = 8000;

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
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, kTokenUrl)) {
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see CLAUDE.md review notes 4.2) — client_id/secret are short OAuth
  // client identifiers, so this buffer has generous headroom.
  std::string encodedId = urlEncode(client_id);
  std::string encodedSecret = urlEncode(client_secret);
  char body[512];
  int written =
      std::snprintf(body, sizeof(body), "grant_type=client_credentials&client_id=%s&client_secret=%s",
                    encodedId.c_str(), encodedSecret.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
    Serial.println("[opensky_client] client_id/client_secret too long for token request buffer");
    http.end();
    return false;
  }

  int code = http.POST(reinterpret_cast<uint8_t *>(body), static_cast<size_t>(written));
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
  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see CLAUDE.md review notes 4.2).
  char url[192];
  int urlWritten = std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
                                  kStatesUrlBase, static_cast<double>(bbox.lamin),
                                  static_cast<double>(bbox.lomin), static_cast<double>(bbox.lamax),
                                  static_cast<double>(bbox.lomax));
  if (urlWritten < 0 || static_cast<size_t>(urlWritten) >= sizeof(url)) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  if (!bearerToken.empty()) {
    char authHeader[2048];
    int authWritten = std::snprintf(authHeader, sizeof(authHeader), "Bearer %s", bearerToken.c_str());
    if (authWritten >= 0 && static_cast<size_t>(authWritten) < sizeof(authHeader)) {
      http.addHeader("Authorization", authHeader);
    } else {
      Serial.println("[opensky_client] bearer token too long for header buffer, sending unauthenticated");
    }
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
  if (rateLimitedUntilMs != 0 && isBeforeDeadline(now, rateLimitedUntilMs)) {
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
    uint32_t backoff_s = parseRetryAfterSeconds(retryAfter, kDefaultRetryAfterS);
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
