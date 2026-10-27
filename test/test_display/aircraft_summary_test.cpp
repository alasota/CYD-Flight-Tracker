// aircraft_summary: shared identity / route lines. docs/screens.md "Screen 1 — Flights" (identity line).
#include <unity.h>

#include "aircraft_summary.h"
#include "table_view.h"  // AircraftRow
#include "test_support.h"

namespace {

static AircraftRow makeRow() {
  AircraftRow row;
  row.aircraft.callsign = "LOT281";
  row.info.found = true;
  row.info.airline = "LOT Polish Airlines";
  row.info.aircraft_type = "B738";
  row.phase = Phase::TAKEOFF;
  row.route.found = true;
  row.route.origin_icao = "EPWA";
  row.route.dest_icao = "LIRF";
  return row;
}

static AirportInfo makeAirport(const char *iata, const char *cc) {
  AirportInfo a;
  a.found = true;
  a.iata_code = iata;
  a.country_code = cc;
  return a;
}

// ---- field helpers + fallbacks ---------------------------------------

static void test_fields_full_data(void) {
  AircraftRow row = makeRow();
  TEST_ASSERT_EQUAL_STRING("LOT281", summaryFlight(row).c_str());
  TEST_ASSERT_EQUAL_STRING("LOT Polish Airlines", summaryAirline(row).c_str());
  TEST_ASSERT_EQUAL_STRING("B738", summaryType(row).c_str());
}

static void test_fields_fall_back_to_dashes(void) {
  AircraftRow row;  // empty callsign, info.found == false
  TEST_ASSERT_EQUAL_STRING("--", summaryFlight(row).c_str());
  TEST_ASSERT_EQUAL_STRING("--", summaryAirline(row).c_str());
  TEST_ASSERT_EQUAL_STRING("--", summaryType(row).c_str());
}

static void test_airline_dashes_when_lookup_found_but_string_empty(void) {
  AircraftRow row = makeRow();
  row.info.airline = "";
  row.aircraft.callsign = "SPKAX";  // no airline prefix to fall back on
  TEST_ASSERT_EQUAL_STRING("--", summaryAirline(row).c_str());
  row.aircraft.callsign = "LOT281";  // ...but an airline callsign fills the gap
  TEST_ASSERT_EQUAL_STRING("LOT", summaryAirline(row).c_str());
}

// ---- formatSummaryRoute: the two RouteFormat variants -----------------

static void test_route_with_country(void) {
  AircraftRow row = makeRow();
  std::string s = formatSummaryRoute(row.route, makeAirport("WAW", "PL"), makeAirport("FCO", "IT"),
                                     RouteFormat::WithCountry);
  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> FCO (IT)", s.c_str());
}

static void test_route_codes_only(void) {
  AircraftRow row = makeRow();
  std::string s = formatSummaryRoute(row.route, makeAirport("WAW", "PL"), makeAirport("FCO", "IT"),
                                     RouteFormat::CodesOnly);
  TEST_ASSERT_EQUAL_STRING("WAW -> FCO", s.c_str());
}

static void test_route_not_found_is_dashes_in_both_modes(void) {
  RouteInfo route;  // found == false
  TEST_ASSERT_EQUAL_STRING(
      "--", formatSummaryRoute(route, AirportInfo{}, AirportInfo{}, RouteFormat::WithCountry).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "--", formatSummaryRoute(route, AirportInfo{}, AirportInfo{}, RouteFormat::CodesOnly).c_str());
}

static void test_route_falls_back_to_icao_when_airport_unresolved(void) {
  AircraftRow row = makeRow();
  // origin airport resolved, destination not.
  std::string s = formatSummaryRoute(row.route, makeAirport("WAW", "PL"), AirportInfo{},
                                     RouteFormat::WithCountry);
  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> LIRF", s.c_str());

  // Neither resolved -> both bare ICAO, no country, even in WithCountry mode.
  std::string s2 = formatSummaryRoute(row.route, AirportInfo{}, AirportInfo{},
                                      RouteFormat::WithCountry);
  TEST_ASSERT_EQUAL_STRING("EPWA -> LIRF", s2.c_str());
}

// ---- fitSummaryIdentity -----------------------------------------------
//
// Measured with a fake 1px-per-char font, so widths == character counts.
// "LOT281  LOT Polish Airlines  B738" is 33 chars.

static int onePx(const std::string &s) { return static_cast<int>(s.size()); }
static const TextMeasure kOnePx = onePx;

static void test_fit_identity_unchanged_when_it_fits(void) {
  TEST_ASSERT_EQUAL_STRING("LOT281  LOT Polish Airlines  B738",
                           fitSummaryIdentity(makeRow(), 33, kOnePx).c_str());
}

// Scenario: Linia identyfikacji nie zawiera znaku fazy lotu — ikona jest rysowana osobno
static void test_fit_identity_has_no_phase_marker(void) {
  // makeRow(): LOT281, LOT Polish Airlines, B738, taking off. The phase icon is
  // drawn separately at a fixed right-aligned spot, so the text has no marker.
  ASSERT_STR_EQ("LOT281  LOT Polish Airlines  B738", fitSummaryIdentity(makeRow(), 200, kOnePx));
}

static void test_fit_identity_truncates_airline_first(void) {
  // 25px: callsign + "  " (8) and "  " + type (6) leave 11 for the airline.
  std::string line = fitSummaryIdentity(makeRow(), 25, kOnePx);
  TEST_ASSERT_EQUAL_STRING("LOT281  LOT Poli...  B738", line.c_str());
}

static void test_fit_identity_drops_airline_when_no_room_for_it(void) {
  // 16px leaves 2px for the airline — not even "L..." fits, so it's dropped
  // rather than shown as a meaningless 2-letter stub.
  TEST_ASSERT_EQUAL_STRING("LOT281  B738", fitSummaryIdentity(makeRow(), 16, kOnePx).c_str());
}

static void test_fit_identity_then_truncates_type(void) {
  TEST_ASSERT_EQUAL_STRING("LOT281  B7", fitSummaryIdentity(makeRow(), 10, kOnePx).c_str());
}

static void test_fit_identity_never_truncates_callsign(void) {
  TEST_ASSERT_EQUAL_STRING("LOT281", fitSummaryIdentity(makeRow(), 3, kOnePx).c_str());
}

static void test_fit_identity_never_exceeds_width_once_callsign_fits(void) {
  AircraftRow row = makeRow();
  row.info.airline = "Scandinavian Airlines System Denmark Norway Sweden";
  row.info.aircraft_type = "Embraer ERJ-190-200 LR";
  for (int px = 6; px <= 80; ++px) {
    TEST_ASSERT_TRUE(onePx(fitSummaryIdentity(row, px, kOnePx)) <= px);
  }
}

// ---- pending ("...") vs unknown ("--") ----------------------------------------

static void test_airline_and_type_show_dots_while_lookup_pending(void) {
  AircraftRow row;
  row.aircraft.callsign = "SPKAX";  // registration-style: no airline prefix
  row.info_pending = true;          // not looked up yet (per-poll budget)
  TEST_ASSERT_EQUAL_STRING("...", summaryAirline(row).c_str());
  TEST_ASSERT_EQUAL_STRING("...", summaryType(row).c_str());
}

static void test_airline_from_callsign_when_hexdb_has_nothing(void) {
  // SunExpress SXS6JZ: unknown to hexdb.io, but the callsign says who it is.
  AircraftRow row;
  row.aircraft.callsign = "SXS6JZ";
  TEST_ASSERT_EQUAL_STRING("SunExpress", summaryAirline(row).c_str());
  row.info_pending = true;  // even before the lookup runs
  TEST_ASSERT_EQUAL_STRING("SunExpress", summaryAirline(row).c_str());
}

static void test_hexdb_airline_wins_over_callsign_table(void) {
  AircraftRow row = makeRow();  // hexdb: "LOT Polish Airlines"
  TEST_ASSERT_EQUAL_STRING("LOT Polish Airlines", summaryAirline(row).c_str());
}

static void test_type_prefers_friendly_name(void) {
  AircraftRow row = makeRow();
  row.info.aircraft_type = "737MAX 8";
  row.info.icao_type = "B38M";
  TEST_ASSERT_EQUAL_STRING("737 MAX 8", summaryType(row).c_str());
  row.info.icao_type = "ZZZZ";  // not in the table -> hexdb.io's Type string
  TEST_ASSERT_EQUAL_STRING("737MAX 8", summaryType(row).c_str());
}

static void test_airline_dashes_once_lookup_confirmed_missing(void) {
  AircraftRow row;
  row.info_pending = false;  // looked up, hexdb.io didn't know it
  TEST_ASSERT_EQUAL_STRING("--", summaryAirline(row).c_str());
}

// ---- rowAirportCode: IATA everywhere -------------------------------------------

static void test_airport_code_prefers_iata(void) {
  AircraftRow row = makeRow();
  row.origin_airport = makeAirport("WAW", "PL");
  row.dest_airport = makeAirport("FCO", "IT");
  TEST_ASSERT_EQUAL_STRING("WAW", rowAirportCode(row, /*origin=*/true).c_str());
  TEST_ASSERT_EQUAL_STRING("FCO", rowAirportCode(row, /*origin=*/false).c_str());
}

static void test_airport_code_dots_while_airport_pending(void) {
  AircraftRow row = makeRow();
  row.origin_airport_pending = true;
  TEST_ASSERT_EQUAL_STRING("...", rowAirportCode(row, true).c_str());
}

static void test_airport_code_falls_back_to_icao_when_unknown(void) {
  // hexdb.io had no airport record — the ICAO code is still better than "--".
  AircraftRow row = makeRow();  // route EPWA-LIRF, airports unresolved, not pending
  TEST_ASSERT_EQUAL_STRING("EPWA", rowAirportCode(row, true).c_str());
  TEST_ASSERT_EQUAL_STRING("LIRF", rowAirportCode(row, false).c_str());
}

static void test_airport_code_without_route(void) {
  AircraftRow row;
  row.route_pending = true;
  TEST_ASSERT_EQUAL_STRING("...", rowAirportCode(row, true).c_str());
  row.route_pending = false;  // confirmed: no filed route
  TEST_ASSERT_EQUAL_STRING("--", rowAirportCode(row, true).c_str());
}

// ---- formatRowRoute ------------------------------------------------------------

static void test_row_route_with_country_and_iata(void) {
  AircraftRow row = makeRow();
  row.origin_airport = makeAirport("WAW", "PL");
  row.dest_airport = makeAirport("FCO", "IT");
  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> FCO (IT)",
                           formatRowRoute(row, RouteFormat::WithCountry).c_str());
  TEST_ASSERT_EQUAL_STRING("WAW -> FCO", formatRowRoute(row, RouteFormat::CodesOnly).c_str());
}

