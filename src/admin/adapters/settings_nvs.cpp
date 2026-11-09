// settings_nvs — the Preferences (NVS) half of the settings module: loads every
// setting through sanitizeSettings() and saves them back, plus the single-key
// writes (last screen, update choices). Hardware adapter, not covered by Unity
// (CLAUDE.md "Testing").
#include "admin/core/settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "admin/core/firmware/release_check.h"  // isReleaseTagFormat()

namespace {
constexpr char kNamespace[] = "cydsky";

void putText(Preferences &prefs, NvsWriteCheck &check, const char *key, const std::string &value) {
  const size_t written = prefs.putString(key, value.c_str());
  // isKey() first: getString() logs an error for a key that was never written.
  const size_t stored =
      value.empty() && prefs.isKey(key) ? prefs.getString(key, "").length() : value.size();
  check.noteText(value.size(), written, stored);
}

// Closes the save and tells the serial log if any of it was refused.
bool finishSave(Preferences &prefs, const NvsWriteCheck &check, const char *what) {
  prefs.end();
  if (check.ok()) return true;
  Serial.printf("[settings] saving %s failed: %d value(s) not stored (NVS full?)\n", what,
                check.failures());
  return false;
}
}  // namespace

Settings loadSettings() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  Settings cfg = defaultSettings();
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
  cfg.web.language =
      clampLanguage(prefs.getUChar("wlang", static_cast<uint8_t>(cfg.web.language)));
  // isKey() first: getString() logs an error for a key that was never written.
  if (prefs.isKey("wpass")) cfg.web.password = prefs.getString("wpass", "").c_str();
  cfg.update.check_enabled = prefs.getBool("updchk", cfg.update.check_enabled);
  cfg.update.show_prerelease = prefs.getBool("updpre", cfg.update.show_prerelease);
  if (prefs.isKey("updskip")) cfg.update.skipped_tag = prefs.getString("updskip", "").c_str();
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
  return sanitizeSettings(cfg);
}

bool saveSettings(const Settings &cfg) {
  Settings sanitized = sanitizeSettings(cfg);

  Preferences prefs;
  NvsWriteCheck check(prefs.begin(kNamespace, /*readOnly=*/false));

  check.note(prefs.putFloat("home_lat", sanitized.home.lat));
  check.note(prefs.putFloat("home_lon", sanitized.home.lon));
  check.note(prefs.putFloat("radius_deg", sanitized.home.radius_deg));
  check.note(prefs.putUInt("poll_s", sanitized.poll.interval_s));
  putText(prefs, check, "os_id", sanitized.opensky.client_id);
  putText(prefs, check, "os_secret", sanitized.opensky.client_secret);
  check.note(prefs.putInt("last_screen", sanitized.display.last_screen));
  check.note(prefs.putBool("auto_cyc", sanitized.auto_cycle.enabled));
  check.note(prefs.putUInt("auto_cyc_s", sanitized.auto_cycle.interval_s));
  check.note(prefs.putFloat("near_km", sanitized.phase.near_airport_km));
  check.note(prefs.putFloat("climb_mps", sanitized.phase.climb_rate_threshold_mps));
  check.note(prefs.putBool("home_cfg", sanitized.home.configured));
  check.note(prefs.putUChar("lang", static_cast<uint8_t>(sanitized.display.language)));
  check.note(prefs.putUChar("wlang", static_cast<uint8_t>(sanitized.web.language)));
  putText(prefs, check, "wpass", sanitized.web.password);
  check.note(prefs.putBool("updchk", sanitized.update.check_enabled));
  check.note(prefs.putBool("updpre", sanitized.update.show_prerelease));
  putText(prefs, check, "updskip", sanitized.update.skipped_tag);
  check.note(prefs.putBool("bl_auto", sanitized.backlight.brightness_auto));
  check.note(prefs.putUChar("bl_fix", sanitized.backlight.brightness_fixed_pct));
  check.note(prefs.putUChar("bl_min", sanitized.backlight.brightness_min_pct));
  check.note(prefs.putUChar("bl_max", sanitized.backlight.brightness_max_pct));
  check.note(prefs.putUChar("nt_mode", static_cast<uint8_t>(sanitized.backlight.night_mode)));
  check.note(prefs.putUShort("nt_from", sanitized.backlight.night_from_min));
  check.note(prefs.putUShort("nt_to", sanitized.backlight.night_to_min));
  check.note(prefs.putUChar("nt_pct", sanitized.backlight.night_pct));
  check.note(prefs.putUShort("nt_wake", sanitized.backlight.night_wake_s));
  check.note(prefs.putBool("nt_poll", sanitized.backlight.night_pause_polling));

  return finishSave(prefs, check, "the settings");
}

bool saveLastScreen(int screen) {
  Preferences prefs;
  NvsWriteCheck check(prefs.begin(kNamespace, /*readOnly=*/false));
  check.note(prefs.putInt("last_screen", clampLastScreen(screen)));
  return finishSave(prefs, check, "the last screen");
}

bool saveUpdateSkippedTag(const std::string &tag) {
  Preferences prefs;
  NvsWriteCheck check(prefs.begin(kNamespace, /*readOnly=*/false));
  putText(prefs, check, "updskip", isReleaseTagFormat(tag) ? tag : std::string());
  return finishSave(prefs, check, "the skipped version");
}

bool saveUpdateShowPrerelease(bool show) {
  Preferences prefs;
  NvsWriteCheck check(prefs.begin(kNamespace, /*readOnly=*/false));
  check.note(prefs.putBool("updpre", show));
  return finishSave(prefs, check, "the development versions choice");
}
