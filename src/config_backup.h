// config_backup — the settings as a JSON file, for the config page's Backup
// tab: "download a backup" (configToBackupJson) and "restore from a backup"
// (restoreConfigFromBackupJson). Grouped like the page's tabs:
//
//   {
//     "format": "cyd-sky-tracker-config", "format_version": 1, "firmware": "v1.0.1",
//     "location": { "lat": 52.2297, "lon": 21.0122, "radius_deg": 1.0 },
//     "opensky":  { "client_id": "...", "client_secret": "...", "poll_interval_s": 30 },
//     "general":  { "language": "pl", "web_language": "en", "auto_cycle": false,
//                   "auto_cycle_interval_s": 15, "near_airport_km": 15.0,
//                   "climb_rate_mps": 1.5 }
//   }
//
// Restoring is forgiving: a missing key keeps the current value (so a file
// saved without credentials doesn't wipe them), a key with the wrong type is
// skipped and reported. Range checks are config_store's sanitizeConfig() job.
//
// Pure — ArduinoJson only, no WebServer/NVS — tested under `pio test -e native`.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "config_store.h"  // Config

constexpr const char *kBackupFormat = "cyd-sky-tracker-config";
constexpr int kBackupFormatVersion = 1;
// A real backup is ~400 bytes; anything far bigger isn't one.
constexpr size_t kMaxBackupJsonBytes = 4096;

// The backup file for `cfg`. Without `includeSecrets` the "opensky" group
// has no client_id / client_secret (restoring it then keeps the device's).
std::string configToBackupJson(const Config &cfg, bool includeSecrets, const char *firmwareVersion);

enum class BackupRestoreStatus {
  Ok,           // config holds the restored settings (maybe with skipped fields)
  NotJson,      // not parseable / too large
  WrongFormat,  // JSON, but not a Sky Tracker backup (no/other "format", newer format_version)
};

struct BackupRestoreResult {
  BackupRestoreStatus status = BackupRestoreStatus::NotJson;
  Config config;                           // `current` with the file's values applied
  int appliedFields = 0;                   // how many settings the file set
  std::vector<std::string> invalidFields;  // e.g. "location.lat" — wrong type, skipped
};

// Applies a backup file onto `current`. Marks the home location as configured
// when the file sets it (same as saving the Location tab).
BackupRestoreResult restoreConfigFromBackupJson(const std::string &json, const Config &current);
