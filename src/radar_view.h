// radar_view — draws Screen 3 ("Radar"), per CLAUDE.md "Screen 3 —
// Radar": a left-hand radar plot (home at center, concentric distance
// rings, aircraft as blips positioned by bearing + distance) and, with no
// divider in between, a right-hand nearest-aircraft summary panel.
// Re-skinned in lcars_theme's palette — that's what
// distinguishes it from a plain radar scope, not the polar-plot math
// (which lives in radar_geometry, reused here rather than duplicated).
// Static plot for v1 — no sweep-line animation. Zero networking code:
// draws from an already-enriched, already-annotated AircraftRow list
// (bearing/distance come from table_view::annotateDistances(), not
// recomputed here) plus the nearest aircraft's route-endpoint AirportInfos.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "radar_geometry.h"
#include "route_lookup.h"  // AirportInfo
#include "table_view.h"    // AircraftRow

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Center and radius (px) of the largest circle that fits within
// [x, y, w, h] with `margin` px of padding on every side — the single
// source of truth for where drawRadarView() places the plot. Screen 3
// calls this with the fixed radar zone (x:0..190, LCARS_CONTENT_TOP..BOTTOM)
// and a 10px margin, which resolves to center (95, 117) / radius 85 — the values
// radar_geometry's polarToScreen()/ring math is exercised with.
struct RadarLayout {
  int16_t center_x = 0;
  int16_t center_y = 0;
  int16_t radius_px = 0;  // 0 if the area is too small to fit any circle
};
RadarLayout computeRadarLayout(int16_t x, int16_t y, int16_t w, int16_t h, int16_t margin);

// Converts a scan radius in degrees (config_store's Config::radius_deg —
// see CLAUDE.md "Rate limits") to an approximate straight-line km value,
// for use as radar_geometry's max_distance_km — this is the "convert
// degrees to km at the call site, not inside radar_geometry" step
// radar_geometry's own header asks for. Uses the same ~111.2 km/degree
// approximation table_view's computeDistanceBearing() is built on; a
// rough, direction-independent estimate good enough for sizing a radar
// ring, not survey-grade.
float radiusDegToKm(float radius_deg);

// A point on the radar plot (screen px).
struct RadarPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// Where a range ring's "28km" label goes: on the ring at 45 degrees up and
// to the right of centre — not at 12 o'clock, where aircraft north of home
// kept covering it. Pure — native-tested.
RadarPoint ringLabelAnchor(int16_t cx, int16_t cy, int16_t radiusPx);

// End of the short heading tick drawn from a blip at (x, y) along the
// aircraft's track (degrees, 0 = north/up, clockwise), `len` px long. Pure.
RadarPoint headingTickEnd(int16_t x, int16_t y, float trackDeg, int16_t len);

// Stats line under the route in the summary panel: compass + distance and
// altitude — "S 4km  511m" — or "S 4km  NA ZIEMI" on the ground. Pure.
std::string radarSummaryStats(const AircraftRow &row, Language lang);

// (Airline truncation for the narrow summary panel uses lcars_theme's
// fitToWidth(), measured in pixels — the shared helper all screens use.)

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// radar_geometry + aircraft_summary. Not covered by Unity (see CLAUDE.md
// "Testing"). Guarded here in the header too since its signature needs the
// LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws Screen 3's content area (LCARS_CONTENT_TOP..BOTTOM) across a
// `screenWidth`-px display, at the fixed CLAUDE.md "Screen 3" layout (the
// k* constants in radar_view.cpp):
//   - radar plot   x:0..190   — home crosshair (LCARS_YELLOW) at center
//     (95,117), 3 concentric rings (radar_geometry::computeRingDistances)
//     at r=28/57/85 with km labels, and a sky (THEME_CONTENT_SECONDARY)
//     blip with a heading tick per airborne row with a known
//     distance/bearing (positioned via polarToScreen), a small muted square
//     for ground traffic; a row beyond `radius_deg` range is clamped to the
//     outer ring and drawn dimmer/smaller
//     (radar_geometry::ScreenPoint::clamped); the summary panel's aircraft
//     gets a white ring;
//   - summary panel x:200..320, text from x:208 (no frame, no divider) with
//     five rows for the nearest aircraft (rows.front()): callsign, airline,
//     origin city, "-> destination" city, compass + distance + altitude.
// `rows` must already be annotated (annotateDistances()) and sorted
// (sortRowsByDistance()).
// Empty-range state (`rows` empty): the radar still draws its empty rings
// and home marker; the summary panel shows a centered "BRAK LOTU" — or, when
// the feed failed, why (emptyStateText(health, short form)).
void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   int16_t screenWidth, OpenSkyHealth health);

#endif  // ARDUINO
