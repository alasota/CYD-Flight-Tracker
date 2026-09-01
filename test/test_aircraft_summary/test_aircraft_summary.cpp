#include <unity.h>

#include "aircraft_summary.h"
#include "table_view.h"  // AircraftRow

void setUp(void) {}
void tearDown(void) {}

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
  TEST_ASSERT_EQUAL_STRING("--", summaryAirline(row).c_str());
}

// ---- formatSummaryIdentity ------------------------------------------

static void test_identity_contains_all_fields_and_phase(void) {
  std::string line = formatSummaryIdentity(makeRow());
  TEST_ASSERT_TRUE(line.find("LOT281") != std::string::npos);
  TEST_ASSERT_TRUE(line.find("LOT Polish Airlines") != std::string::npos);
  TEST_ASSERT_TRUE(line.find("B738") != std::string::npos);
  TEST_ASSERT_TRUE(line.find(phaseIcon(Phase::TAKEOFF)) != std::string::npos);
}

static void test_identity_phase_icon_varies(void) {
  AircraftRow takeoff = makeRow();
  AircraftRow landing = makeRow();
  landing.phase = Phase::LANDING;
  TEST_ASSERT_TRUE(formatSummaryIdentity(takeoff) != formatSummaryIdentity(landing));
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

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_fields_full_data);
  RUN_TEST(test_fields_fall_back_to_dashes);
  RUN_TEST(test_airline_dashes_when_lookup_found_but_string_empty);

  RUN_TEST(test_identity_contains_all_fields_and_phase);
  RUN_TEST(test_identity_phase_icon_varies);

  RUN_TEST(test_route_with_country);
  RUN_TEST(test_route_codes_only);
  RUN_TEST(test_route_not_found_is_dashes_in_both_modes);
  RUN_TEST(test_route_falls_back_to_icao_when_airport_unresolved);

  return UNITY_END();
}
