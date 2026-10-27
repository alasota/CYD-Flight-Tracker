// place_names — turning an airport into a readable place name for the
// detail views (featured panel, Screen 2, Screen 3 summary): "Warszawa ->
// Rzym" instead of "WAW -> FCO". The table keeps IATA codes (no room).
//
// hexdb.io has no city field — only the airport's name ("Heathrow Airport",
// "Narita International Airport"), which often doesn't contain the city at
// all. So the place name comes from, in order:
//   1. a built-in IATA -> city table (Polish + English names) for the
//      airports most likely to show up over Poland — Polish airports,
//      Europe, the big world hubs;
//   2. hexdb.io's airport name with the generic words stripped
//      ("Rome-Fiumicino International Airport" -> "Rome-Fiumicino");
//   3. the IATA code, then the ICAO code.
//
// All output is font-safe text (lcars_theme::isFontSafeText(): ASCII + the
// Polish letters). hexdb.io airport names are folded to ASCII (asciiFold()),
// since they can contain any accent or dash the fonts may not have.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <string>

#include "display/domain/i18n.h"          // Language
#include "enrichment/domain/route_lookup.h"  // AirportInfo

// UTF-8 -> printable ASCII: accented Latin letters lose their accents
// ("Kraków" -> "Krakow", "Łódź" -> "Lodz"), en/em dashes become "-", curly
// quotes become "'", anything else non-ASCII (emoji, CJK, ...) is dropped.
std::string asciiFold(const std::string &utf8);

// hexdb.io airport name minus the generic words "International",
// "Airport", "Airfield", "Aerodrome", "Intl" ("Warsaw Chopin Airport" ->
// "Warsaw Chopin"). A name made only of those words is returned unchanged.
std::string shortAirportName(const std::string &airportName);

// One built-in table row.
struct CityEntry {
  const char *iata;
  const char *pl;  // Polish exonym (UTF-8, font-safe), e.g. "Monachium", "Kraków"
  const char *en;  // English name, e.g. "Munich"
};

// City name for an IATA code in `lang`, or nullptr if it isn't in the table.
const char *cityForIata(const std::string &iata, Language lang);

// The table itself (sorted by IATA) — exposed for the consistency tests.
int cityTableSize();
const CityEntry &cityTableEntry(int index);

// Best readable name for an airport (see the order above). `icaoFallback`
// is used when hexdb.io has no record for the airport at all.
std::string placeName(const AirportInfo &airport, const std::string &icaoFallback, Language lang);
