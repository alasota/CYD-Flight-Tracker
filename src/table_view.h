// table_view — draws Screen 1 (aircraft table), per CLAUDE.md
// "Visualization concept — Screen 1". Composes featured_panel (the
// closest aircraft, spotlighted) with a paginated table of the rest. Uses
// lcars_theme for all chrome (panels/pills/fonts/colors) — defines none of
// its own. Zero networking code, zero calls into
// opensky_client/aircraft_lookup/route_lookup: it takes their
// Aircraft/AircraftInfo/RouteInfo *types* as plain data (joined by
// icao24/callsign by whoever builds the AircraftRow list) and only draws.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "aircraft_lookup.h"
#include "featured_panel.h"
#include "flight_phase.h"
#include "lcars_theme.h"
#include "opensky_client.h"
#include "route_lookup.h"

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Great-circle distance (km) and initial bearing (degrees, 0=north,
// clockwise) from (home_lat, home_lon) to (lat, lon), via the haversine
// formula on a spherical Earth — good enough for a "how far/which way"
// home tracker, not survey-grade. distance_km == 0 and bearing_deg == 0
// when the two points coincide (bearing is undefined at zero distance).
struct DistanceBearing {
  float distance_km = 0.0f;
  float bearing_deg = 0.0f;
};
DistanceBearing computeDistanceBearing(float home_lat, float home_lon, float lat, float lon);

// One enriched table row: an OpenSky state vector joined with its hexdb.io
// aircraft lookup (by icao24) and route lookup (by callsign) — all three
// structs kept as-is, no duplicated fields — plus the distance/bearing
// from home (annotateDistances()) and the classified flight phase
// (classifyPhases()) once those have run.
struct AircraftRow {
  Aircraft aircraft;
  AircraftInfo info;
  RouteInfo route;
  Phase phase = Phase::NONE;
  float distance_km = 0.0f;
  float bearing_deg = 0.0f;
  bool has_distance = false;  // false when aircraft.has_position is false
};

// Joins OpenSky state vectors with their hexdb.io aircraft lookups (by
// icao24) and route lookups (by callsign), producing the AircraftRow list
// table_view actually draws — the "connect Aircraft + AircraftInfo +
// RouteInfo" step CLAUDE.md's table_view input contract describes. A key
// with no entry in the corresponding map (lookup still pending, or
// main.cpp simply never called it for that row) produces a
// default-constructed AircraftInfo/RouteInfo (found == false), which
// drawTablePage()/featured_panel render as "--" rather than dropping the
// row, per CLAUDE.md. Distance and phase are NOT computed here — call
// annotateDistances() then classifyPhases() (then sortRowsByDistance()) on
// the result afterward. Pure — no HTTP call happens here, just the join —
// tested under `pio test -e native`.
std::vector<AircraftRow> buildEnrichedRecords(
    const std::vector<Aircraft> &aircraft, const std::map<std::string, AircraftInfo> &infoByIcao24,
    const std::map<std::string, RouteInfo> &routeByCallsign);

// Fills distance_km/bearing_deg/has_distance on every row in `rows` from
// (home_lat, home_lon) via computeDistanceBearing(). Rows whose
// Aircraft::has_position is false get has_distance=false (nothing to
// compute) and are left at distance_km=0/bearing_deg=0. Call this once
// before classifyPhases()/sortRowsByDistance()/drawTablePage() — all read
// the annotated fields rather than recomputing per call.
void annotateDistances(std::vector<AircraftRow> &rows, float home_lat, float home_lon);

// Fills row.phase on every row via flight_phase's classifyPhase(), using
// each row's own (already annotated) distance_km as the "distance to
// nearest airport" input — this project has no airport coordinate
// database, only country codes (route_lookup), so distance from HOME is
// what's actually available; see CLAUDE.md "Flight phase". Rows with
// has_distance == false get Phase::NONE (nothing meaningful to classify)
// rather than a misleading guess. Call after annotateDistances().
void classifyPhases(std::vector<AircraftRow> &rows, float near_airport_km,
                     float climb_threshold_mps);

// Sorts `rows` ascending by distance_km — closest aircraft first, per
// CLAUDE.md "sort by distance ascending (closest aircraft on top)". Call
// annotateDistances() first. Rows with has_distance == false sort last
// (nothing to rank them by); otherwise stable (equal distances keep their
// relative order).
void sortRowsByDistance(std::vector<AircraftRow> &rows);

// Result of splitting a (sorted) AircraftRow list into "the one closest
// aircraft, for featured_panel" and "everything else, for the table
// below it".
struct FeaturedSplit {
  bool hasFeatured = false;
  AircraftRow featured;           // valid only when hasFeatured
  std::vector<AircraftRow> rest;  // everything else, order preserved
};

// Splits `rows` (already sorted ascending by distance —
// sortRowsByDistance()) into its first element (the closest aircraft) and
// the remainder. Empty input -> hasFeatured=false, empty rest. A
// single-row input -> that row featured, rest empty. Pure.
FeaturedSplit splitFeaturedAndRest(const std::vector<AircraftRow> &rows);

// Screen 1's table shows **exactly 5** data rows at a fixed 18px step
// (rows at y = 140, 158, 176, 194, 212), per CLAUDE.md "Screen 1" — no
// height-derived row count any more. More than 5 aircraft in range are
// paged through via touch (getPageSlice()), not scrolled.
constexpr int kTableRowsPerPage = 5;

// Accessors so other modules (e.g. main.cpp's touch tap-zone sizing) stay
// in sync with these instead of hardcoding their own copies — see
// CLAUDE.md review notes 5.5.
int tableRowsPerPage();   // kTableRowsPerPage (5)
int16_t tableRowHeightPx();  // the 18px row step
int16_t tableFirstRowY();    // 140 — y of the first data row

// Number of pages needed to show `totalRows` records at `rowsPerPage` rows
// per page (ceiling division) — 0 records or a non-positive rowsPerPage
// both yield 0 pages, not 1. Pure — no TFT dependency.
int getPageCount(int totalRows, int rowsPerPage);

// The subset of `rows` to draw for `page` (0-based) at `rowsPerPage` rows
// per page. An out-of-range page (negative, or past the last page) returns
// an empty subset rather than out-of-bounds access — callers should check
// getPageCount() if they need to clamp `page` instead of just getting
// nothing back. Pure — no TFT dependency.
std::vector<AircraftRow> getPageSlice(const std::vector<AircraftRow> &rows, int page,
                                       int rowsPerPage);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// featured_panel. Not covered by Unity (see CLAUDE.md "Testing"). Guarded
// here in the header too (like lcars_theme/featured_panel) since its
// signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws Screen 1's content area (everything below the 25px status_bar
// header) across a `screenWidth`-px display, at the fixed coordinates
// CLAUDE.md "Screen 1" specifies:
//   - featured_panel for the single closest aircraft in `rows` (or its
//     "no aircraft in range" placeholder if `rows` is empty), y:28..105;
//   - a cyan "FLIGHTS" sub-header bar, then an orange "|"-separated column
//     header row at y:125;
//   - exactly kTableRowsPerPage (5) data rows from y:140 at an 18px step
//     — flight, airline, origin IATA, destination IATA, aircraft type,
//     phase icon — for everyone *except* the featured aircraft.
// `rows` must already be annotated (annotateDistances()), classified
// (classifyPhases()), and sorted (sortRowsByDistance()).
// `originAirport`/`destAirport` are the featured aircraft's route
// endpoints' AirportInfo (pass AirportInfo{} if not resolved). `page` is
// 0-based over the *rest* of the list (the featured row is never repeated
// in the table); an out-of-range page draws no data rows. Which page is
// current, and reacting to touch, is main.cpp's job — this only lays out
// whichever page it's told to draw.
void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, const AirportInfo &originAirport,
                    const AirportInfo &destAirport, int16_t screenWidth, int page);

#endif  // ARDUINO