static void test_row_route_pending_and_unknown(void) {
  AircraftRow row;
  row.route_pending = true;
  TEST_ASSERT_EQUAL_STRING("...", formatRowRoute(row, RouteFormat::WithCountry).c_str());
  row.route_pending = false;
  TEST_ASSERT_EQUAL_STRING("--", formatRowRoute(row, RouteFormat::WithCountry).c_str());
}

static void test_row_route_endpoint_pending(void) {
  AircraftRow row = makeRow();
  row.origin_airport = makeAirport("WAW", "PL");
  row.dest_airport_pending = true;
  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> ...", formatRowRoute(row, RouteFormat::WithCountry).c_str());
}

// ---- route with city names (detail views) -------------------------------------

static AircraftRow rowWawFco() {
  AircraftRow row = makeRow();  // route EPWA-LIRF
  row.origin_airport = makeAirport("WAW", "PL");
  row.dest_airport = makeAirport("FCO", "IT");
  return row;
}

static void test_city_route_full_when_it_fits(void) {
  TEST_ASSERT_EQUAL_STRING("Warszawa (PL) -> Rzym (IT)",
                           formatRowRouteCities(rowWawFco(), Language::PL, 100, kOnePx).c_str());
  TEST_ASSERT_EQUAL_STRING("Warsaw (PL) -> Rome (IT)",
                           formatRowRouteCities(rowWawFco(), Language::EN, 100, kOnePx).c_str());
}

