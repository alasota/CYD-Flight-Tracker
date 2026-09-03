#include "opensky_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <ArduinoJson.h>

#include "net_config.h"

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

bool shouldPollNow(uint32_t now_ms, uint32_t last_poll_ms, bool ever_polled, uint32_t interval_ms) {
  if (!ever_polled) {
    return true;
  }
  return (now_ms - last_poll_ms) >= interval_ms;  // unsigned: rollover-safe
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

// Shared extraction: turn OpenSky's "states" array (an array of state
// vectors) into Aircraft records. Used by both the string-based
// parseStatesResponse() (tests) and the device's streaming parse path
// (fetchAircraftStates()), so the field mapping lives in exactly one place.
std::vector<Aircraft> extractAircraftFromStates(JsonArrayConst states) {
  std::vector<Aircraft> result;
  for (JsonArrayConst state : states) {
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
}  // namespace

std::vector<Aircraft> parseStatesResponse(const std::string &json) {
  if (json.size() > kMaxStatesResponseBytes) {
    return {};  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return {};
  }

  // doc["states"] being missing/null yields an empty JsonArrayConst here
  // (ArduinoJson), so a response with no aircraft in range just produces
  // an empty result rather than needing a separate null check.
  return extractAircraftFromStates(doc["states"].as<JsonArrayConst>());
}

uint32_t parseRetryAfterSeconds(const std::string &header_value, uint32_t default_backoff_s,
                                uint32_t max_backoff_s) {
  uint32_t seconds = default_backoff_s;
  if (!header_value.empty()) {
    bool allDigits = true;
    for (char c : header_value) {
      if (c < '0' || c > '9') {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      // strtoul saturates to ULONG_MAX on overflow rather than wrapping,
      // so an absurd header still ends up clamped below, not tiny.
      unsigned long parsed = std::strtoul(header_value.c_str(), nullptr, 10);
      seconds = parsed > max_backoff_s ? max_backoff_s : static_cast<uint32_t>(parsed);
    }
  }
  return seconds > max_backoff_s ? max_backoff_s : seconds;
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

// HTTP timeouts are shared across the networking modules — see net_config.h.

TokenState cachedToken;
uint32_t rateLimitedUntilMs = 0;
uint32_t lastPollMs = 0;
bool everPolled = false;

// One /states/all attempt, with the 200-response body parsed inline
// straight from the socket (streaming + an ArduinoJson field filter) so a
// big busy-airspace response never has to exist in RAM as one contiguous
// String + std::string + document all at once — see review notes 4.1.
struct StatesAttempt {
  bool transport_ok = false;  // got any HTTP response at all
  int code = 0;               // HTTP status when transport_ok
  bool body_ok = false;       // a 200 whose body parsed cleanly
  std::string retry_after;    // X-Rate-Limit-Retry-After-Seconds, if sent
  std::vector<Aircraft> aircraft;
};

bool fetchToken(const std::string &client_id, const std::string &client_secret) {
  WiFiClientSecure client;
  // No CA bundle pinned — acceptable for a hobby device per CLAUDE.md's
  // scope; a future hardening pass could pin OpenSky/Keycloak's cert.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kOpenSkyHttpTimeoutMs);
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

StatesAttempt requestStatesOnce(const BoundingBox &bbox, const std::string &bearerToken) {
  StatesAttempt att;

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see CLAUDE.md review notes 4.2).
  char url[192];
  int urlWritten = std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
                                  kStatesUrlBase, static_cast<double>(bbox.lamin),
                                  static_cast<double>(bbox.lomin), static_cast<double>(bbox.lamax),
                                  static_cast<double>(bbox.lomax));
  if (urlWritten < 0 || static_cast<size_t>(urlWritten) >= sizeof(url)) {
    return att;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kOpenSkyHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return att;
  }
  if (!bearerToken.empty()) {
    // std::string on the heap, not a 2KB stack buffer (review notes 4.2) —
    // a bearer JWT is ~1KB and loopTask's stack is only 8KB.
    std::string authHeader = "Bearer ";
    authHeader += bearerToken;
    http.addHeader("Authorization", authHeader.c_str());
  }
  const char *collectHeaders[] = {kRetryAfterHeader};
  http.collectHeaders(collectHeaders, 1);

  int code = http.GET();
  if (code <= 0) {
    http.end();
    return att;  // transport_ok stays false
  }

  att.transport_ok = true;
  att.code = code;
  if (http.hasHeader(kRetryAfterHeader)) {
    att.retry_after = std::string(http.header(kRetryAfterHeader).c_str());
  }

  if (code == HTTP_CODE_OK) {
    int len = http.getSize();  // -1 when chunked / unknown
    if (len > 0 && static_cast<size_t>(len) > kMaxStatesResponseBytes) {
      Serial.printf("[opensky_client] /states/all body too large (%d bytes) — skipping parse\n",
                    len);
    } else {
      // Filter: keep every element of each state vector, drop the rest of
      // the envelope. Parsed directly from the socket stream.
      JsonDocument filter;
      filter["states"][0] = true;
      JsonDocument doc;
      DeserializationError err =
          deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (err) {
        Serial.printf("[opensky_client] /states/all parse error: %s\n", err.c_str());
      } else {
        att.aircraft = extractAircraftFromStates(doc["states"].as<JsonArrayConst>());
        att.body_ok = true;
      }
    }
  }

  http.end();
  return att;
}

}  // namespace

OpenSkyPollResult fetchAircraftStates(const Config &cfg) {
  OpenSkyPollResult result;
  uint32_t now = millis();

  if (rateLimitedUntilMs != 0 && isBeforeDeadline(now, rateLimitedUntilMs)) {
    int32_t remain_ms = static_cast<int32_t>(rateLimitedUntilMs - now);
    result.status = OpenSkyPollStatus::RateLimited;
    result.retry_after_s = remain_ms > 0 ? static_cast<uint32_t>((remain_ms + 999) / 1000) : 0;
    return result;
  }

  bool useAuth = shouldUseOAuth(cfg.opensky_client_id, cfg.opensky_client_secret);

  if (useAuth && tokenNeedsRefresh(cachedToken, now)) {
    if (!fetchToken(cfg.opensky_client_id, cfg.opensky_client_secret)) {
      // Couldn't get a token this round; fall back to anonymous for this
      // poll rather than failing outright.
      useAuth = false;
    }
  }

  BoundingBox bbox = computeBoundingBox(cfg.home_lat, cfg.home_lon, cfg.radius_deg);

  StatesAttempt att =
      requestStatesOnce(bbox, useAuth ? cachedToken.access_token : std::string());

  if (att.transport_ok && att.code == 401 && useAuth) {
    // Treat a first 401 as "refresh once and retry", not fatal — see CLAUDE.md.
    if (fetchToken(cfg.opensky_client_id, cfg.opensky_client_secret)) {
      att = requestStatesOnce(bbox, cachedToken.access_token);
    }
  }

  if (!att.transport_ok) {
    Serial.println("[opensky_client] /states/all request failed (no HTTP response)");
    result.status = OpenSkyPollStatus::NetworkError;
    return result;
  }

  if (att.code == 429) {
    uint32_t backoff_s = parseRetryAfterSeconds(att.retry_after, kDefaultRetryAfterS);
    rateLimitedUntilMs = now + backoff_s * 1000UL;
    Serial.printf("[opensky_client] rate limited (429), backing off %us\n",
                  static_cast<unsigned>(backoff_s));
    result.status = OpenSkyPollStatus::RateLimited;
    result.retry_after_s = backoff_s;
    return result;
  }

  if (att.code == 401 || att.code == 403) {
    // Still rejected after a fresh token — the credentials are wrong, not a
    // transient blip. Surface it instead of silently going empty forever
    // (review notes 1.3).
    Serial.printf("[opensky_client] auth rejected (http=%d) — check client_id/secret\n", att.code);
    result.status = OpenSkyPollStatus::AuthError;
    return result;
  }

  if (att.code != 200 || !att.body_ok) {
    Serial.printf("[opensky_client] /states/all failed (http=%d, parsed=%d)\n", att.code,
                  att.body_ok ? 1 : 0);
    result.status = OpenSkyPollStatus::NetworkError;
    return result;
  }

  result.status = OpenSkyPollStatus::Ok;
  result.aircraft = std::move(att.aircraft);
  return result;
}

OpenSkyPollResult openSkyClientPoll(uint32_t now_ms, const Config &cfg) {
  if (!shouldPollNow(now_ms, lastPollMs, everPolled, cfg.poll_interval_s * 1000UL)) {
    return {};  // OpenSkyPollStatus::NotDue
  }

  OpenSkyPollResult result = fetchAircraftStates(cfg);
  lastPollMs = now_ms;
  everPolled = true;
  return result;
}

#endif  // ARDUINO
