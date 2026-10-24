#include "config_store.h"

#include <algorithm>

#include "release_check.h"  // isReleaseTagFormat()
#include "web_auth.h"       // validateWebPassword()

namespace {
constexpr float kMinRadiusDeg = 0.1f;
constexpr float kMaxRadiusDeg = 10.0f;
constexpr uint32_t kMinPollIntervalS = 5;
// Floor for the screen auto-cycle interval — see clampAutoCycleIntervalS().
constexpr uint32_t kMinAutoCycleIntervalS = 3;
// flight_phase thresholds: strictly-positive floor, roomy ceiling.
constexpr float kMinNearAirportKm = 0.5f;
constexpr float kMaxNearAirportKm = 500.0f;
constexpr float kMinClimbRateThresholdMps = 0.1f;
constexpr float kMaxClimbRateThresholdMps = 50.0f;
constexpr float kMinLatDeg = -90.0f;
constexpr float kMaxLatDeg = 90.0f;
constexpr float kMinLonDeg = -180.0f;
constexpr float kMaxLonDeg = 180.0f;

float clampLat(float lat) { return std::min(std::max(lat, kMinLatDeg), kMaxLatDeg); }
float clampLon(float lon) { return std::min(std::max(lon, kMinLonDeg), kMaxLonDeg); }
}  // namespace

int clampLastScreen(int raw) {
  if (raw < 0 || raw > 2) return 0;
  return raw;
}

Config defaultConfig() { return Config{}; }

float clampRadiusDeg(float radius_deg) {
  return std::min(std::max(radius_deg, kMinRadiusDeg), kMaxRadiusDeg);
}

uint32_t clampPollIntervalS(uint32_t poll_interval_s) {
  return std::max(poll_interval_s, kMinPollIntervalS);
}

uint32_t clampAutoCycleIntervalS(uint32_t interval_s) {
  return std::max(interval_s, kMinAutoCycleIntervalS);
}

float clampNearAirportKm(float near_airport_km) {
  return std::min(std::max(near_airport_km, kMinNearAirportKm), kMaxNearAirportKm);
}

float clampClimbRateThresholdMps(float climb_rate_threshold_mps) {
  return std::min(std::max(climb_rate_threshold_mps, kMinClimbRateThresholdMps),
                  kMaxClimbRateThresholdMps);
}

uint8_t clampBrightnessPct(int pct) { return static_cast<uint8_t>(std::min(std::max(pct, 5), 100)); }

uint8_t clampNightPct(int pct) { return static_cast<uint8_t>(std::min(std::max(pct, 0), 100)); }

uint16_t clampClockMinutes(int minutes) {
  return static_cast<uint16_t>(std::min(std::max(minutes, 0), 1439));
}

uint16_t clampNightWakeS(int seconds) {
  return static_cast<uint16_t>(std::min(std::max(seconds, 10), 600));
}

