// poll_pipeline — turns one /states/all answer into what the screens show
// (spec/done/04-dual-core.md §5.4). Moved out of main.cpp's loop(), where it had no
// test at all.
//
// Steps (unchanged from the single-core firmware): rows nearest-first
// (airborne, then ground, then no position), hexdb.io airline/type + route
// for as many rows as the per-poll budget allows (cache hits are free),
// flight phases, IATA codes for the route airports (own budget), every
// aircraft's closest point of approach and Screen 2's pick.
//
// Two phases, so positions don't wait for hexdb.io:
//   phase A = buildDataSnapshot(..., budgets 0): no network at all — only what
//             is cached (RAM or NVS); the rest is *_pending ("...").
//   phase B = buildDataSnapshot(..., full budgets): also the new lookups.
//   B is published only when it fetched something (`fetches` > 0); otherwise
//   it equals A and there is nothing to repaint.
//
// Pure: the lookups are injected (LookupPorts) — production wires in
// aircraft_lookup / route_lookup / lookup_store, the native tests stubs.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aircraft_lookup.h"
#include "core_link.h"  // DataSnapshot
#include "opensky_client.h"
#include "route_lookup.h"

struct LookupPorts {
  // "Cached" = answered without a hexdb.io request (RAM or NVS).
  std::function<bool(const std::string &)> aircraftKnown;
  std::function<bool(const std::string &)> routeKnown;
  std::function<bool(const std::string &)> airportKnown;
  // The lookups themselves; for a known key they don't touch the network.
  std::function<AircraftInfo(const std::string &icao24)> aircraft;
  std::function<RouteInfo(const std::string &callsign)> route;
  std::function<AirportInfo(const std::string &icaoCode)> airport;
};

struct PipelineParams {
  float homeLat = 0.0f;
  float homeLon = 0.0f;
  float nearAirportKm = 15.0f;
  float climbRateMps = 1.5f;
  int lookupBudget = 0;   // new airline/type + route lookups (shared)
  int airportBudget = 0;  // new airport lookups
};

// One snapshot from `aircraft`. `polledAtMs` = when /states/all answered
// (the countdown's base). `fetches` in the result counts the lookups that
// were not cached — i.e. the hexdb.io requests this call made.
DataSnapshot buildDataSnapshot(const std::vector<Aircraft> &aircraft, const PipelineParams &p,
                               const LookupPorts &ports, uint32_t polledAtMs);

// Phase A's params: the same, with no new lookups allowed.
PipelineParams cacheOnly(PipelineParams p);
