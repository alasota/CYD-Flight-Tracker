// table_view — draws Screen 1 (aircraft table), per CLAUDE.md
// "Visualization concept — Screen 1". Uses lcars_theme for all chrome
// (panels/pills/fonts/colors) — defines none of its own. Zero networking
// code, zero calls into opensky_client/aircraft_lookup: it takes their
// Aircraft/AircraftInfo *types* as plain data (joined by icao24 by
// whoever builds the AircraftRow list) and only draws.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "aircraft_lookup.h"
#include "lcars_theme.h"
#include "opensky_client.h"

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
// lookup by icao24 (both structs kept as-is, no duplicated fields), plus
// the distance/bearing from home once annotateDistances() has run.
struct AircraftRow {
  Aircraft aircraft;
  AircraftInfo info;
  float distance_km = 0.0f;
  float bearing_deg = 0.0f;
  bool has_distance = false;  // false when aircraft.has_position is false
};

// Joins OpenSky state vectors with their hexdb.io lookups by icao24,
// producing the AircraftRow list table_view actually draws — this is the
// "connect Aircraft + AircraftInfo" step CLAUDE.md's table_view input
// contract describes. An icao24 with no entry in `infoByIcao24` (lookup
// still pending, or main.cpp simply never called aircraft_lookup for it)
// produces a row with a default-constructed AircraftInfo (found == false),
// which drawTablePage() renders as "--" rather than dropping the row, per
// CLAUDE.md. Distance is NOT computed here — call annotateDistances() (and
// then sortRowsByDistance()) on the result afterward. Pure — no HTTP call
// happens here, just the join; no dependency on aircraft_lookup's actual
// lookup functions — tested under `pio test -e native`.
std::vector<AircraftRow> buildEnrichedRecords(const std::vector<Aircraft> &aircraft,
                                               const std::map<std::string, AircraftInfo> &infoByIcao24);

// Fills distance_km/bearing_deg/has_distance on every row in `rows` from
// (home_lat, home_lon) via computeDistanceBearing(). Rows whose
// Aircraft::has_position is false get has_distance=false (nothing to
// compute) and are left at distance_km=0/bearing_deg=0. Call this once
// before sortRowsByDistance()/drawTablePage() — both read the annotated
// fields rather than recomputing per call.
void annotateDistances(std::vector<AircraftRow> &rows, float home_lat, float home_lon);

// Sorts `rows` ascending by distance_km — closest aircraft first, per
// CLAUDE.md "sort by distance ascending (closest aircraft on top)". Call
// annotateDistances() first. Rows with has_distance == false sort last
// (nothing to rank them by); otherwise stable (equal distances keep their
// relative order).
void sortRowsByDistance(std::vector<AircraftRow> &rows);

// The fixed row height (pixels) this module draws at — the actual single
// source of truth backing rowsPerPage() below. Exposed so other modules
// (e.g. main.cpp's touch tap-zone sizing) can stay in sync with it instead
// of hardcoding their own independent copy of the same number — see
// CLAUDE.md review notes 5.5.
int16_t tableRowHeightPx();

// Rows that fit in a content area `contentHeight` pixels tall, at
// tableRowHeightPx(). Used by drawTablePage() below and by the paging
// logic that follows.
int rowsPerPage(int16_t contentHeight);

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

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too (like lcars_theme) since its signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws one page of `rows` (already annotated via annotateDistances() and
// sorted via sortRowsByDistance()) into the content area [x, y, w, h]:
// airline, flight (callsign), aircraft type, altitude, speed, distance —
// using lcars_theme for panels/fonts/colors, nothing of its own. `page` is
// 0-based (internally via getPageSlice()/rowsPerPage()); an out-of-range
// page just draws nothing. Row 0 of the *whole* sorted list (the closest
// aircraft) is highlighted with a brighter lcars_theme accent whenever it
// falls on the visible page. Missing lookup/position data renders as "--"
// rather than being hidden, per CLAUDE.md. Which page is current, and
// reacting to touch to change it, is main.cpp's job (touch_input) — this
// only lays out whichever page it's told to draw.
void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t x, int16_t y,
                    int16_t w, int16_t h, int page);

#endif  // ARDUINO
