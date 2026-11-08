#include "main/net_perf.h"

#include <Arduino.h>

#include "enrichment/core/aircraft_lookup.h"  // aircraftLookupCacheSize()
#include "enrichment/core/route_lookup.h"     // routeLookupCacheSize(), airportLookupCacheSize()
#include "platform/core_link.h"
#include "platform/perf_stats.h"

namespace {

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

}  // namespace

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
