#include <unity.h>

#include "featured_panel.h"
#include "table_view.h"

void setUp(void) {}
void tearDown(void) {}

static AircraftRow makeRow() {
  AircraftRow row;
  row.aircraft.callsign = "LOT281";
  row.info.found = true;
  row.info.airline = "LOT Polish Airlines";
  row.info.aircraft_type = "737 MAX 8";
  row.phase = Phase::TAKEOFF;
  return row;
}

// ---- formatFeaturedLine1 ----------------------------------------------------

static void test_line1_full_data(void) {
  AircraftRow row = makeRow();
  std::string line = formatFeaturedLine1(row);

  TEST_ASSERT_TRUE(line.find("LOT281") != std::string::npos);
  TEST_ASSERT_TRUE(line.find("LOT Polish Airlines") != std::string::npos);
  TEST_ASSERT_TRUE(line.find("737 MAX 8") != std::string::npos);
  TEST_ASSERT_TRUE(line.find(phaseIcon(Phase::TAKEOFF)) != std::string::npos);
}

static void test_line1_falls_back_to_dashes(void) {
  AircraftRow row;  // empty callsign, info.found=false by default
  std::string line = formatFeaturedLine1(row);

  TEST_ASSERT_TRUE(line.find("--") != std::string::npos);
}

static void test_line1_distinguishes_phase_icons(void) {
  AircraftRow takeoff = makeRow();
  takeoff.phase = Phase::TAKEOFF;
  AircraftRow landing = makeRow();
  landing.phase = Phase::LANDING;

  std::string takeoffLine = formatFeaturedLine1(takeoff);
  std::string landingLine = formatFeaturedLine1(landing);

  TEST_ASSERT_TRUE(takeoffLine != landingLine);
}

// ---- formatFeaturedLine2 ----------------------------------------------------

static void test_line2_full_data_shows_iata_and_country(void) {
  RouteInfo route;
  route.found = true;
  route.origin_icao = "EPWA";
  route.dest_icao = "LIRF";

  AirportInfo origin;
  origin.found = true;
  origin.iata_code = "WAW";
  origin.country_code = "PL";

  AirportInfo dest;
  dest.found = true;
  dest.iata_code = "FCO";
  dest.country_code = "IT";

  std::string line = formatFeaturedLine2(route, origin, dest);

  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> FCO (IT)", line.c_str());
}

static void test_line2_no_route_is_dash(void) {
  RouteInfo route;  // found=false by default
  AirportInfo origin;
  AirportInfo dest;

  std::string line = formatFeaturedLine2(route, origin, dest);
  TEST_ASSERT_EQUAL_STRING("--", line.c_str());
}

static void test_line2_route_found_but_airports_unresolved_falls_back_to_icao(void) {
  RouteInfo route;
  route.found = true;
  route.origin_icao = "EPWA";
  route.dest_icao = "LIRF";

  AirportInfo origin;  // found=false
  AirportInfo dest;    // found=false

  std::string line = formatFeaturedLine2(route, origin, dest);
  TEST_ASSERT_EQUAL_STRING("EPWA -> LIRF", line.c_str());
}

static void test_line2_one_airport_resolved_other_not(void) {
  RouteInfo route;
  route.found = true;
  route.origin_icao = "EPWA";
  route.dest_icao = "LIRF";

  AirportInfo origin;
  origin.found = true;
  origin.iata_code = "WAW";
  origin.country_code = "PL";

  AirportInfo dest;  // unresolved

  std::string line = formatFeaturedLine2(route, origin, dest);
  TEST_ASSERT_EQUAL_STRING("WAW (PL) -> LIRF", line.c_str());
}

// ---- featuredPanelHeightPx --------------------------------------------------

static void test_featured_panel_height_is_positive_and_reasonable(void) {
  int16_t h = featuredPanelHeightPx();
  TEST_ASSERT_TRUE(h > 0);
  TEST_ASSERT_TRUE(h < 240);  // must fit on a 240px-tall screen alongside a table
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_line1_full_data);
  RUN_TEST(test_line1_falls_back_to_dashes);
  RUN_TEST(test_line1_distinguishes_phase_icons);

  RUN_TEST(test_line2_full_data_shows_iata_and_country);
  RUN_TEST(test_line2_no_route_is_dash);
  RUN_TEST(test_line2_route_found_but_airports_unresolved_falls_back_to_icao);
  RUN_TEST(test_line2_one_airport_resolved_other_not);

  RUN_TEST(test_featured_panel_height_is_positive_and_reasonable);

  return UNITY_END();
}
