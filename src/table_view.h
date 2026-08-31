// table_view — draws Screen 1 (aircraft table), per CLAUDE.md
// "Visualization concept — Screen 1". Uses lcars_theme for all chrome
// (panels/pills/fonts/colors) — defines none of its own. Zero networking
// code, zero calls into opensky_client/aircraft_lookup: it takes their
// Aircraft/AircraftInfo *types* as plain data (joined by icao24 by
// whoever builds the AircraftRow list) and only draws.
#pragma once

#include <cstdint>
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

// Rows that fit in a content area `contentHeight` pixels tall, at this
// module's fixed row height — the single source of truth for row height,
// used by drawTablePage() below and by (step 9's) touch paging logic.
int rowsPerPage(int16_t contentHeight);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too (like lcars_theme) since its signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws one page of `rows` (already annotated via annotateDistances() and
// sorted via sortRowsByDistance()) into the content area [x, y, w, h]:
// airline, flight (callsign), aircraft type, altitude, speed, distance —
// using lcars_theme for panels/fonts/colors, nothing of its own. `page` is
// 0-based; only the rows that fit starting at page*rowsPerPage(h) are
// drawn. Row 0 of the *whole* sorted list (the closest aircraft) is
// highlighted with a brighter lcars_theme accent whenever it falls on the
// visible page. Missing lookup/position data renders as "--" rather than
// being hidden, per CLAUDE.md. Paging/scrolling via touch is step 9 — this
// only lays out whichever page it's told to draw.
void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t x, int16_t y,
                    int16_t w, int16_t h, int page);

#endif  // ARDUINO
