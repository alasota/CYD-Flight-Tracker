// radar_view_draw — the LovyanGFX half of radar_view: Screen 3's radar plot
// and nearest-aircraft summary (docs/screens.md "Screen 3"), drawn from
// radar_view's and radar_geometry's pure maths. Hardware adapter, not covered
// by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>
#include <vector>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/radar_view.h"

// Draws Screen 3's content area (LCARS_CONTENT_TOP..BOTTOM) across a
// `screenWidth`-px display, at the fixed docs/screens.md "Screen 3" layout (the
// k* constants in radar_view_draw.cpp):
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
// (sortRowsByDistance(), flight_ordering).
// Empty-range state (`rows` empty): the radar still draws its empty rings
// and home marker; the summary panel shows a centered "BRAK LOTU" — or, when
// the feed failed, why (emptyStateText(health, short form)).
void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   int16_t screenWidth, FeedHealth health);
