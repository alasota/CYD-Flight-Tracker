#include "opensky/core/opensky_client.h"

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

bool shouldPollNow(uint32_t now_ms, uint32_t last_poll_ms, bool ever_polled, uint32_t interval_ms) {
  if (!ever_polled) {
    return true;
  }
  return (now_ms - last_poll_ms) >= interval_ms;  // unsigned: rollover-safe
}

uint32_t msUntilNextPoll(uint32_t now_ms, uint32_t last_poll_ms, bool ever_polled,
                         uint32_t interval_ms) {
  if (!ever_polled) return 0;
  const uint32_t elapsed = now_ms - last_poll_ms;  // unsigned: rollover-safe
  return elapsed >= interval_ms ? 0 : interval_ms - elapsed;
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
    if (!state[7].isNull()) {
      ac.baro_altitude = state[7].as<float>();
      ac.has_altitude = true;
    }
    if (!state[8].isNull()) ac.on_ground = state[8].as<bool>();
    if (!state[9].isNull()) {
      ac.velocity = state[9].as<float>();
      ac.has_velocity = true;
    }
    if (!state[10].isNull()) ac.true_track = state[10].as<float>();
    if (!state[11].isNull()) ac.vertical_rate = state[11].as<float>();

    ac.on_ground = inferOnGround(ac);  // apron traffic often lacks the flag
    result.push_back(ac);
  }
  return result;
}

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
