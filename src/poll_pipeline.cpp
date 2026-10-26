#include "poll_pipeline.h"

#include <map>

#include "cpa_predictor.h"
#include "flight_screen.h"  // pickNowAircraft(), kOverheadRadiusKm
#include "lookup_budget.h"
#include "table_view.h"

// TODO(spec 09): stub from test-author
bool serveBetweenFetches(bool & /*busy*/, const std::function<void()> & /*serve*/,
                         const std::function<bool()> & /*updateStarted*/) {
  return true;
}

PipelineParams cacheOnly(PipelineParams p) {
  p.lookupBudget = 0;
  p.airportBudget = 0;
  return p;
}

DataSnapshot buildDataSnapshot(const std::vector<Aircraft> &aircraft, const PipelineParams &p,
                               const LookupPorts &ports, uint32_t polledAtMs) {
  DataSnapshot s;
  s.polledAtMs = polledAtMs;
  std::vector<AircraftRow> &rows = s.rows;

  // Rank aircraft (airborne nearest-first, then ground) so the limited per-poll
  // lookup budget is spent on the ones the user actually sees first.
  rows = buildEnrichedRecords(aircraft, {}, {});
  annotateDistances(rows, p.homeLat, p.homeLon);
  sortRowsForDisplay(rows);  // airborne first, then ground traffic (#9)

  std::vector<LookupKeys> byPriority;
  byPriority.reserve(rows.size());
  for (const AircraftRow &r : rows) byPriority.push_back({r.aircraft.icao24, r.aircraft.callsign});
  const LookupPlan plan =
      planLookups(byPriority, ports.aircraftKnown, ports.routeKnown, p.lookupBudget);

  std::map<std::string, AircraftInfo> infoByIcao24;
  for (const std::string &hex : plan.icao24) {
    if (!ports.aircraftKnown(hex)) ++s.fetches;
    infoByIcao24[hex] = ports.aircraft(hex);
  }
  std::map<std::string, RouteInfo> routeByCallsign;
  for (const std::string &cs : plan.callsigns) {
    if (!ports.routeKnown(cs)) ++s.fetches;
    routeByCallsign[cs] = ports.route(cs);
  }

  // Fold the enrichment into the already-sorted rows in place — it doesn't
  // change distance, so no re-sort needed. A key the budget didn't reach
  // this cycle is *pending* ("..."), not unknown ("--").
  for (AircraftRow &r : rows) {
    auto info = infoByIcao24.find(r.aircraft.icao24);
    if (info != infoByIcao24.end()) {
      r.info = info->second;
    } else {
      r.info_pending = !r.aircraft.icao24.empty();
    }
    auto route = routeByCallsign.find(r.aircraft.callsign);
    if (route != routeByCallsign.end()) {
      r.route = route->second;
    } else {
      r.route_pending = !r.aircraft.callsign.empty();
    }
  }
  classifyPhases(rows, p.nearAirportKm, p.climbRateMps);

  // Route-endpoint airports for every row with a route, so the whole UI
  // shows IATA codes. Cache hits are free; new lookups are budgeted
  // (planLookups, airport codes in the icao24 slot), nearest rows first.
  std::vector<LookupKeys> airportKeys;
  for (const AircraftRow &r : rows) {
    if (!r.route.found) continue;
    airportKeys.push_back({r.route.origin_icao, ""});
    airportKeys.push_back({r.route.dest_icao, ""});
  }
  const LookupPlan airportPlan =
      planLookups(airportKeys, ports.airportKnown, ports.routeKnown, p.airportBudget);
  std::map<std::string, AirportInfo> airportByCode;
  for (const std::string &code : airportPlan.icao24) {
    if (!ports.airportKnown(code)) ++s.fetches;
    airportByCode[code] = ports.airport(code);
  }
  for (AircraftRow &r : rows) {
    if (!r.route.found) continue;
    auto o = airportByCode.find(r.route.origin_icao);
    if (o != airportByCode.end()) r.origin_airport = o->second;
    else r.origin_airport_pending = true;
    auto d = airportByCode.find(r.route.dest_icao);
    if (d != airportByCode.end()) r.dest_airport = d->second;
    else r.dest_airport_pending = true;
  }

  // Screen 2 ("TERAZ" / "NOW"): every aircraft's closest point of approach;
  // the one overhead soonest is shown (the UI ticks its countdown).
  std::vector<CpaPrediction> cpas(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    const Aircraft &ac = rows[i].aircraft;
    if (!ac.has_position) continue;
    cpas[i] = predictCpa(p.homeLat, p.homeLon, ac.lat, ac.lon, ac.velocity, ac.true_track);
  }
  const NowPick now = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  s.nowRowIndex = now.index;
  s.cpa = now.cpa;
  return s;
}
