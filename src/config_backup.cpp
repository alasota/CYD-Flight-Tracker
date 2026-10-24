#include "config_backup.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "backlight.h"  // nightModeCode() / parseClockMinutes()
#include "web_auth.h"   // validateWebPassword()

std::string configToBackupJson(const Config &cfg, bool includeSecrets, const char *firmwareVersion) {
  JsonDocument doc;
  doc["format"] = kBackupFormat;
  doc["format_version"] = kBackupFormatVersion;
  doc["firmware"] = firmwareVersion != nullptr ? firmwareVersion : "";

  JsonObject location = doc["location"].to<JsonObject>();
  location["lat"] = cfg.home_lat;
  location["lon"] = cfg.home_lon;
  location["radius_deg"] = cfg.radius_deg;

  JsonObject opensky = doc["opensky"].to<JsonObject>();
  if (includeSecrets) {
    opensky["client_id"] = cfg.opensky_client_id;
    opensky["client_secret"] = cfg.opensky_client_secret;
  }
  opensky["poll_interval_s"] = cfg.poll_interval_s;

  JsonObject general = doc["general"].to<JsonObject>();
  general["language"] = languageCode(cfg.language);
  general["web_language"] = languageCode(cfg.web_language);
  general["auto_cycle"] = cfg.auto_cycle_enabled;
  general["auto_cycle_interval_s"] = cfg.auto_cycle_interval_s;
  general["near_airport_km"] = cfg.near_airport_km;
  general["climb_rate_mps"] = cfg.climb_rate_threshold_mps;
  general["update_check"] = cfg.update_check_enabled;
  general["update_show_prerelease"] = cfg.update_show_prerelease;

  JsonObject display = doc["display"].to<JsonObject>();
  display["brightness_auto"] = cfg.brightness_auto;
  display["brightness_fixed"] = cfg.brightness_fixed_pct;
  display["brightness_min"] = cfg.brightness_min_pct;
  display["brightness_max"] = cfg.brightness_max_pct;
  display["night_mode"] = nightModeCode(cfg.night_mode);
  display["night_from"] = formatClockMinutes(cfg.night_from_min);
  display["night_to"] = formatClockMinutes(cfg.night_to_min);
  display["night_brightness"] = cfg.night_pct;
  display["night_wake_s"] = cfg.night_wake_s;
  display["night_pause_polling"] = cfg.night_pause_polling;

  // The panel password is a secret too: only with includeSecrets, and only
  // when one is set.
  if (includeSecrets && !cfg.web_password.empty()) {
    doc["security"]["web_password"] = cfg.web_password;
  }

  std::string out;
  serializeJsonPretty(doc, out);
  return out;
}

namespace {

// Applies one key of `group`: absent -> nothing, right type -> set + count,
// wrong type -> reported in invalidFields.
class Applier {
 public:
  explicit Applier(BackupRestoreResult &r) : r_(r) {}

  void number(JsonObjectConst group, const char *groupName, const char *key, float &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    if (!v.is<double>()) return invalid(groupName, key);
    out = v.as<float>();
    ++r_.appliedFields;
  }

  void count(JsonObjectConst group, const char *groupName, const char *key, uint32_t &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    if (!v.is<long long>() || v.as<long long>() < 0 || v.as<long long>() > 0xFFFFFFFFLL) {
      return invalid(groupName, key);
    }
    out = static_cast<uint32_t>(v.as<long long>());
    ++r_.appliedFields;
  }

  void text(JsonObjectConst group, const char *groupName, const char *key, std::string &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    if (!v.is<const char *>()) return invalid(groupName, key);
    out = v.as<const char *>();
    ++r_.appliedFields;
  }

  void flag(JsonObjectConst group, const char *groupName, const char *key, bool &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    if (!v.is<bool>()) return invalid(groupName, key);
    out = v.as<bool>();
    ++r_.appliedFields;
  }

  // A small whole number (percent, seconds), capped at what T holds — the
  // real range checks are sanitizeConfig()'s.
  template <typename T>
  void small(JsonObjectConst group, const char *groupName, const char *key, T &out) {
    uint32_t v = out;
    count(group, groupName, key, v);
    out = static_cast<T>(std::min<uint32_t>(v, std::numeric_limits<T>::max()));
  }

