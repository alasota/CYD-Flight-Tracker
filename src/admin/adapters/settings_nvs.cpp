// settings_nvs — the Preferences (NVS) half of config_store: loads every
// setting through sanitizeConfig() and saves them back, plus the single-key
// writes (last screen, update choices). Hardware adapter, not covered by Unity
// (CLAUDE.md "Testing").
#include "admin/core/config_store.h"

#include <Preferences.h>

#include "admin/core/firmware/release_check.h"  // isReleaseTagFormat()

namespace {
constexpr char kNamespace[] = "cydsky";
}  // namespace

Config loadConfig() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  Config cfg = defaultConfig();
  cfg.home.lat = prefs.getFloat("home_lat", cfg.home.lat);
  cfg.home.lon = prefs.getFloat("home_lon", cfg.home.lon);
  cfg.home.radius_deg = prefs.getFloat("radius_deg", cfg.home.radius_deg);
  cfg.poll.interval_s = prefs.getUInt("poll_s", cfg.poll.interval_s);
  cfg.opensky.client_id = prefs.getString("os_id", "").c_str();
  cfg.opensky.client_secret = prefs.getString("os_secret", "").c_str();
  cfg.display.last_screen = clampLastScreen(prefs.getInt("last_screen", cfg.display.last_screen));
  cfg.auto_cycle.enabled = prefs.getBool("auto_cyc", cfg.auto_cycle.enabled);
  cfg.auto_cycle.interval_s = prefs.getUInt("auto_cyc_s", cfg.auto_cycle.interval_s);
  cfg.phase.near_airport_km = prefs.getFloat("near_km", cfg.phase.near_airport_km);
  cfg.phase.climb_rate_threshold_mps =
      prefs.getFloat("climb_mps", cfg.phase.climb_rate_threshold_mps);
  cfg.home.configured = prefs.getBool("home_cfg", cfg.home.configured);
  cfg.display.language =
      clampLanguage(prefs.getUChar("lang", static_cast<uint8_t>(cfg.display.language)));
  cfg.web_language =
      clampLanguage(prefs.getUChar("wlang", static_cast<uint8_t>(cfg.web_language)));
  // isKey() first: getString() logs an error for a key that was never written.
  if (prefs.isKey("wpass")) cfg.web_password = prefs.getString("wpass", "").c_str();
  cfg.update_check_enabled = prefs.getBool("updchk", cfg.update_check_enabled);
  cfg.update_show_prerelease = prefs.getBool("updpre", cfg.update_show_prerelease);
  if (prefs.isKey("updskip")) cfg.update_skipped_tag = prefs.getString("updskip", "").c_str();
  cfg.backlight.brightness_auto = prefs.getBool("bl_auto", cfg.backlight.brightness_auto);
  cfg.backlight.brightness_fixed_pct = prefs.getUChar("bl_fix", cfg.backlight.brightness_fixed_pct);
  cfg.backlight.brightness_min_pct = prefs.getUChar("bl_min", cfg.backlight.brightness_min_pct);
  cfg.backlight.brightness_max_pct = prefs.getUChar("bl_max", cfg.backlight.brightness_max_pct);
  cfg.backlight.night_mode =
      clampNightMode(prefs.getUChar("nt_mode", static_cast<uint8_t>(cfg.backlight.night_mode)));
  cfg.backlight.night_from_min = prefs.getUShort("nt_from", cfg.backlight.night_from_min);
  cfg.backlight.night_to_min = prefs.getUShort("nt_to", cfg.backlight.night_to_min);
  cfg.backlight.night_pct = prefs.getUChar("nt_pct", cfg.backlight.night_pct);
  cfg.backlight.night_wake_s = prefs.getUShort("nt_wake", cfg.backlight.night_wake_s);
  cfg.backlight.night_pause_polling = prefs.getBool("nt_poll", cfg.backlight.night_pause_polling);

  prefs.end();
  return sanitizeConfig(cfg);
}

void saveConfig(const Config &cfg) {
  Config sanitized = sanitizeConfig(cfg);

  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);

  prefs.putFloat("home_lat", sanitized.home.lat);
  prefs.putFloat("home_lon", sanitized.home.lon);
  prefs.putFloat("radius_deg", sanitized.home.radius_deg);
  prefs.putUInt("poll_s", sanitized.poll.interval_s);
  prefs.putString("os_id", sanitized.opensky.client_id.c_str());
  prefs.putString("os_secret", sanitized.opensky.client_secret.c_str());
  prefs.putInt("last_screen", sanitized.display.last_screen);
  prefs.putBool("auto_cyc", sanitized.auto_cycle.enabled);
  prefs.putUInt("auto_cyc_s", sanitized.auto_cycle.interval_s);
  prefs.putFloat("near_km", sanitized.phase.near_airport_km);
  prefs.putFloat("climb_mps", sanitized.phase.climb_rate_threshold_mps);
  prefs.putBool("home_cfg", sanitized.home.configured);
  prefs.putUChar("lang", static_cast<uint8_t>(sanitized.display.language));
  prefs.putUChar("wlang", static_cast<uint8_t>(sanitized.web_language));
  prefs.putString("wpass", sanitized.web_password.c_str());
  prefs.putBool("updchk", sanitized.update_check_enabled);
  prefs.putBool("updpre", sanitized.update_show_prerelease);
  prefs.putString("updskip", sanitized.update_skipped_tag.c_str());
  prefs.putBool("bl_auto", sanitized.backlight.brightness_auto);
  prefs.putUChar("bl_fix", sanitized.backlight.brightness_fixed_pct);
  prefs.putUChar("bl_min", sanitized.backlight.brightness_min_pct);
  prefs.putUChar("bl_max", sanitized.backlight.brightness_max_pct);
  prefs.putUChar("nt_mode", static_cast<uint8_t>(sanitized.backlight.night_mode));
  prefs.putUShort("nt_from", sanitized.backlight.night_from_min);
  prefs.putUShort("nt_to", sanitized.backlight.night_to_min);
  prefs.putUChar("nt_pct", sanitized.backlight.night_pct);
  prefs.putUShort("nt_wake", sanitized.backlight.night_wake_s);
  prefs.putBool("nt_poll", sanitized.backlight.night_pause_polling);

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
