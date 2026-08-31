#include "config_store.h"

#include <algorithm>

namespace {
constexpr float kMinRadiusDeg = 0.1f;
constexpr float kMaxRadiusDeg = 10.0f;
constexpr uint32_t kMinPollIntervalS = 5;
constexpr float kMinLatDeg = -90.0f;
constexpr float kMaxLatDeg = 90.0f;
constexpr float kMinLonDeg = -180.0f;
constexpr float kMaxLonDeg = 180.0f;

float clampLat(float lat) { return std::min(std::max(lat, kMinLatDeg), kMaxLatDeg); }
float clampLon(float lon) { return std::min(std::max(lon, kMinLonDeg), kMaxLonDeg); }
}  // namespace

ViewMode viewModeFromValue(uint8_t raw) {
  switch (raw) {
    case static_cast<uint8_t>(ViewMode::Table):
      return ViewMode::Table;
    case static_cast<uint8_t>(ViewMode::Radar):
      return ViewMode::Radar;
    default:
      return ViewMode::Table;
  }
}

Config defaultConfig() { return Config{}; }

float clampRadiusDeg(float radius_deg) {
  return std::min(std::max(radius_deg, kMinRadiusDeg), kMaxRadiusDeg);
}

uint32_t clampPollIntervalS(uint32_t poll_interval_s) {
  return std::max(poll_interval_s, kMinPollIntervalS);
}

Config sanitizeConfig(const Config &cfg) {
  Config out = cfg;
  out.home_lat = clampLat(out.home_lat);
  out.home_lon = clampLon(out.home_lon);
  out.radius_deg = clampRadiusDeg(out.radius_deg);
  out.poll_interval_s = clampPollIntervalS(out.poll_interval_s);
  return out;
}

#ifdef ARDUINO

#include <Preferences.h>

namespace {
constexpr char kNamespace[] = "cydsky";
}  // namespace

Config loadConfig() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  Config cfg = defaultConfig();
  cfg.home_lat = prefs.getFloat("home_lat", cfg.home_lat);
  cfg.home_lon = prefs.getFloat("home_lon", cfg.home_lon);
  cfg.radius_deg = prefs.getFloat("radius_deg", cfg.radius_deg);
  cfg.poll_interval_s = prefs.getUInt("poll_s", cfg.poll_interval_s);
  cfg.opensky_client_id = prefs.getString("os_id", "").c_str();
  cfg.opensky_client_secret = prefs.getString("os_secret", "").c_str();
  cfg.last_view =
      viewModeFromValue(prefs.getUChar("last_view", static_cast<uint8_t>(cfg.last_view)));

  prefs.end();
  return sanitizeConfig(cfg);
}

void saveConfig(const Config &cfg) {
  Config sanitized = sanitizeConfig(cfg);

  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);

  prefs.putFloat("home_lat", sanitized.home_lat);
  prefs.putFloat("home_lon", sanitized.home_lon);
  prefs.putFloat("radius_deg", sanitized.radius_deg);
  prefs.putUInt("poll_s", sanitized.poll_interval_s);
  prefs.putString("os_id", sanitized.opensky_client_id.c_str());
  prefs.putString("os_secret", sanitized.opensky_client_secret.c_str());
  prefs.putUChar("last_view", static_cast<uint8_t>(sanitized.last_view));

  prefs.end();
}

#endif  // ARDUINO