Config sanitizeConfig(const Config &cfg) {
  Config out = cfg;
  out.home_lat = clampLat(out.home_lat);
  out.home_lon = clampLon(out.home_lon);
  out.radius_deg = clampRadiusDeg(out.radius_deg);
  out.poll_interval_s = clampPollIntervalS(out.poll_interval_s);
  out.last_screen = clampLastScreen(out.last_screen);
  out.auto_cycle_interval_s = clampAutoCycleIntervalS(out.auto_cycle_interval_s);
  out.near_airport_km = clampNearAirportKm(out.near_airport_km);
  out.climb_rate_threshold_mps = clampClimbRateThresholdMps(out.climb_rate_threshold_mps);
  out.language = clampLanguage(static_cast<int>(out.language));
  out.web_language = clampLanguage(static_cast<int>(out.web_language));
  if (validateWebPassword(out.web_password) != WebPasswordCheck::Ok) out.web_password.clear();
  if (!out.update_skipped_tag.empty() && !isReleaseTagFormat(out.update_skipped_tag)) {
    out.update_skipped_tag.clear();
  }
  out.brightness_fixed_pct = clampBrightnessPct(out.brightness_fixed_pct);
  out.brightness_min_pct = clampBrightnessPct(out.brightness_min_pct);
  out.brightness_max_pct = clampBrightnessPct(out.brightness_max_pct);
  if (out.brightness_min_pct > out.brightness_max_pct) {
    std::swap(out.brightness_min_pct, out.brightness_max_pct);
  }
  out.night_mode = clampNightMode(static_cast<int>(out.night_mode));
  out.night_from_min = clampClockMinutes(out.night_from_min);
  out.night_to_min = clampClockMinutes(out.night_to_min);
  out.night_pct = clampNightPct(out.night_pct);
  out.night_wake_s = clampNightWakeS(out.night_wake_s);
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
  cfg.last_screen = clampLastScreen(prefs.getInt("last_screen", cfg.last_screen));
  cfg.auto_cycle_enabled = prefs.getBool("auto_cyc", cfg.auto_cycle_enabled);
  cfg.auto_cycle_interval_s = prefs.getUInt("auto_cyc_s", cfg.auto_cycle_interval_s);
  cfg.near_airport_km = prefs.getFloat("near_km", cfg.near_airport_km);
  cfg.climb_rate_threshold_mps = prefs.getFloat("climb_mps", cfg.climb_rate_threshold_mps);
  cfg.home_configured = prefs.getBool("home_cfg", cfg.home_configured);
  cfg.language = clampLanguage(prefs.getUChar("lang", static_cast<uint8_t>(cfg.language)));
  cfg.web_language =
      clampLanguage(prefs.getUChar("wlang", static_cast<uint8_t>(cfg.web_language)));
  // isKey() first: getString() logs an error for a key that was never written.
  if (prefs.isKey("wpass")) cfg.web_password = prefs.getString("wpass", "").c_str();
  cfg.update_check_enabled = prefs.getBool("updchk", cfg.update_check_enabled);
  cfg.update_show_prerelease = prefs.getBool("updpre", cfg.update_show_prerelease);
  if (prefs.isKey("updskip")) cfg.update_skipped_tag = prefs.getString("updskip", "").c_str();
  cfg.brightness_auto = prefs.getBool("bl_auto", cfg.brightness_auto);
  cfg.brightness_fixed_pct = prefs.getUChar("bl_fix", cfg.brightness_fixed_pct);
  cfg.brightness_min_pct = prefs.getUChar("bl_min", cfg.brightness_min_pct);
  cfg.brightness_max_pct = prefs.getUChar("bl_max", cfg.brightness_max_pct);
  cfg.night_mode = clampNightMode(prefs.getUChar("nt_mode", static_cast<uint8_t>(cfg.night_mode)));
  cfg.night_from_min = prefs.getUShort("nt_from", cfg.night_from_min);
  cfg.night_to_min = prefs.getUShort("nt_to", cfg.night_to_min);
  cfg.night_pct = prefs.getUChar("nt_pct", cfg.night_pct);
  cfg.night_wake_s = prefs.getUShort("nt_wake", cfg.night_wake_s);
  cfg.night_pause_polling = prefs.getBool("nt_poll", cfg.night_pause_polling);

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
  prefs.putInt("last_screen", sanitized.last_screen);
  prefs.putBool("auto_cyc", sanitized.auto_cycle_enabled);
  prefs.putUInt("auto_cyc_s", sanitized.auto_cycle_interval_s);
  prefs.putFloat("near_km", sanitized.near_airport_km);
  prefs.putFloat("climb_mps", sanitized.climb_rate_threshold_mps);
  prefs.putBool("home_cfg", sanitized.home_configured);
  prefs.putUChar("lang", static_cast<uint8_t>(sanitized.language));
  prefs.putUChar("wlang", static_cast<uint8_t>(sanitized.web_language));
  prefs.putString("wpass", sanitized.web_password.c_str());
  prefs.putBool("updchk", sanitized.update_check_enabled);
  prefs.putBool("updpre", sanitized.update_show_prerelease);
  prefs.putString("updskip", sanitized.update_skipped_tag.c_str());
  prefs.putBool("bl_auto", sanitized.brightness_auto);
  prefs.putUChar("bl_fix", sanitized.brightness_fixed_pct);
  prefs.putUChar("bl_min", sanitized.brightness_min_pct);
  prefs.putUChar("bl_max", sanitized.brightness_max_pct);
  prefs.putUChar("nt_mode", static_cast<uint8_t>(sanitized.night_mode));
  prefs.putUShort("nt_from", sanitized.night_from_min);
  prefs.putUShort("nt_to", sanitized.night_to_min);
  prefs.putUChar("nt_pct", sanitized.night_pct);
  prefs.putUShort("nt_wake", sanitized.night_wake_s);
  prefs.putBool("nt_poll", sanitized.night_pause_polling);

  prefs.end();
}

void saveLastScreen(int screen) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putInt("last_screen", clampLastScreen(screen));
  prefs.end();
}

void saveUpdateSkippedTag(const std::string &tag) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putString("updskip", isReleaseTagFormat(tag) ? tag.c_str() : "");
  prefs.end();
}

void saveUpdateShowPrerelease(bool show) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putBool("updpre", show);
  prefs.end();
}

#endif  // ARDUINO
