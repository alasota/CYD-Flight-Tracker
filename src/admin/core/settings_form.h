// settings_form — what the settings page's forms send, checked before it
// reaches config_store: the number fields (no "abc" silently read as 0) and
// the OpenSky credentials file. Pure, tested under `pio test -e native`; the
// WebServer routes that use it are web_server.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "admin/core/config_store.h"  // Config
#include "admin/core/web_auth.h"      // PasswordChange
#include "admin/core/web_i18n.h"      // WebStr, Language

// Parsed OpenSky OAuth client credentials from the JSON file OpenSky's
// account page generates for download: {"clientId":"...","clientSecret":
// "..."}.
struct OpenSkyCredentials {
  bool ok = false;  // true only if both fields were present and non-empty
  std::string client_id;
  std::string client_secret;
};

// Hard ceiling on how large a credentials JSON body
// parseOpenSkyCredentialsJson() will attempt to parse — matches the config
// page's own upload size cap, but enforced here too so the function is
// self-defending regardless of caller (see docs/review-notes-2026.md 4.3).
constexpr size_t kMaxCredentialsJsonBytes = 4096;

// Parses that JSON file's contents (uploaded on the config page's OpenSky
// tab, web_server). A body over kMaxCredentialsJsonBytes is rejected outright
// without being parsed. Pure — uses ArduinoJson but no WebServer/hardware
// dependency — tested under `pio test -e native`.
OpenSkyCredentials parseOpenSkyCredentialsJson(const std::string &json);

// True if `s` parses entirely as a valid (base-10) floating point number —
// no trailing garbage, not empty (leading whitespace is tolerated, same as
// strtof()). Used by the config form to reject malformed lat/lon/radius
// input instead of silently treating it as 0, which sanitizeConfig would
// then just clamp up to a "valid-looking" value with no feedback to the
// user — see docs/review-notes-2026.md 1.6. Pure — tested under
// `pio test -e native`.
bool isValidFloatString(const std::string &s);

// True if `s` is a valid non-negative base-10 integer (digits only, at
// least one digit) — same idea as isValidFloatString(), for the
// poll-interval field.
bool isValidUnsignedIntString(const std::string &s);

// ---- The settings form (POST /save) ------------------------------------------

// The fields of one submit, by name. Like WebServer::arg() / hasArg(), the
// first value of a name counts (a later one with the same name is ignored).
class FormArgs {
 public:
  void add(const std::string &name, const std::string &value);
  bool has(const std::string &name) const;
  std::string get(const std::string &name) const;  // "" when absent

 private:
  std::vector<std::pair<std::string, std::string>> args_;
};

struct SettingsFormOutcome {
  std::vector<WebStr> rejected;     // labels of the fields that kept their old value, form order
  PasswordChange password;          // what the submit did to the panel password
  WebStr message = WebStr::Saved;   // Saved / PasswordChanged / PasswordCleared / SavedWithInvalid
};

// Applies one settings-form submit to `cfg` in place (no second Config on
// core 0's stack): numbers that don't parse are rejected and keep their old
// value (docs/review-notes-2026.md 1.6), percent fields are clamped, a missing
// checkbox means unchecked, a missing text field means unchanged, an empty
// OpenSky secret keeps the saved one; sets home_configured
// (docs/review-notes-2026.md 1.5). The caller saves it.
SettingsFormOutcome applySettingsForm(Config &cfg, const FormArgs &args);

// "Latitude, Longitude, …" — the rejected labels in `lang`, joined by ", ".
std::string rejectedFieldsText(const std::vector<WebStr> &rejected, Language lang);