static void test_city_route_drops_country_codes_first(void) {
  // "Warszawa (PL) -> Rzym (IT)" is 26 chars; 20 only fits the bare names.
  TEST_ASSERT_EQUAL_STRING("Warszawa -> Rzym",
                           formatRowRouteCities(rowWawFco(), Language::PL, 20, kOnePx).c_str());
}

static void test_city_route_then_shortens_both_names(void) {
  std::string s = formatRowRouteCities(rowWawFco(), Language::PL, 14, kOnePx);
  TEST_ASSERT_TRUE(onePx(s) <= 14);
  ASSERT_CONTAINS(s, " -> ");
}

static void test_city_route_pending_and_unknown(void) {
  AircraftRow row;
  row.route_pending = true;
  TEST_ASSERT_EQUAL_STRING("...", formatRowRouteCities(row, Language::PL, 100, kOnePx).c_str());
  row.route_pending = false;
  TEST_ASSERT_EQUAL_STRING("--", formatRowRouteCities(row, Language::PL, 100, kOnePx).c_str());

  AircraftRow half = rowWawFco();
  half.dest_airport = AirportInfo{};
  half.dest_airport_pending = true;
  TEST_ASSERT_EQUAL_STRING("Warszawa (PL) -> ...",
                           formatRowRouteCities(half, Language::PL, 100, kOnePx).c_str());
}

