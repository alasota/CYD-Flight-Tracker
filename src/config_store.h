// config_store — Preferences/NVS-backed persistence for all user-configurable
// settings (home position, scan radius, poll interval, OpenSky OAuth
// credentials, last-active view). See CLAUDE.md "Code conventions".
//
// Split in two per CLAUDE.md "Testing": the struct + pure validation/default
// helpers below have no hardware dependency and run under `pio test -e
// native`; loadConfig()/saveConfig() are the thin Preferences adapter and
// are only compiled for the ARDUINO (device) build.
#pragma once

#include <cstdint>
#include <string>

// Which screen (see CLAUDE.md "Visualization concept") was last shown -
// persisted so the display comes back up on the same view after a reboot.
enum class ViewMode : uint8_t {
  Table = 0,
  Radar = 1,
};

// Coerces a raw stored byte back into a ViewMode, falling back to Table for
// any value that isn't a known enumerator (e.g. NVS never written, or a
// stale value left behind by a future format change).
ViewMode viewModeFromValue(uint8_t raw);

struct Config {
  float home_lat = 0.0f;
  float home_lon = 0.0f;
  // Bounding-box half-width in degrees. Default keeps the resulting OpenSky
  // bbox in the 1-credit tier (<=25 sq deg) — see CLAUDE.md "Rate limits".
  float radius_deg = 2.5f;
  uint32_t poll_interval_s = 15;
  std::string opensky_client_id;
  std::string opensky_client_secret;
  ViewMode last_view = ViewMode::Table;
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
