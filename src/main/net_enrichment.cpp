#include "main/net_enrichment.h"

#include <map>
#include <string>

#include "admin/adapters/web_server.h"
#include "admin/core/firmware/ota_update.h"
#include "enrichment/core/aircraft_lookup.h"
#include "enrichment/core/lookup_store.h"
#include "enrichment/core/route_lookup.h"

namespace {

// The lookups for poll_pipeline. Per poll, results are memoised so phase B
// doesn't repeat (or re-log) what phase A already looked up.
std::map<std::string, AircraftInfo> memoAircraft;
std::map<std::string, RouteInfo> memoRoute;
std::map<std::string, AirportInfo> memoAirport;

}  // namespace

EnrichmentPort productionPorts() {
  EnrichmentPort p;
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
    return serveBetweenFetches(inBetweenFetches, webServerLoop, [] {
      return webServerRestartPending() || otaFlashSession().inProgress();
    });
  };
  return p;
}

void enrichmentClearMemos() {
  memoAircraft.clear();
  memoRoute.clear();
  memoAirport.clear();
}
