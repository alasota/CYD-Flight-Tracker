// radar_geometry — pure polar-to-screen math for Screen 2's radar plot
// (see CLAUDE.md "Screen 2 — Radar"): home at center, aircraft as blips
// positioned by bearing + distance. Zero LovyanGFX/Arduino dependency —
// tested under `pio test -e native`. A future `radar_view` module is
// expected to call into this the way `table_view` calls into
// `lcars_theme`/`featured_panel`.
//
// `max_distance_km` is meant to come from config_store's configured scan
// radius (Config::radius_deg) — converting that from degrees to km is the
// caller's job, not this module's, so it stays purely geometric with no
// opinion on units conversion or where the radius comes from.
#pragma once

#include <cstdint>
#include <vector>

// A point on the radar canvas, plus whether it had to be clamped to the
// outer ring.
struct ScreenPoint {
  int16_t x = 0;
  int16_t y = 0;
  // True if distance_km exceeded max_distance_km and the point was
  // clipped to the ring's edge instead of drawn outside the canvas — the
  // aircraft is "off the scale" in that direction, not literally at this
  // position, so a caller should render it distinctly (e.g. dimmed or
  // smaller) rather than as an ordinary in-range blip.
  bool clamped = false;
};

// Converts a bearing+distance from home into a screen coordinate for a
// circular radar plot centered at (center_x, center_y) with radius
// radius_px. Compass convention: bearing_deg 0 = north/up, increasing
// clockwise (standard compass azimuth) — matches
// table_view::computeDistanceBearing()'s bearing_deg output, so its result
// can be fed straight in.
//
// distance_km is scaled linearly against max_distance_km: distance_km ==
// max_distance_km lands exactly on the ring's edge (radius_px from
// center); distance_km == 0 lands exactly at the center (home). A
// distance_km beyond max_distance_km is clamped to the edge — see
// ScreenPoint::clamped — rather than drawn outside the canvas. A
// non-positive max_distance_km (shouldn't happen given config_store's
// radius clamp, but handled defensively) collapses everything to the
// center rather than dividing by zero.
ScreenPoint polarToScreen(float bearing_deg, float distance_km, float max_distance_km,
                          int16_t center_x, int16_t center_y, int16_t radius_px);

// Distances (km) for `ring_count` concentric radar rings, evenly spaced
// from the center out to max_distance_km — ring i (0-based in the
// returned vector) is at max_distance_km * (i+1) / ring_count, so the
// last ring always lands exactly on max_distance_km. Used to draw radar
// rings with distance labels. ring_count <= 0 yields an empty result.
std::vector<float> computeRingDistances(float max_distance_km, int ring_count);
