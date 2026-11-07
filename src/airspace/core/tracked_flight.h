// tracked_flight — one aircraft in range as every screen shows it
// (TrackedFlight): its state joined with the hexdb.io airline / type and
// route, plus the distance and bearing from home and the flight phase
// (docs/screens.md "Screen 1 — Flights"). poll_pipeline builds the list;
// the screens only draw it.
//
// Pure — no HTTP call, no drawing — tested under `pio test -e native`.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "airspace/core/aircraft_state.h"     // Aircraft
#include "airspace/core/flight_phase.h"       // Phase
#include "enrichment/core/aircraft_lookup.h"  // AircraftInfo
#include "enrichment/core/route_lookup.h"     // RouteInfo, AirportInfo

// One tracked flight (a table row on Screen 1): an aircraft state joined
// with its hexdb.io aircraft lookup (by icao24) and route lookup (by
// callsign) — all three
// structs kept as-is, no duplicated fields — plus the distance/bearing
// from home (annotateDistances()) and the classified flight phase
// (classifyPhases()) once those have run.
struct TrackedFlight {
  Aircraft aircraft;
  AircraftInfo info;
  RouteInfo route;
  Phase phase = Phase::NONE;
  float distance_km = 0.0f;
  float bearing_deg = 0.0f;
  bool has_distance = false;  // false when aircraft.has_position is false

  // "Still loading" vs "confirmed unknown": a lookup that the per-poll
  // budget hasn't reached yet is *pending* (shown as "..."); a lookup that
  // ran and found nothing is not (shown as "--"). Set by poll_pipeline.
  bool info_pending = false;   // aircraft_lookup (airline / type)
  bool route_pending = false;  // route_lookup (origin / destination)

  // Route endpoint airports (IATA code + country) — looked up for every row
  // with a route, so the whole UI can show IATA codes. A pending flag means
  // that airport's lookup hasn't run yet this cycle.
  AirportInfo origin_airport;
  AirportInfo dest_airport;
  bool origin_airport_pending = false;
  bool dest_airport_pending = false;

  // Copy, move and destructor are defined (= default) in tracked_flight.cpp,
  // not inline: a row holds many strings, and inlining its copy at every
  // call site in every translation unit costs ~1 KB of flash.
  TrackedFlight() = default;
  TrackedFlight(const TrackedFlight &other);
  TrackedFlight(TrackedFlight &&other) noexcept;
  TrackedFlight &operator=(const TrackedFlight &other);
  TrackedFlight &operator=(TrackedFlight &&other) noexcept;
  ~TrackedFlight();
};

// Joins the aircraft states with their hexdb.io aircraft lookups (by
// icao24) and route lookups (by callsign), producing the TrackedFlight list
// every screen draws — the "connect Aircraft + AircraftInfo + RouteInfo"
// step (CLAUDE.md "Code conventions"). A key
// with no entry in the corresponding map (lookup still pending, or
// poll_pipeline simply never ran it for that row) produces a
// default-constructed AircraftInfo/RouteInfo (found == false), which
// drawTablePage()/featured_panel render as "--" rather than dropping the
// row (docs/data-sources.md "Aircraft identification"). Distance and phase
// are NOT computed here — call annotateDistances() then classifyPhases()
// (then sortRowsByDistance()) on the result afterward. Pure — no HTTP call
// happens here, just the join — tested under `pio test -e native`.
std::vector<TrackedFlight> buildEnrichedRecords(
    const std::vector<Aircraft> &aircraft, const std::map<std::string, AircraftInfo> &infoByIcao24,
    const std::map<std::string, RouteInfo> &routeByCallsign);

// Fills distance_km/bearing_deg/has_distance on every row in `rows` from
// (home_lat, home_lon) via shared/geo's computeDistanceBearing(). Rows whose
// Aircraft::has_position is false get has_distance=false (nothing to
// compute) and are left at distance_km=0/bearing_deg=0. Call this once
// before classifyPhases()/sortRowsByDistance()/drawTablePage() — all read
// the annotated fields rather than recomputing per call.
void annotateDistances(std::vector<TrackedFlight> &rows, float home_lat, float home_lon);

// Fills row.phase on every row via flight_phase's classifyPhase(), using
// each row's own (already annotated) distance_km as the "distance to
// nearest airport" input — this project has no airport coordinate
// database, only country codes (route_lookup), so distance from HOME is
// what's actually available; see docs/screens.md "Flight phase". Rows with
// has_distance == false get Phase::NONE (nothing meaningful to classify)
// rather than a misleading guess. Call after annotateDistances().
void classifyPhases(std::vector<TrackedFlight> &rows, float near_airport_km,
                     float climb_threshold_mps);
