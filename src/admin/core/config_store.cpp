#include "admin/core/config_store.h"

#include <algorithm>

#include "admin/core/firmware/release_check.h"  // isReleaseTagFormat()
#include "admin/core/web_auth.h"       // validateWebPassword()

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

Settings defaultConfig() { return Settings{}; }

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

Settings sanitizeConfig(const Settings &cfg) {
  Settings out = cfg;
  out.home.lat = clampLat(out.home.lat);
  out.home.lon = clampLon(out.home.lon);
  out.home.radius_deg = clampRadiusDeg(out.home.radius_deg);
  out.poll.interval_s = clampPollIntervalS(out.poll.interval_s);
  out.display.last_screen = clampLastScreen(out.display.last_screen);
  out.auto_cycle.interval_s = clampAutoCycleIntervalS(out.auto_cycle.interval_s);
  out.phase.near_airport_km = clampNearAirportKm(out.phase.near_airport_km);
  out.phase.climb_rate_threshold_mps =
      clampClimbRateThresholdMps(out.phase.climb_rate_threshold_mps);
  out.display.language = clampLanguage(static_cast<int>(out.display.language));
  out.web.language = clampLanguage(static_cast<int>(out.web.language));
  if (validateWebPassword(out.web.password) != WebPasswordCheck::Ok) out.web.password.clear();
  if (!out.update.skipped_tag.empty() && !isReleaseTagFormat(out.update.skipped_tag)) {
    out.update.skipped_tag.clear();
  }
  out.backlight.brightness_fixed_pct = clampBrightnessPct(out.backlight.brightness_fixed_pct);
  out.backlight.brightness_min_pct = clampBrightnessPct(out.backlight.brightness_min_pct);
  out.backlight.brightness_max_pct = clampBrightnessPct(out.backlight.brightness_max_pct);
  if (out.backlight.brightness_min_pct > out.backlight.brightness_max_pct) {
    std::swap(out.backlight.brightness_min_pct, out.backlight.brightness_max_pct);
  }
  out.backlight.night_mode = clampNightMode(static_cast<int>(out.backlight.night_mode));
  out.backlight.night_from_min = clampClockMinutes(out.backlight.night_from_min);
  out.backlight.night_to_min = clampClockMinutes(out.backlight.night_to_min);
  out.backlight.night_pct = clampNightPct(out.backlight.night_pct);
  out.backlight.night_wake_s = clampNightWakeS(out.backlight.night_wake_s);
  return out;
}
