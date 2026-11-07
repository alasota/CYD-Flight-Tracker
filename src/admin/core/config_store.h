// config_store — Preferences/NVS-backed persistence for all user-configurable
// settings (home position, scan radius, poll interval, OpenSky OAuth
// credentials, last-active screen). See CLAUDE.md "Code conventions".
//
// Split in two per CLAUDE.md "Testing": the struct + pure validation/default
// helpers below have no hardware dependency and run under `pio test -e
// native`; loadConfig()/saveConfig() are the thin Preferences adapter
// admin/adapters/settings_nvs, compiled only for the device.
#pragma once

#include <cstdint>
#include <string>

#include "airspace/core/flight_phase.h"  // PhaseThresholds
#include "airspace/core/home.h"          // Home
#include "display/core/backlight.h"      // BacklightSettings, NightMode
#include "display/core/display_settings.h"  // DisplaySettings, AutoCycleSettings
#include "display/core/i18n.h"           // Language
#include "opensky/core/opensky_settings.h"  // OpenSkyCredentials, PollSettings

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

struct Config {
  Home home;  // home position, scan radius, configured flag (airspace)
  PollSettings poll;            // seconds between /states/all requests (opensky)
  OpenSkyCredentials opensky;   // OAuth2 client id / secret (opensky)
  PhaseThresholds phase;  // flight_phase thresholds (airspace)
  DisplaySettings display;         // display language, last screen (display)
  AutoCycleSettings auto_cycle;    // auto screen cycling (display)
  BacklightSettings backlight;     // brightness, night mode (display)
  // Language of the config web page itself (web_i18n), independent of the
  // display's. NVS key `wlang`, same index/clamping as `language`.
  Language web_language = kDefaultLanguage;
  // Optional password of the config web page (web_auth; HTTP Digest, login
  // "admin"). Empty = no password. NVS key `wpass`, plain text — Digest needs
  // the password itself. One that fails validateWebPassword() is dropped by
  // sanitizeConfig().
  std::string web_password;
  // New versions on GitHub (release_check, spec/done/02-github-release-check.md).
  // update_check_enabled: ask GitHub once a day (NVS `updchk`) — it only
  // checks, installing always needs a YES. update_show_prerelease: the
  // config page also lists development versions (pre-releases; NVS
  // `updpre`). update_skipped_tag: the release NO was tapped for on the
  // display, which then isn't asked about again (NVS `updskip`; not in
  // backups — device state, not a setting); one that isn't a vX.Y.Z tag is
  // dropped by sanitizeConfig().
  bool update_check_enabled = true;
  bool update_show_prerelease = false;
  std::string update_skipped_tag;
};

// Hardcoded fallback values used before anything has ever been saved to NVS.
Config defaultConfig();

// Clamps a caller-supplied config to safe ranges (lat/lon to valid degrees,
// radius floor/ceiling, a minimum poll interval so the firmware can't be
// configured to hammer the OpenSky API faster than docs/data-sources.md
// "Rate limits" allows). Pure — no NVS access — so it's covered by native tests.
Config sanitizeConfig(const Config &cfg);

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
// sanitizeConfig() also swaps min / max when min > max.
uint8_t clampBrightnessPct(int pct);
uint8_t clampNightPct(int pct);
uint16_t clampClockMinutes(int minutes);
uint16_t clampNightWakeS(int seconds);

// --- Hardware adapter: reads/writes ESP32 NVS via the Preferences library.
// Thin wrapper around a hardware/SDK API — see CLAUDE.md "Testing" — not
// covered by Unity. Both always sanitize before returning/persisting.
Config loadConfig();
void saveConfig(const Config &cfg);

// Persists just the last-active screen index (0/1/2), clamped via
// clampLastScreen(). A targeted single-key write for the frequently-
// changing nav state — a screen change (especially under auto-cycle,
// which fires every few seconds) shouldn't rewrite lat/lon/credentials/
// every other setting the way saveConfig() does.
void saveLastScreen(int screen);

// Single-key writes for release_check's state (same reason as above).
void saveUpdateSkippedTag(const std::string &tag);
void saveUpdateShowPrerelease(bool show);
