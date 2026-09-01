// radar_view — draws Screen 2 (LCARS-skinned radar plot), per CLAUDE.md
// "Screen 2 — Radar": home at center, concentric distance rings, aircraft
// as blips positioned by bearing + distance. Re-skinned in lcars_theme's
// palette — see CLAUDE.md "Design language" — that's what distinguishes
// it from a plain radar scope, not the underlying polar-plot math (which
// lives in radar_geometry, reused here rather than duplicated). Static
// plot for v1 — no sweep-line animation. Zero networking code: draws from
// an already-enriched, already-annotated AircraftRow list (table_view's
// type) — bearing/distance come from table_view::annotateDistances()'s
// computeDistanceBearing() call, not recomputed here.
#pragma once

#include <cstdint>
#include <vector>

#include "radar_geometry.h"
#include "table_view.h"  // AircraftRow

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Center and radius (px) of the largest circle that fits within
// [x, y, w, h] with `margin` px of padding on every side — the single
// source of truth for where drawRadarView() below places the plot.
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
// radar_geometry. Not covered by Unity (see CLAUDE.md "Testing"). Guarded
// here in the header too (like table_view/featured_panel) since its
// signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws Screen 2 into the content area [x, y, w, h]: a home marker at the
// plot's center (computeRadarLayout()), concentric distance rings
// (radar_geometry::computeRingDistances()) in lcars_theme colors with
// distance labels, and a blip for every row with a known distance/bearing
// (AircraftRow::has_distance — annotateDistances() must have already run
// on `rows`, and bearing/distance are read from the row rather than
// recomputed here). `radius_deg` is config_store's scan radius, converted
// internally via radiusDegToKm(). A row whose distance exceeds that range
// is clamped to the outer ring and drawn dimmed (smaller, outline-only,
// paler color) rather than hidden or drawn off-canvas — see
// radar_geometry::ScreenPoint::clamped. Uses lcars_theme for all colors,
// nothing of its own. No sweep-line animation (static plot, per CLAUDE.md
// "fine for v1").
void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg, int16_t x,
                    int16_t y, int16_t w, int16_t h);

#endif  // ARDUINO