static void test_row_place_name_per_endpoint(void) {
  TEST_ASSERT_EQUAL_STRING("Warszawa", rowPlaceName(rowWawFco(), true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("Rome", rowPlaceName(rowWawFco(), false, Language::EN).c_str());
  AircraftRow none;
  TEST_ASSERT_EQUAL_STRING("--", rowPlaceName(none, true, Language::PL).c_str());
}

}  // namespace

void run_aircraft_summary_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_city_route_full_when_it_fits);
  RUN_TEST(test_city_route_drops_country_codes_first);
  RUN_TEST(test_city_route_then_shortens_both_names);
  RUN_TEST(test_city_route_pending_and_unknown);
  RUN_TEST(test_row_place_name_per_endpoint);

  RUN_TEST(test_airline_and_type_show_dots_while_lookup_pending);
  RUN_TEST(test_airline_from_callsign_when_hexdb_has_nothing);
  RUN_TEST(test_hexdb_airline_wins_over_callsign_table);
  RUN_TEST(test_type_prefers_friendly_name);
  RUN_TEST(test_airline_dashes_once_lookup_confirmed_missing);
  RUN_TEST(test_airport_code_prefers_iata);
  RUN_TEST(test_airport_code_dots_while_airport_pending);
  RUN_TEST(test_airport_code_falls_back_to_icao_when_unknown);
  RUN_TEST(test_airport_code_without_route);
  RUN_TEST(test_row_route_with_country_and_iata);
  RUN_TEST(test_row_route_pending_and_unknown);
  RUN_TEST(test_row_route_endpoint_pending);

  RUN_TEST(test_fit_identity_unchanged_when_it_fits);
  RUN_TEST(test_fit_identity_has_no_phase_marker);
  RUN_TEST(test_fit_identity_truncates_airline_first);
  RUN_TEST(test_fit_identity_drops_airline_when_no_room_for_it);
  RUN_TEST(test_fit_identity_then_truncates_type);
  RUN_TEST(test_fit_identity_never_truncates_callsign);
  RUN_TEST(test_fit_identity_never_exceeds_width_once_callsign_fits);

  RUN_TEST(test_fields_full_data);
  RUN_TEST(test_fields_fall_back_to_dashes);
  RUN_TEST(test_airline_dashes_when_lookup_found_but_string_empty);


  RUN_TEST(test_route_with_country);
  RUN_TEST(test_route_codes_only);
  RUN_TEST(test_route_not_found_is_dashes_in_both_modes);
  RUN_TEST(test_route_falls_back_to_icao_when_airport_unresolved);

}
