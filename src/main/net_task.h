// net_task — the network task on core 0 (spec/done/04-dual-core.md §5.6).
//
// Owns everything that talks to the network: WiFi (wifi_manager), NTP
// (time_sync), the config web page + mDNS (config_portal), OpenSky polling
// (opensky_client), hexdb.io enrichment with its caches (aircraft_lookup,
// route_lookup, lookup_store, via poll_pipeline), the GitHub release check
// and both OTA paths, and marking a new image valid. It never touches the
// display: what the UI needs goes out as a NetState snapshot (core_link),
// published only when something changed; commands from the UI come in
// through core_link's queue.
//
// Device-only (FreeRTOS task, Arduino networking).
#pragma once

#ifdef ARDUINO

#include <freertos/FreeRTOS.h>

#include <cstdint>
#include <memory>

struct Config;

constexpr uint32_t kNetTaskStackBytes = 16 * 1024;  // TLS to GitHub verifies P-384 chains
constexpr UBaseType_t kNetTaskPriority = 1;         // below WiFi (23) / lwIP (18)
constexpr BaseType_t kNetTaskCore = 0;              // where WiFi and lwIP already run
constexpr uint32_t kNetIdleSleepMs = 5;             // each pass: the web page stays snappy,
                                                    // core 0's idle task (watchdog) gets CPU

// Starts the task. `bootCfg` is what setup() loaded (no second NVS read);
// `appPendingVerify`: the running image still has to be marked valid.
void netTaskStart(std::shared_ptr<const Config> bootCfg, bool appPendingVerify);

#endif  // ARDUINO
