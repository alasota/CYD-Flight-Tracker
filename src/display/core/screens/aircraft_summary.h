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
// Zero networking code — draws from an already-enriched TrackedFlight plus
// the two AirportInfo results for its route endpoints (looked up
// elsewhere, exactly as featured_panel already receives them). The texts are
// pure; the LovyanGFX drawing is display/adapters/lgfx/aircraft_summary_draw.
#pragma once

#include <string>

#include "airspace/core/tracked_flight.h"   // TrackedFlight
#include "display/core/i18n.h"               // Language
#include "display/core/text/fit_to_width.h"  // TextMeasure, fitToWidth()
#include "enrichment/core/route_lookup.h"  // RouteInfo, AirportInfo

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
std::string summaryFlight(const TrackedFlight &row);   // callsign
std::string summaryAirline(const TrackedFlight &row);  // RegisteredOwners
std::string summaryType(const TrackedFlight &row);     // aircraft type

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
std::string fitSummaryIdentity(const TrackedFlight &row, int maxPx, const TextMeasure &measure);

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
std::string rowAirportCode(const TrackedFlight &row, bool origin);

// The route line for `row` using its own airports (row.origin_airport /
// dest_airport) — "WAW (PL) -> FCO (IT)" / "WAW -> FCO"; "..." while the
// route is loading, "--" when there is none; a still-loading endpoint shows
// as "...".
std::string formatRowRoute(const TrackedFlight &row, RouteFormat fmt);

// One route endpoint of `row` as a readable place name in `lang`
// (place_names::placeName(): "Warszawa", "Londyn", "Rome-Fiumicino"...);
// "..." while loading, "--" without a route.
std::string rowPlaceName(const TrackedFlight &row, bool origin, Language lang);

// The detail views' route line with city names, fitted to `maxPx`:
//   1. "Warszawa (PL) -> Rzym (IT)"  if it fits;
//   2. "Warszawa -> Rzym"            (country codes dropped);
//   3. both names cut to half the remaining width each.
// "..." / "--" for a loading / missing route, "..." for a loading endpoint.
std::string formatRowRouteCities(const TrackedFlight &row, Language lang, int maxPx,
                                 const TextMeasure &measure);
