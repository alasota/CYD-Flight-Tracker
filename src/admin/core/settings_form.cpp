#include "admin/core/settings_form.h"

#include <cstdint>
#include <cstdlib>

#include <ArduinoJson.h>

#include "display/core/backlight.h"  // nightModeFromCode(), parseClockMinutes()

OpenSkyCredentials parseOpenSkyCredentialsJson(const std::string &json) {
  OpenSkyCredentials result;

  if (json.size() > kMaxCredentialsJsonBytes) {
    return result;  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return result;
  }
  if (doc["clientId"].isNull() || doc["clientSecret"].isNull()) {
    return result;
  }

  result.client_id = doc["clientId"].as<std::string>();
  result.client_secret = doc["clientSecret"].as<std::string>();
  result.ok = !result.client_id.empty() && !result.client_secret.empty();
  return result;
}

bool isValidFloatString(const std::string &s) {
  if (s.empty()) return false;
  const char *start = s.c_str();
  char *end = nullptr;
  std::strtof(start, &end);
  if (end == start) return false;  // no digits consumed at all
  while (*end == ' ' || *end == '\t') ++end;
  return *end == '\0';  // nothing but (optional trailing whitespace) left over
}

bool isValidUnsignedIntString(const std::string &s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// ---- The settings form (POST /save) ------------------------------------------

void FormArgs::add(const std::string &name, const std::string &value) {
  if (!has(name)) args_.emplace_back(name, value);
}

bool FormArgs::has(const std::string &name) const {
  for (const auto &a : args_) {
    if (a.first == name) return true;
  }
  return false;
}

std::string FormArgs::get(const std::string &name) const {
  for (const auto &a : args_) {
    if (a.first == name) return a.second;
  }
  return std::string();
}

WebStr passwordProblem(const PasswordChange &c) {
  if (c.outcome == PasswordChange::Outcome::Mismatch) return WebStr::PasswordMismatch;
  switch (c.check) {
    case WebPasswordCheck::TooShort:
      return WebStr::PasswordTooShort;
    case WebPasswordCheck::TooLong:
      return WebStr::PasswordTooLong;
    default:
      return WebStr::PasswordBadChar;
  }
}

namespace {

bool setFloat(const std::string &v, float &out) {
  if (!isValidFloatString(v)) return false;
  out = std::strtof(v.c_str(), nullptr);
  return true;
}

bool setUnsigned(const std::string &v, uint32_t &out) {
  if (!isValidUnsignedIntString(v)) return false;
  out = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
  return true;
}

// Brightness / night numbers: digits only, at most 4 of them; the range is
// clamped (clamp()), not rejected.
template <typename T>
bool setClamped(const std::string &v, T &out, T (*clamp)(int)) {
  if (!isValidUnsignedIntString(v) || v.size() > 4) return false;
  out = clamp(std::atoi(v.c_str()));
  return true;
}

bool setClock(const std::string &v, uint16_t &out) {
  int minutes = 0;
  if (!parseClockMinutes(v, &minutes)) return false;
  out = static_cast<uint16_t>(minutes);
  return true;
}

// One per number field: false = the value doesn't parse, cfg unchanged. Plain
// functions, not lambdas: the firmware builds as C++11, where a lambda's
// conversion to a function pointer isn't constexpr and the table below would
// be initialized at boot, in RAM.
bool setLat(Config &c, const std::string &v) { return setFloat(v, c.home.lat); }
bool setLon(Config &c, const std::string &v) { return setFloat(v, c.home.lon); }
bool setRadius(Config &c, const std::string &v) { return setFloat(v, c.home.radius_deg); }
bool setPoll(Config &c, const std::string &v) { return setUnsigned(v, c.poll_interval_s); }
bool setCycle(Config &c, const std::string &v) { return setUnsigned(v, c.auto_cycle_interval_s); }
bool setNearKm(Config &c, const std::string &v) { return setFloat(v, c.phase.near_airport_km); }
bool setClimb(Config &c, const std::string &v) {
  return setFloat(v, c.phase.climb_rate_threshold_mps);
}
bool setBlMin(Config &c, const std::string &v) {
  return setClamped(v, c.brightness_min_pct, clampBrightnessPct);
}
bool setBlMax(Config &c, const std::string &v) {
  return setClamped(v, c.brightness_max_pct, clampBrightnessPct);
}
bool setBlFixed(Config &c, const std::string &v) {
  return setClamped(v, c.brightness_fixed_pct, clampBrightnessPct);
}
bool setNightPct(Config &c, const std::string &v) {
  return setClamped(v, c.night_pct, clampNightPct);
}
bool setNightWake(Config &c, const std::string &v) {
  return setClamped(v, c.night_wake_s, clampNightWakeS);
}
bool setNightFrom(Config &c, const std::string &v) { return setClock(v, c.night_from_min); }
bool setNightTo(Config &c, const std::string &v) { return setClock(v, c.night_to_min); }

// The fields a value that doesn't parse is rejected for — it keeps its old
// value (docs/review-notes-2026.md 1.6). In the form's order, which is the
// order the page lists the rejected ones in.
struct FormField {
  const char *name;
  WebStr label;
  bool (*apply)(Config &cfg, const std::string &value);
};

const FormField kNumberFields[] = {
    {"lat", WebStr::Latitude, setLat},
    {"lon", WebStr::Longitude, setLon},
    {"radius", WebStr::Radius, setRadius},
    {"poll_interval", WebStr::PollInterval, setPoll},
    {"auto_cycle_interval", WebStr::AutoCycleInterval, setCycle},
    {"near_airport_km", WebStr::NearAirport, setNearKm},
    {"climb_rate_mps", WebStr::ClimbRate, setClimb},
    {"bl_min", WebStr::BrightnessMin, setBlMin},
    {"bl_max", WebStr::BrightnessMax, setBlMax},
    {"bl_fixed", WebStr::BrightnessFixedPct, setBlFixed},
    {"night_pct", WebStr::NightBrightness, setNightPct},
    {"night_wake_s", WebStr::NightWake, setNightWake},
    {"night_from", WebStr::NightFrom, setNightFrom},
    {"night_to", WebStr::NightTo, setNightTo},
};

}  // namespace

SettingsFormOutcome applySettingsForm(Config &cfg, const FormArgs &args) {
  SettingsFormOutcome out;

  for (const FormField &f : kNumberFields) {
    if (args.has(f.name) && !f.apply(cfg, args.get(f.name))) out.rejected.push_back(f.label);
  }
  // A field that wasn't sent is clamped all the same (the saved value).
  cfg.brightness_min_pct = clampBrightnessPct(cfg.brightness_min_pct);
  cfg.brightness_max_pct = clampBrightnessPct(cfg.brightness_max_pct);
  cfg.brightness_fixed_pct = clampBrightnessPct(cfg.brightness_fixed_pct);
  cfg.night_pct = clampNightPct(cfg.night_pct);
  cfg.night_wake_s = clampNightWakeS(cfg.night_wake_s);

  // Checkboxes: the browser sends one only when it's ticked. POST /save is
  // always a full settings-form submit, so a missing one means "unchecked".
  cfg.auto_cycle_enabled = args.has("auto_cycle");
  cfg.update_check_enabled = args.has("update_check");
  cfg.night_pause_polling = args.has("night_pause");

  if (args.has("brightness_mode")) cfg.brightness_auto = args.get("brightness_mode") != "fixed";
  NightMode mode;
  if (args.has("night_mode") && nightModeFromCode(args.get("night_mode"), &mode)) {
    cfg.night_mode = mode;
  }
  // An unknown code falls back to the default language.
  if (args.has("language")) cfg.language = languageFromCode(args.get("language").c_str());
  if (args.has("web_language")) {
    cfg.web_language = languageFromCode(args.get("web_language").c_str());
  }

  if (args.has("client_id")) cfg.opensky_client_id = args.get("client_id");
  // The page never shows the saved secret, so an empty field means "keep it".
  const std::string secret = args.get("client_secret");
  if (!secret.empty()) cfg.opensky_client_secret = secret;
  if (args.has("clear_credentials")) {
    cfg.opensky_client_id.clear();
    cfg.opensky_client_secret.clear();
  }

  // Panel password (web_auth): both fields empty = no change.
  out.password = decidePasswordChange(args.get("new_password"), args.get("new_password2"),
                                      args.has("clear_password"));
  switch (out.password.outcome) {
    case PasswordChange::Outcome::Set:
      cfg.web_password = out.password.value;
      out.message = WebStr::PasswordChanged;
      break;
    case PasswordChange::Outcome::Cleared:
      cfg.web_password.clear();
      out.message = WebStr::PasswordCleared;
      break;
    case PasswordChange::Outcome::Mismatch:
    case PasswordChange::Outcome::Invalid:
      out.rejected.push_back(passwordProblem(out.password));
      break;
    case PasswordChange::Outcome::Unchanged:
    default:
      break;
  }
  if (!out.rejected.empty()) out.message = WebStr::SavedWithInvalid;

  // The user has now been through the settings form at least once
  // (docs/review-notes-2026.md 1.5): what the UI checks, through NetState, to
  // tell "not configured yet" apart from "home really is at (0,0)".
  cfg.home.configured = true;
  return out;
}

std::string rejectedFieldsText(const std::vector<WebStr> &rejected, Language lang) {
  std::string text;
  for (WebStr f : rejected) {
    if (!text.empty()) text += ", ";
    text += webTr(f, lang);
  }
  return text;
}
