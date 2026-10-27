#include "net_task.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <esp_ota_ops.h>

#include <map>
#include <string>

#include "aircraft_lookup.h"
#include "config_portal.h"
#include "config_store.h"
#include "core_link.h"
#include "device_alerts.h"
#include "hexdb_http.h"
#include "lookup_budget.h"
#include "lookup_store.h"
#include "opensky_client.h"
#include "ota_update.h"
#include "perf_stats.h"
#include "poll_pipeline.h"
#include "release_check.h"
#include "route_lookup.h"
#include "time_sync.h"
#include "version.h"
#include "wifi_manager.h"

namespace {

// ---- State owned by this task (nothing here is touched from core 1) ------

std::shared_ptr<const Config> cfg;  // the current settings, shared with the UI
uint32_t cfgSeq = 0;
uint32_t seenSaveCount = 0;
bool appPendingVerify = false;

NetState state;      // what the next publish will carry
NetState published;  // what the UI has (for netStateDiffers())
bool everPublished = false;

// ---- Publishing -----------------------------------------------------------

// Device alerts (spec 08), derived from the current state on every pass —
// cheap (no allocation), and a publish happens only when they change. Each
// change is logged once.
void refreshAlerts() {
  const AlertInputs in = deviceAlertInputs(*cfg);
  const AlertSet next = collectAlerts(in);
  if (next != state.alerts) {
    const std::string log = alertChangeLog(state.alerts, next);
    if (!log.empty()) Serial.println(log.c_str());
  }
  state.alerts = next;
  state.alertDetails = collectAlertDetails(in);
}

void refreshSummary() {
  state.cfg = cfg;
  state.cfgSeq = cfgSeq;
  state.wifi = wifiManagerStatus();
  state.restartPending = configPortalRestartPending();
  const UpdateState &u = releaseUpdateState();
  state.update.status = u.status;
  state.update.installed = u.installed;
  state.update.latestTag = u.latest.tag;
  state.update.installPending = releaseInstallPending();
  refreshAlerts();
}

// Hands the UI a new snapshot if anything it uses changed, and wakes it.
void publishIfChanged() {
  refreshSummary();
  if (everPublished && !netStateDiffers(published, state)) return;
  published = state;
  everPublished = true;
  core_link::netState.publish(std::make_shared<const NetState>(state));
  core_link::wakeUi();
}

// ---- OTA progress -> UI (the OTA code draws nothing) ------------------------

void otaBegin(size_t total, bool fromGithub) {
  state.ota.phase = OtaProgress::Phase::Running;
  state.ota.fromGithub = fromGithub;
  state.ota.written = 0;
  state.ota.total = static_cast<uint32_t>(total);
  state.ota.newVersion.clear();
  ++state.ota.seq;
  publishIfChanged();
}
void otaOnStart(size_t total) { otaBegin(total, false); }
void githubOtaOnStart(size_t total) { otaBegin(total, true); }

void otaOnProgress(size_t written, size_t total, const char *newVersion) {
  state.ota.written = static_cast<uint32_t>(written);
  state.ota.total = static_cast<uint32_t>(total);
  state.ota.newVersion = newVersion != nullptr ? newVersion : "";
  ++state.ota.seq;
  publishIfChanged();
  // Every 16 KB: a tick for core 0's idle task — a fast, steady download
  // could otherwise keep this task busy past the 5 s task watchdog.
  vTaskDelay(1);
}

void otaOnEnd(bool ok) {
  state.ota.phase = ok ? OtaProgress::Phase::Done : OtaProgress::Phase::Failed;
  ++state.ota.seq;
  publishIfChanged();
}

// ---- Config ------------------------------------------------------------------

void reloadConfig() {
  cfg = std::make_shared<const Config>(loadConfig());
  ++cfgSeq;
  releaseCheckBegin(*cfg);
}

// ---- GitHub gate (was main.cpp's updateGate()) ----------------------------

CheckGate updateGate() {
  CheckGate g;
  g.enabled = cfg->update_check_enabled;
  g.wifi = wifiManagerStatus() == WifiStatus::Connected;
  g.timeSynced = isTimeSynced();
  g.otaBusy = otaFlashSession().inProgress() || configPortalRestartPending();
  g.msUntilNextPoll = openSkyMsUntilNextPoll(millis(), *cfg);
  g.maxFreeBlock = ESP.getMaxAllocHeap();
  return g;
}

// ---- Rollback: mark the image valid once it has proven itself --------------

void markAppValidWhenStable() {
  if (!appPendingVerify) return;
  const WifiStatus ws = wifiManagerStatus();
  const bool reachable =
      (ws == WifiStatus::Connected && configPortalIsActive()) || ws == WifiStatus::PortalActive;
  const bool uiAlive =
      !heartbeatStale(millis(), core_link::uiBeatMs.load(), kUiAliveLimitMs);
  if (shouldRestartForRollback(appPendingVerify, millis(), uiAlive)) {
    Serial.println("[ota] the display loop has stopped on a new image: restarting to roll back");
    Serial.flush();
    ESP.restart();
  }
  if (shouldMarkAppValid(appPendingVerify, millis(), reachable, uiAlive)) {
    esp_ota_mark_app_valid_cancel_rollback();
    appPendingVerify = false;
    Serial.printf("[ota] %s marked valid after %u s, rollback cancelled\n", firmwareVersion(),
                  static_cast<unsigned>(millis() / 1000));
  }
}

// ---- Commands from the UI ------------------------------------------------------

void handleCommands() {
  NetCommand c;
  while (core_link::receiveCommand(&c)) {
    const std::string tag = netCommandTag(c);
    switch (c.type) {
      case NetCommandType::InstallTag:
        if (releaseInstallRequest(tag)) {
          Serial.printf("[update] YES on the display, installing %s\n", tag.c_str());
        } else {
          Serial.printf("[update] YES on the display, but %s is not on offer\n", tag.c_str());
        }
        break;
      case NetCommandType::SkipTag:
        Serial.printf("[update] NO on the display, %s skipped\n", tag.c_str());
        saveUpdateSkippedTag(tag);
        reloadConfig();
        break;
      case NetCommandType::None:
      default:
        break;
    }
  }
}

// ---- One OpenSky poll -> one or two snapshots ----------------------------------

// The lookups for poll_pipeline. Per poll, results are memoised so phase B
// doesn't repeat (or re-log) what phase A already looked up.
std::map<std::string, AircraftInfo> memoAircraft;
std::map<std::string, RouteInfo> memoRoute;
std::map<std::string, AirportInfo> memoAirport;

LookupPorts productionPorts() {
  LookupPorts p;
  // "Cached" = in RAM or persisted in NVS (lookup_store) — neither costs a
  // hexdb.io request, so neither uses up the per-poll budget.
  p.aircraftKnown = [](const std::string &k) {
    return memoAircraft.count(k) > 0 || aircraftLookupIsCached(k) || storeHasAircraft(k);
  };
  p.routeKnown = [](const std::string &k) {
    return memoRoute.count(k) > 0 || routeLookupIsCached(k) || storeHasRoute(k);
  };
  p.airportKnown = [](const std::string &k) {
    return memoAirport.count(k) > 0 || airportLookupIsCached(k) || storeHasAirport(k);
  };
  p.aircraft = [](const std::string &k) {
    auto it = memoAircraft.find(k);
    if (it != memoAircraft.end()) return it->second;
    return memoAircraft[k] = lookupAircraft(k);
  };
  p.route = [](const std::string &k) {
    auto it = memoRoute.find(k);
    if (it != memoRoute.end()) return it->second;
    return memoRoute[k] = lookupRoute(k);
  };
  p.airport = [](const std::string &k) {
    auto it = memoAirport.find(k);
    if (it != memoAirport.end()) return it->second;
    return memoAirport[k] = lookupAirportCountry(k);
  };
  // Between phase B's hexdb.io requests the settings page gets its turn —
  // otherwise it waits for all of them (spec 09 §3). Not nested (a handler
  // can't start another phase B); a firmware update started there ends
  // phase B, the main loop then stays off the network until the restart.
  // A settings save only reaches `cfg` after the poll (seenSaveCount).
  p.betweenFetches = [] {
    static bool inBetweenFetches = false;
    return serveBetweenFetches(inBetweenFetches, configPortalLoop, [] {
      return configPortalRestartPending() || otaFlashSession().inProgress();
    });
  };
  return p;
}

OpenSkyHealth healthFor(OpenSkyPollStatus s) {
  switch (s) {
    case OpenSkyPollStatus::Ok:
      return OpenSkyHealth::Ok;
    case OpenSkyPollStatus::RateLimited:
      return OpenSkyHealth::RateLimited;
    case OpenSkyPollStatus::AuthError:
      return OpenSkyHealth::AuthError;
    default:
      return OpenSkyHealth::NetworkError;
  }
}

void pollOpenSky() {
  if (wifiManagerStatus() != WifiStatus::Connected) return;
  // Paused at night when asked for (spec/03) — decided on the UI core.
  const bool allowed = core_link::pollingAllowed.load();
  static bool wasAllowed = true;
  if (allowed != wasAllowed) {
    wasAllowed = allowed;
    Serial.printf("[light] polling %s\n", allowed ? "resumed" : "paused (night)");
  }
  if (!allowed) return;

  OpenSkyPollResult poll = openSkyClientPoll(millis(), *cfg);
  if (poll.status == OpenSkyPollStatus::NotDue) return;
  const uint32_t polledAt = millis();

  state.health = healthFor(poll.status);
  Serial.printf("[main] poll status=%d aircraft=%u home_configured=%d free_heap=%u\n",
                static_cast<int>(poll.status), static_cast<unsigned>(poll.aircraft.size()),
                cfg->home_configured ? 1 : 0, static_cast<unsigned>(ESP.getFreeHeap()));

  // Only a clean poll updates the aircraft list — a failure keeps the last
  // good data on screen (same DataSnapshot pointer) rather than blanking it.
  if (poll.status != OpenSkyPollStatus::Ok || !cfg->home_configured) {
    publishIfChanged();
    return;
  }

  PipelineParams p;
  p.homeLat = cfg->home_lat;
  p.homeLon = cfg->home_lon;
  p.nearAirportKm = cfg->near_airport_km;
  p.climbRateMps = cfg->climb_rate_threshold_mps;
  p.lookupBudget = kDefaultLookupBudget;
  p.airportBudget = kDefaultAirportLookupBudget;

  memoAircraft.clear();
  memoRoute.clear();
  memoAirport.clear();
  const LookupPorts ports = productionPorts();

  // Phase A: positions now, from what is cached — no network.
  state.data = std::make_shared<const DataSnapshot>(
      buildDataSnapshot(poll.aircraft, cacheOnly(p), ports, polledAt));
  publishIfChanged();

  // Phase B: the new hexdb.io lookups; published only if it fetched anything
  // (also when a firmware update stopped it halfway — the board restarts anyway).
  const uint32_t t0 = millis();
  DataSnapshot b = buildDataSnapshot(poll.aircraft, p, ports, polledAt);
  hexdbSessionEnd();
  if (b.fetches > 0) {
    Serial.printf("[main] phase B: %u hexdb.io lookups in %u ms\n",
                  static_cast<unsigned>(b.fetches), static_cast<unsigned>(millis() - t0));
    state.data = std::make_shared<const DataSnapshot>(std::move(b));
    publishIfChanged();
  }
  memoAircraft.clear();
  memoRoute.clear();
  memoAirport.clear();
}

// ---- Perf (perf_stats) -----------------------------------------------------

BusyStats netWindow;
uint32_t netWindowStartMs = 0;
uint32_t netMaxEverMs = 0;
PerfReport lastReport;  // for /perf.json (served by this same task)

PerfReport buildReport() {
  PerfReport r;
  const UiPerfShare ui = core_link::uiPerf.get();
  r.ui.count = ui.windowCount;
  r.ui.maxMs = ui.windowMaxMs;
  for (int i = 0; i < kPerfGapBuckets; ++i) r.ui.buckets[i] = ui.buckets[i];
  r.uiMaxEverMs = ui.maxEverMs;
  r.uiSlowEver = ui.slowEver;
  r.uiStackFree = ui.stackFree;
  r.net = netWindow;
  r.netMaxEverMs = netMaxEverMs;
  r.netStackFree = uxTaskGetStackHighWaterMark(nullptr);
  r.freeHeap = ESP.getFreeHeap();
  r.maxBlock = ESP.getMaxAllocHeap();
  r.uptimeS = millis() / 1000;
  r.cacheAircraft = aircraftLookupCacheSize();
  r.cacheRoute = routeLookupCacheSize();
  r.cacheAirport = airportLookupCacheSize();
  return r;
}

void perfIteration(uint32_t busyMs) {
  const uint32_t now = millis();
  netWindow.addIteration(busyMs);
  if (busyMs > netMaxEverMs) netMaxEverMs = busyMs;
  if (netWindowStartMs == 0) netWindowStartMs = now;
  if (now - netWindowStartMs >= kPerfWindowMs) {
    netWindow.windowMs = now - netWindowStartMs;
    lastReport = buildReport();
    Serial.println(perfLogLine(lastReport).c_str());
    netWindow = BusyStats{};
    netWindowStartMs = now;
  }
}

std::string perfJsonNow() {
  // The current (incomplete) window for the network part would read 0 %;
  // the last complete minute is what's meaningful — refreshed totals on top.
  PerfReport r = lastReport;
  const PerfReport now = buildReport();
  r.uiMaxEverMs = now.uiMaxEverMs;
  r.uiSlowEver = now.uiSlowEver;
  r.netMaxEverMs = now.netMaxEverMs;
  r.freeHeap = now.freeHeap;
  r.maxBlock = now.maxBlock;
  r.uptimeS = now.uptimeS;
  r.cacheAircraft = now.cacheAircraft;
  r.cacheRoute = now.cacheRoute;
  r.cacheAirport = now.cacheAirport;
  return perfJson(r);
}

// ---- The task ----------------------------------------------------------------

void netTask(void *) {
  wifiManagerBegin();  // tries the saved network; falls back to its own captive portal
  timeSyncBegin();     // NTP (Europe/Warsaw); the header shows --:-- until it lands
  // mDNS + the config WebServer, non-blocking. In the portal-fallback case
  // cyd-sky.local won't resolve until the device joins a real network.
  configPortalBegin();
  seenSaveCount = configPortalSaveCount();

  OtaObserver otaObs;
  otaObs.onStart = otaOnStart;
  otaObs.onProgress = otaOnProgress;
  otaObs.onEnd = otaOnEnd;
  configPortalSetOtaObserver(otaObs);
  otaObs.onStart = githubOtaOnStart;
  releaseSetOtaObserver(otaObs);
  releaseCheckBegin(*cfg);
  configPortalSetPerfSource(perfJsonNow);
  publishIfChanged();

  for (;;) {
    const uint32_t t0 = millis();
    core_link::netBeatMs.store(t0);

    wifiManagerLoop();
    configPortalLoop();

    if (configPortalRestartPending()) {
      // A good firmware upload: the board restarts in a moment — off the network.
      publishIfChanged();
      vTaskDelay(pdMS_TO_TICKS(kNetIdleSleepMs));
      continue;
    }
    markAppValidWhenStable();

    // Settings saved on the config page: reload them now (language etc.).
    if (configPortalSaveCount() != seenSaveCount) {
      seenSaveCount = configPortalSaveCount();
      reloadConfig();
    }
    handleCommands();
    pollOpenSky();
    releaseCheckLoop(updateGate());  // an asked-for install / "check now" / a due check
    publishIfChanged();

    perfIteration(millis() - t0);
    vTaskDelay(pdMS_TO_TICKS(kNetIdleSleepMs));
  }
}

}  // namespace

void netTaskStart(std::shared_ptr<const Config> bootCfg, bool pendingVerify) {
  cfg = std::move(bootCfg);
  appPendingVerify = pendingVerify;
  state.cfg = cfg;
  xTaskCreatePinnedToCore(netTask, "net", kNetTaskStackBytes, nullptr, kNetTaskPriority, nullptr,
                          kNetTaskCore);
}

#endif  // ARDUINO
