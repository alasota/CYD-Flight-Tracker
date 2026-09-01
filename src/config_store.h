// config_store — Preferences/NVS-backed persistence for all user-configurable
// settings (home position, scan radius, poll interval, OpenSky OAuth
// credentials, last-active screen). See CLAUDE.md "Code conventions".
//
// Split in two per CLAUDE.md "Testing": the struct + pure validation/default
// helpers below have no hardware dependency and run under `pio test -e
// native`; loadConfig()/saveConfig() are the thin Preferences adapter and
// are only compiled for the ARDUINO (device) build.
#pragma once

#include <cstdint>
#include <string>

// Clamps a persisted screen index to a valid screen_nav index
// (0 = Flights, 1 = Flight, 2 = Radar — see CLAUDE.md "Screen
// navigation"). Any out-of-range value (NVS never written, or a stale
// value left behind by a future format change) falls back to 0 (Flights).
int clampLastScreen(int raw);

struct Config {
  float home_lat = 0.0f;
  float home_lon = 0.0f;
  // Bounding-box half-width in degrees. Default keeps the resulting OpenSky
  // bbox in the 1-credit tier (<=25 sq deg) — see CLAUDE.md "Rate limits".
  float radius_deg = 2.5f;
  uint32_t poll_interval_s = 15;
  std::string opensky_client_id;
  std::string opensky_client_secret;
  // Which of the three screens was showing at last power-down (0/1/2),
  // restored on boot — replaces the old two-screen `last_view` boolean now
  // that there are three screens (see CLAUDE.md "Screen navigation").
  int last_screen = 0;
  // True once the user has saved the settings form at least once via
  // config_portal. (0, 0) is a real place (Gulf of Guinea) — without this
  // flag there's no way to tell "user hasn't configured their home
  // location yet" apart from "user's home really is at 0,0", so the
  // firmware would happily poll/display aircraft around Null Island with
  // no indication anything needs setting up. See CLAUDE.md review notes
  // 1.5.
  bool home_configured = false;
};

// Hardcoded fallback values used before anything has ever been saved to NVS.
Config defaultConfig();

// Clamps a caller-supplied config to safe ranges (lat/lon to valid degrees,
// radius floor/ceiling, a minimum poll interval so the firmware can't be
// configured to hammer the OpenSky API faster than CLAUDE.md allows). Pure —
// no NVS access — so it's covered by native tests.
Config sanitizeConfig(const Config &cfg);

float clampRadiusDeg(float radius_deg);
uint32_t clampPollIntervalS(uint32_t poll_interval_s);

// --- Hardware adapter: reads/writes ESP32 NVS via the Preferences library.
// Thin wrapper around a hardware/SDK API — see CLAUDE.md "Testing" — not
// covered by Unity. Both always sanitize before returning/persisting.
Config loadConfig();
void saveConfig(const Config &cfg);
