// settings — Preferences/NVS-backed persistence for all user-configurable
// settings: `Settings`, the aggregate of the contexts' settings objects (home,
// flight phases, OpenSky account and poll interval, display, auto-cycle,
// backlight, update check, settings page). See CLAUDE.md "Code conventions".
//
// Split in two per CLAUDE.md "Testing": the struct + pure validation/default
// helpers below have no hardware dependency and run under `pio test -e
// native`; loadSettings()/saveSettings() are the thin Preferences adapter
// admin/adapters/settings_nvs, compiled only for the device.
#pragma once

#include <cstdint>
#include <string>

#include "airspace/core/flight_phase.h"       // PhaseThresholds
#include "airspace/core/home.h"               // Home
#include "display/core/backlight.h"           // BacklightSettings, NightMode
#include "display/core/display_settings.h"    // DisplaySettings, AutoCycleSettings
#include "display/core/i18n.h"                // Language
#include "opensky/core/opensky_settings.h"    // OpenSkyCredentials, PollSettings

// Clamps the auto-cycle interval (see docs/screens.md "Auto screen cycling") to
// a sane floor — a few seconds minimum, so the display can't be
// configured to flip faster than the eye can follow, and never zero
// (docs/screens.md "Auto screen cycling": must be > 0). No ceiling: a very
// long interval is a legitimate "basically don't cycle" choice. Pure — no
// NVS access — so it's covered by native tests.
uint32_t clampAutoCycleIntervalS(uint32_t interval_s);

// Clamps a persisted screen index to a valid screen_nav index
// (0 = Flights, 1 = Flight, 2 = Radar — see docs/screens.md "Screen
// navigation"). Any out-of-range value (NVS never written, or a stale
// value left behind by a future format change) falls back to 0 (Flights).
int clampLastScreen(int raw);

// New versions on GitHub (release_check, spec/done/02-github-release-check.md),
// admin's settings object. check_enabled: ask GitHub once a day (NVS
// `updchk`) — it only checks, installing always needs a YES.
// show_prerelease: the config page also lists development versions
// (pre-releases; NVS `updpre`). skipped_tag: the release NO was tapped for on
// the display, which then isn't asked about again (NVS `updskip`; not in
// backups — device state, not a setting); one that isn't a vX.Y.Z tag is
// dropped by sanitizeSettings().
struct UpdateSettings {
  bool check_enabled = true;
  bool show_prerelease = false;
  std::string skipped_tag;
};

// The settings page itself (admin's settings object).
struct WebSettings {
  // Language of the config web page (web_i18n), independent of the
  // display's. NVS key `wlang`, same index/clamping as the display language.
  Language language = kDefaultLanguage;
  // Optional password of the config web page (web_auth; HTTP Digest, login
  // "admin"). Empty = no password. NVS key `wpass`, plain text — Digest needs
  // the password itself. One that fails validateWebPassword() is dropped by
  // sanitizeSettings().
  std::string password;
};

// All settings: the aggregate of the contexts' settings objects (spec 12 §7).
// Each context's modules get only their objects; the NVS keys
// (admin/adapters/settings_nvs) and the backup JSON (config_backup) are flat
// and the same as before the split. ⚠️ gnu++11 on env:cyd: none of these
// structs is an aggregate there (default member initializers) — assign
// members, never `{…}`-initialize them.
struct Settings {
  Home home;                    // position, scan radius, configured (airspace)
  PhaseThresholds phase;        // flight_phase thresholds (airspace)
  OpenSkyCredentials opensky;   // OAuth2 client id / secret (opensky)
  PollSettings poll;            // seconds between /states/all requests (opensky)
  DisplaySettings display;      // display language, last screen (display)
  AutoCycleSettings auto_cycle;  // auto screen cycling (display)
  BacklightSettings backlight;  // brightness, night mode (display)
  UpdateSettings update;        // GitHub release check (admin)
  WebSettings web;              // settings page language, password (admin)
};

// Hardcoded fallback values used before anything has ever been saved to NVS.
Settings defaultSettings();

// Clamps a caller-supplied config to safe ranges (lat/lon to valid degrees,
// radius floor/ceiling, a minimum poll interval so the firmware can't be
// configured to hammer the OpenSky API faster than docs/data-sources.md
// "Rate limits" allows). Pure — no NVS access — so it's covered by native tests.
Settings sanitizeSettings(const Settings &cfg);

float clampRadiusDeg(float radius_deg);
uint32_t clampPollIntervalS(uint32_t poll_interval_s);

// flight_phase threshold clamps — both must stay strictly positive (a
// zero/negative "near airport" radius or climb cutoff would make
// classifyPhase() nonsensical), with a generous ceiling so a fat-fingered
// value in web_server can't break the classifier. Pure — native-tested.
float clampNearAirportKm(float near_airport_km);
float clampClimbRateThresholdMps(float climb_rate_threshold_mps);

// Brightness / night clamps (spec/03 §5.5): day brightness 5..100 %, night
// 0..100 % (0 = off), a time of day 0..1439 min, the wake time 10..600 s.
// sanitizeSettings() also swaps min / max when min > max.
uint8_t clampBrightnessPct(int pct);
uint8_t clampNightPct(int pct);
uint16_t clampClockMinutes(int minutes);
uint16_t clampNightWakeS(int seconds);

// Adds up the results of one save's Preferences::put* calls, which return the
// bytes written and 0 on failure. Pure — native-tested.
class NvsWriteCheck {
 public:
  explicit NvsWriteCheck(bool opened) : opened_(opened) {}

  // A fixed-size value (never 0 bytes when it worked).
  void note(size_t written);
  // A string: an empty one writes 0 bytes when it works too, so for that case
  // `storedLength`, read back after the put, is what tells (a failed put leaves
  // the old value there).
  void noteText(size_t length, size_t written, size_t storedLength);

  int failures() const { return failures_; }
  bool ok() const { return opened_ && failures_ == 0; }

 private:
  bool opened_;
  int failures_ = 0;
};

// --- Hardware adapter: reads/writes ESP32 NVS via the Preferences library.
// Thin wrapper around a hardware/SDK API — see CLAUDE.md "Testing" — not
// covered by Unity. Both always sanitize before returning/persisting.
Settings loadSettings();
void saveSettings(const Settings &cfg);

// Persists just the last-active screen index (0/1/2), clamped via
// clampLastScreen(). A targeted single-key write for the frequently-
// changing nav state — a screen change (especially under auto-cycle,
// which fires every few seconds) shouldn't rewrite lat/lon/credentials/
// every other setting the way saveSettings() does.
void saveLastScreen(int screen);

// Single-key writes for release_check's state (same reason as above).
void saveUpdateSkippedTag(const std::string &tag);
void saveUpdateShowPrerelease(bool show);