  // "HH:MM" -> minutes after midnight.
  void clock(JsonObjectConst group, const char *groupName, const char *key, uint16_t &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    int minutes = 0;
    if (!v.is<const char *>() || !parseClockMinutes(v.as<const char *>(), &minutes)) {
      return invalid(groupName, key);
    }
    out = static_cast<uint16_t>(minutes);
    ++r_.appliedFields;
  }

  void nightMode(JsonObjectConst group, const char *groupName, const char *key, NightMode &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    NightMode m;
    if (!v.is<const char *>() || !nightModeFromCode(v.as<const char *>(), &m)) {
      return invalid(groupName, key);
    }
    out = m;
    ++r_.appliedFields;
  }

  void language(JsonObjectConst group, const char *groupName, const char *key, Language &out) {
    JsonVariantConst v = group[key];
    if (v.isNull()) return;
    const char *code = v.is<const char *>() ? v.as<const char *>() : nullptr;
    if (code == nullptr || (std::strcmp(code, "pl") != 0 && std::strcmp(code, "en") != 0)) {
      return invalid(groupName, key);
    }
    out = languageFromCode(code);
    ++r_.appliedFields;
  }

 private:
  void invalid(const char *groupName, const char *key) {
    r_.invalidFields.push_back(std::string(groupName) + "." + key);
  }
  BackupRestoreResult &r_;
};

}  // namespace

BackupRestoreResult restoreConfigFromBackupJson(const std::string &json, const Config &current) {
  BackupRestoreResult r;
  r.config = current;
  if (json.size() > kMaxBackupJsonBytes) return r;  // NotJson

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonObject>()) return r;

  const char *format = doc["format"];
  if (format == nullptr || std::strcmp(format, kBackupFormat) != 0 ||
      doc["format_version"].as<int>() > kBackupFormatVersion) {
    r.status = BackupRestoreStatus::WrongFormat;
    return r;
  }
  r.status = BackupRestoreStatus::Ok;

  Applier a(r);
  Config &c = r.config;

  JsonObjectConst location = doc["location"];
  const int beforeLocation = r.appliedFields;
  a.number(location, "location", "lat", c.home_lat);
  a.number(location, "location", "lon", c.home_lon);
  a.number(location, "location", "radius_deg", c.radius_deg);
  if (r.appliedFields > beforeLocation) c.home_configured = true;

  JsonObjectConst opensky = doc["opensky"];
  a.text(opensky, "opensky", "client_id", c.opensky_client_id);
  a.text(opensky, "opensky", "client_secret", c.opensky_client_secret);
  a.count(opensky, "opensky", "poll_interval_s", c.poll_interval_s);

  JsonObjectConst general = doc["general"];
  a.language(general, "general", "language", c.language);
  a.language(general, "general", "web_language", c.web_language);
  a.flag(general, "general", "auto_cycle", c.auto_cycle_enabled);
  a.count(general, "general", "auto_cycle_interval_s", c.auto_cycle_interval_s);
  a.number(general, "general", "near_airport_km", c.near_airport_km);
  a.number(general, "general", "climb_rate_mps", c.climb_rate_threshold_mps);
  a.flag(general, "general", "update_check", c.update_check_enabled);
  a.flag(general, "general", "update_show_prerelease", c.update_show_prerelease);

  JsonObjectConst display = doc["display"];
  a.flag(display, "display", "brightness_auto", c.brightness_auto);
  a.small(display, "display", "brightness_fixed", c.brightness_fixed_pct);
  a.small(display, "display", "brightness_min", c.brightness_min_pct);
  a.small(display, "display", "brightness_max", c.brightness_max_pct);
  a.nightMode(display, "display", "night_mode", c.night_mode);
  a.clock(display, "display", "night_from", c.night_from_min);
  a.clock(display, "display", "night_to", c.night_to_min);
  a.small(display, "display", "night_brightness", c.night_pct);
  a.small(display, "display", "night_wake_s", c.night_wake_s);
  a.flag(display, "display", "night_pause_polling", c.night_pause_polling);

  JsonObjectConst security = doc["security"];
  std::string password = c.web_password;
  const int beforePassword = r.appliedFields;
  a.text(security, "security", "web_password", password);
  if (r.appliedFields > beforePassword) {
    if (validateWebPassword(password) == WebPasswordCheck::Ok) {
      c.web_password = password;
    } else {
      --r.appliedFields;
      r.invalidFields.push_back("security.web_password");
    }
  }

  return r;
}
