// aircraft_summary — the shared aircraft identity block (flight / airline /
// aircraft type / phase icon, plus origin -> destination) rendered the
// same way on all three screens, per CLAUDE.md "Code conventions"
// (aircraft_summary) and "Visualization concept". Centralized here so this
// isn't copy-pasted into featured_panel (Screen 1), flight_screen
// (Screen 2) and radar_view (Screen 3).
//
// The one thing that differs between screens is how much of the route to
// show — Screen 1 & 2 want country codes ("WAW (PL) -> FCO (IT)"),
// Screen 3's narrower panel wants bare codes ("WAW -> FCO"). That's a
// RouteFormat parameter, not three separate functions.
//
// Zero networking code — draws from an already-enriched AircraftRow plus
// the two AirportInfo results for its route endpoints (looked up
// elsewhere, exactly as featured_panel already receives them).
#pragma once

#include <string>

#include "lcars_theme.h"   // TextMeasure, fitToWidth()
#include "route_lookup.h"  // RouteInfo, AirportInfo

// Forward-declared rather than #include "table_view.h" (which pulls in a
// lot and would risk an include cycle) — only a reference to AircraftRow
// is needed in these signatures. aircraft_summary.cpp includes
// table_view.h itself for the full definition. Same pattern as
// featured_panel.h.
struct AircraftRow;

// How the origin -> destination line is rendered.
enum class RouteFormat {
  WithCountry,  // "WAW (PL) -> FCO (IT)"  — Screen 1 (featured) & Screen 2
  CodesOnly,    // "WAW -> FCO"            — Screen 3's narrow summary panel
};

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Placeholders: "..." = lookup not run yet (still loading), "--" = looked up,
// nothing known.
constexpr const char *kPendingText = "...";
constexpr const char *kUnknownText = "--";

// Individual identity fields. Airline/type show "..." while the aircraft
// lookup is pending (row.info_pending), "--" when it resolved without that
// field — same "show the row anyway" rule the rest of the UI uses.
std::string summaryFlight(const AircraftRow &row);   // callsign
std::string summaryAirline(const AircraftRow &row);  // RegisteredOwners
std::string summaryType(const AircraftRow &row);     // aircraft type

// The one-line identity "<flight>  <airline>  <type>" used by Screen 1's
// featured panel and Screen 2 (Screen 3 draws the fields on separate rows
// with the helpers above), cut to fit `maxPx`. No phase marker: the phase
// icon is drawn separately at a fixed right-aligned spot so it can never be
// pushed off-screen (docs/screens.md "Screen 1" featured panel). Shrinks in this
// order until it fits:
//   1. "<flight>  <airline>  <type>"   unchanged if it fits;
//   2. airline cut with "..." to the remaining width;
//   3. airline dropped ("<flight>  <type>") if not even "X..." fits;
//   4. type cut to the remaining width, or dropped;
//   5. "<flight>" alone — the callsign is never truncated, even if it
//      alone overflows.
std::string fitSummaryIdentity(const AircraftRow &row, int maxPx, const TextMeasure &measure);

// "<origin> -> <dest>", each end formatted per `fmt`:
//   WithCountry: "<IATA> (<country>)", e.g. "WAW (PL)"
//   CodesOnly:   "<IATA>",             e.g. "WAW"
// If `route` hasn't resolved (RouteInfo::found == false) the whole line is
// "--". If a route resolved but that end's airport lookup didn't, that end
// falls back to its bare ICAO code (from RouteInfo) with no country — an
// unresolved airport shouldn't hide an otherwise-known route.
std::string formatSummaryRoute(const RouteInfo &route, const AirportInfo &originAirport,
                               const AirportInfo &destAirport, RouteFormat fmt);

// One route endpoint of `row` as shown in the table — the IATA code when the
// airport is known; "..." while the route or that airport is still loading;
// the ICAO code if hexdb.io has no airport record (better than nothing);
// "--" when the flight has no known route.
std::string rowAirportCode(const AircraftRow &row, bool origin);

// The route line for `row` using its own airports (row.origin_airport /
// dest_airport) — "WAW (PL) -> FCO (IT)" / "WAW -> FCO"; "..." while the
// route is loading, "--" when there is none; a still-loading endpoint shows
// as "...".
std::string formatRowRoute(const AircraftRow &row, RouteFormat fmt);

// One route endpoint of `row` as a readable place name in `lang`
// (place_names::placeName(): "Warszawa", "Londyn", "Rome-Fiumicino"...);
// "..." while loading, "--" without a route.
std::string rowPlaceName(const AircraftRow &row, bool origin, Language lang);

// The detail views' route line with city names, fitted to `maxPx`:
//   1. "Warszawa (PL) -> Rzym (IT)"  if it fits;
//   2. "Warszawa -> Rzym"            (country codes dropped);
//   3. both names cut to half the remaining width each.
// "..." / "--" for a loading / missing route, "..." for a loading endpoint.
std::string formatRowRouteCities(const AircraftRow &row, Language lang, int maxPx,
                                 const TextMeasure &measure);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too since the signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the two-line identity block (identity line + route line) into
// [x, y, w, h], for Screen 1's featured panel and Screen 2. Uses
// lcars_theme fonts/colors, no chrome of its own — the caller draws any
// surrounding panel/frame. The route line uses city names
// (formatRowRouteCities(), "Warszawa (PL) -> Rzym (IT)").
void drawAircraftSummary(LGFX &gfx, const AircraftRow &row, int16_t x, int16_t y, int16_t w,
                         int16_t h);

// The identity line alone, top-left at (x, y): fitSummaryIdentity() text
// on the left, the phase icon right-aligned so its right edge sits at
// `rightX`. Nothing is drawn past `rightX`. Shared by drawAircraftSummary()
// and featured_panel so both obey the same overflow rule.
void drawIdentityLine(LGFX &gfx, const AircraftRow &row, int16_t x, int16_t y, int16_t rightX);

#endif  // ARDUINO
