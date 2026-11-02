// featured_panel — Screen 1's top zone (y:18..95), spotlighting the
// nearest airborne aircraft; text and stat chips on black, no frame. The
// identity text comes from aircraft_summary (identity line + route line); this
// module adds only its own altitude / speed / distance stat chips and the
// "no aircraft in range" placeholder. See docs/screens.md "Screen 1 — Flights".
//
// Zero networking code — draws from an already-enriched AircraftRow
// (airspace/core/tracked_flight) plus the two AirportInfo results for its route's
// endpoints, looked up separately for only the one featured row. The pill
// texts are pure; the LovyanGFX drawing is display/adapters/lgfx/featured_panel_draw.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "airspace/core/tracked_flight.h"  // AircraftRow
#include "display/core/screens/aircraft_summary.h"  // RouteFormat, the identity/route formatters
#include "display/core/theme/lcars_theme.h"
#include "enrichment/core/route_lookup.h"  // AirportInfo
#include "display/core/screens/status_bar.h"    // OpenSkyHealth, emptyStateText()

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed vertical bounds of the featured panel on the 320x240 frame, per
// docs/screens.md "Screen 1" (featured flight section, y:18..95).
// table_view starts its table section immediately below.
constexpr int16_t kFeaturedPanelTop = LCARS_CONTENT_TOP;  // 18
constexpr int16_t kFeaturedPanelHeight = 77;  // y:18..95 per docs/screens.md "Screen 1"
int16_t featuredPanelTopPx();     // 18
int16_t featuredPanelHeightPx();  // 77 (18..95)

// Texts for the stat pills under the featured aircraft: airborne ->
// {"<alt>m", "<speed>km/h", "<compass> <dist>km"}; on the ground -> {"NA ZIEMI" /
// "ON GROUND", "<dist>km"} (altitude and speed mean nothing while taxiing,
// and "0m" / "1m/s" read like a bug — the first pill is drawn double-width).
// A missing value is "--", never a fake 0. Pure — native-tested.
std::vector<std::string> featuredPillTexts(const AircraftRow &row, Language lang);
