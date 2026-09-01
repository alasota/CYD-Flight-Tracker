// radar_view — draws Screen 3 ("Radar"), per CLAUDE.md "Screen 3 —
// Radar": a left-hand radar plot (home at center, concentric distance
// rings, aircraft as blips positioned by bearing + distance), an
// LCARS_ORANGE vertical divider, and a right-hand nearest-aircraft
// summary panel. Re-skinned in lcars_theme's palette — that's what
// distinguishes it from a plain radar scope, not the polar-plot math
// (which lives in radar_geometry, reused here rather than duplicated).
// Static plot for v1 — no sweep-line animation. Zero networking code:
// draws from an already-enriched, already-annotated AircraftRow list
// (bearing/distance come from table_view::annotateDistances(), not
// recomputed here) plus the nearest aircraft's route-endpoint AirportInfos.
#pragma once

#include <cstdint>
#include <vector>

#include "radar_geometry.h"
#include "route_lookup.h"  // AirportInfo
#include "table_view.h"    // AircraftRow

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Center and radius (px) of the largest circle that fits within
// [x, y, w, h] with `margin` px of padding on every side — the single
// source of truth for where drawRadarView() places the plot. Screen 3
// calls this with the fixed radar zone (x:0..190, y:28..238) and a 10px
// margin, which resolves to center (95, 133) / radius 85 — the values
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

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// radar_geometry + aircraft_summary. Not covered by Unity (see CLAUDE.md
// "Testing"). Guarded here in the header too since its signature needs the
// LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws Screen 3's content area (below the 25px status_bar header) across
// a `screenWidth`-px display, at the fixed CLAUDE.md "Screen 3" layout:
//   - radar plot   x:0..190   — home crosshair (LCARS_YELLOW) at center
//     (95,133), 3 concentric rings (radar_geometry::computeRingDistances)
//     at r=28/57/85 with km labels, and a ~5px LCARS_CYAN blip per row
//     with a known distance/bearing (positioned via polarToScreen); a row
//     beyond `radius_deg` range is clamped to the outer ring and drawn
//     dimmer/smaller (radar_geometry::ScreenPoint::clamped);
//   - divider      x:192..200 — an 8px vertical LCARS_ORANGE bar;
//   - summary panel x:200..320 — an LCARS_MAGENTA elbow frame (swept
//     corner facing the divider) with three rows for the nearest aircraft
//     (rows.front()): callsign, airline, "WAW -> FCO" route (IATA only, no
//     country codes — RouteFormat::CodesOnly), via aircraft_summary.
// `rows` must already be annotated (annotateDistances()) and sorted
// (sortRowsByDistance()). `nearestOrigin`/`nearestDest` are rows.front()'s
// route-endpoint AirportInfos (pass AirportInfo{} if unresolved).
// Empty-range state (`rows` empty): the radar still draws its empty rings
// and home marker; the summary panel shows a centered "BRAK LOTU".
void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   const AirportInfo &nearestOrigin, const AirportInfo &nearestDest,
                   int16_t screenWidth);

#endif  // ARDUINO
