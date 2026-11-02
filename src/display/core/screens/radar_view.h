// radar_view — draws Screen 3 ("Radar"), per docs/screens.md "Screen 3 —
// Radar": a left-hand radar plot (home at center, concentric distance
// rings, aircraft as blips positioned by bearing + distance) and, with no
// divider in between, a right-hand nearest-aircraft summary panel.
// Re-skinned in lcars_theme's palette — that's what
// distinguishes it from a plain radar scope, not the polar-plot math
// (which lives in radar_geometry, reused here rather than duplicated).
// Static plot for v1 — no sweep-line animation. Zero networking code:
// draws from an already-enriched, already-annotated AircraftRow list
// (bearing/distance come from tracked_flight's annotateDistances(), not
// recomputed here) plus the nearest aircraft's route-endpoint AirportInfos.
// Layout maths and texts are pure; the LovyanGFX drawing is
// display/adapters/lgfx/radar_view_draw.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "display/core/screens/radar_geometry.h"
#include "enrichment/core/route_lookup.h"  // AirportInfo
#include "airspace/core/feed_health.h"     // OpenSkyHealth
#include "airspace/core/tracked_flight.h"  // AircraftRow
#include "display/core/i18n.h"             // Language

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
// see docs/data-sources.md "Rate limits") to an approximate straight-line km value,
// for use as radar_geometry's max_distance_km — this is the "convert
// degrees to km at the call site, not inside radar_geometry" step
// radar_geometry's own header asks for. Uses the same ~111.2 km/degree
// approximation shared/geo's computeDistanceBearing() is built on; a
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
