// featured_panel — the top-of-screen panel spotlighting the closest
// aircraft: flight/airline/type + phase icon on line 1, origin->dest
// route (with country) on line 2, plus altitude/speed/distance as
// lcars_theme pills. See CLAUDE.md "Screen 1". Zero networking code —
// draws from an already-enriched AircraftRow (table_view's type) plus the
// two AirportInfo results for its route's endpoints, looked up separately
// by main.cpp for only the one featured row (not worth carrying on every
// AircraftRow when only one is ever shown here). A single placeholder
// view covers "no aircraft in range".
#pragma once

#include <string>

#include "lcars_theme.h"
#include "route_lookup.h"  // RouteInfo, AirportInfo

// Forward-declared rather than #include "table_view.h" — table_view.h
// includes this header (to call the draw functions below), so including
// it back here would cycle. Only a reference to AircraftRow is needed in
// these signatures, which a forward declaration covers; featured_panel.cpp
// includes table_view.h itself for the full definition.
struct AircraftRow;

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed height (pixels) of the featured panel — the single source of
// truth table_view uses to reserve space for it above the aircraft table.
int16_t featuredPanelHeightPx();

// "<flight>  <airline>  <type>  <phase icon>" — line 1 of the featured
// panel. Falls back to "--" for flight/airline/type exactly like
// table_view's row rendering, per CLAUDE.md ("show the row anyway ...
// as '—'").
std::string formatFeaturedLine1(const AircraftRow &row);

// "<origin> (<country>) -> <dest> (<country>)" — line 2 of the featured
// panel. "--" for the whole line when `route` hasn't resolved
// (RouteInfo::found == false). When it has, each end falls back to its
// bare route code (no country) if that end's AirportInfo hasn't resolved
// — an unresolved airport lookup shouldn't hide an otherwise-known route.
std::string formatFeaturedLine2(const RouteInfo &route, const AirportInfo &originAirport,
                                 const AirportInfo &destAirport);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too (like table_view/lcars_theme) since these signatures need the LGFX
// device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the featured panel for `row` into [x, y, w, h]: line 1, line 2,
// and altitude/speed/distance as lcars_theme pills. Uses lcars_theme for
// all chrome, nothing of its own.
void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                        const AirportInfo &destAirport, int16_t x, int16_t y, int16_t w,
                        int16_t h);

// Draws the "no aircraft in range" placeholder for the same panel area,
// when there's nothing to feature.
void drawFeaturedPanelEmpty(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h);

#endif  // ARDUINO
