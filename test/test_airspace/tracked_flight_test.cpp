// tracked_flight: joining lookups, distance from home, flight phase. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "airspace/core/tracked_flight.h"
#include "test_support.h"

namespace {

// ---- buildEnrichedRecords ---------------------------------------------------

static void test_build_enriched_records_found_true(void) {
  Aircraft ac;
  ac.icao24 = "4010ee";
  ac.callsign = "DLH9LH";
  ac.lat = 50.1210f;
  ac.lon = 6.1546f;
  ac.has_position = true;

  AircraftInfo info;
  info.found = true;
  info.airline = "easyJet Airline";
  info.aircraft_type = "A319 111";
  info.registration = "G-EZBZ";

  RouteInfo route;
  route.found = true;
  route.origin_icao = "EIDW";
  route.dest_icao = "EGLL";

  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", info}};
  std::map<std::string, RouteInfo> routeByCallsign = {{"DLH9LH", route}};

  std::vector<TrackedFlight> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_EQUAL_STRING("4010ee", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("DLH9LH", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_TRUE(rows[0].info.found);
  TEST_ASSERT_EQUAL_STRING("easyJet Airline", rows[0].info.airline.c_str());
  TEST_ASSERT_EQUAL_STRING("A319 111", rows[0].info.aircraft_type.c_str());
  TEST_ASSERT_TRUE(rows[0].route.found);
  TEST_ASSERT_EQUAL_STRING("EIDW", rows[0].route.origin_icao.c_str());
  TEST_ASSERT_EQUAL_STRING("EGLL", rows[0].route.dest_icao.c_str());
}

static void test_build_enriched_records_found_false_when_lookup_missing(void) {
  Aircraft ac;
  ac.icao24 = "a1b2c3";
  ac.callsign = "N12345";
  ac.has_position = true;

  // icao24/callsign never resolved (no entry at all — e.g. lookup still pending).
  std::map<std::string, AircraftInfo> infoByIcao24;
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<TrackedFlight> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_FALSE(rows[0].info.found);
  // drawTablePage()/featured_panel render this as "--" — see orDash()/
  // aircraft_summary::formatSummaryRoute().
  TEST_ASSERT_TRUE(rows[0].info.airline.empty());
  TEST_ASSERT_TRUE(rows[0].info.aircraft_type.empty());
}

static void test_build_enriched_records_missing_route_renders_as_dash(void) {
  // Airline/type lookup resolved, but route_lookup hasn't (yet) — the two
  // enrichment sources fail independently (docs/data-sources.md "Route & airport lookup").
  Aircraft ac;
  ac.icao24 = "4010ee";
  ac.callsign = "DLH9LH";
  ac.has_position = true;

  AircraftInfo info;
  info.found = true;
  info.airline = "Lufthansa";
  info.aircraft_type = "A320";

  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", info}};
  std::map<std::string, RouteInfo> routeByCallsign;  // empty — no route entry at all

  std::vector<TrackedFlight> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_TRUE(rows[0].info.found);
  TEST_ASSERT_FALSE(rows[0].route.found);
  TEST_ASSERT_TRUE(rows[0].route.origin_icao.empty());
  TEST_ASSERT_TRUE(rows[0].route.dest_icao.empty());
}

static void test_build_enriched_records_found_false_from_cached_miss(void) {
  Aircraft ac;
  ac.icao24 = "ffffff";
  ac.has_position = true;

  // icao24 WAS looked up, and aircraft_lookup cached an explicit miss
  // (hexdb.io's "not found" response) — same result either way.
  AircraftInfo miss;  // found=false by default
  std::map<std::string, AircraftInfo> infoByIcao24 = {{"ffffff", miss}};
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<TrackedFlight> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_FALSE(rows[0].info.found);
}

static void test_build_enriched_records_empty_aircraft_list(void) {
  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", AircraftInfo{}}};
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<TrackedFlight> rows = buildEnrichedRecords({}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_TRUE(rows.empty());
}

// ---- annotateDistances -----------------------------------------------------

static void test_annotate_fills_distance_for_positioned_aircraft(void) {
  TrackedFlight row;
  row.aircraft.icao24 = "a";
  row.aircraft.lat = 1.0f;
  row.aircraft.lon = 0.0f;
  row.aircraft.has_position = true;

  std::vector<TrackedFlight> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_TRUE(rows[0].has_distance);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, rows[0].distance_km);
}

static void test_annotate_leaves_no_position_aircraft_unset(void) {
  TrackedFlight row;
  row.aircraft.icao24 = "noPos";
  row.aircraft.has_position = false;

  std::vector<TrackedFlight> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_FALSE(rows[0].has_distance);
}

// ---- classifyPhases ---------------------------------------------------------

static void test_classify_phases_fills_phase_from_distance_and_vertical_rate(void) {
  TrackedFlight row;
  row.aircraft.vertical_rate = 5.0f;
  row.aircraft.on_ground = false;
  row.distance_km = 2.0f;
  row.has_distance = true;

  std::vector<TrackedFlight> rows = {row};
  classifyPhases(rows, /*near_airport_km=*/5.0f, /*climb_threshold_mps=*/3.0f);

  ASSERT_ENUM_EQ(Phase::TAKEOFF, rows[0].phase);
}

static void test_classify_phases_no_distance_forces_none(void) {
  TrackedFlight row;
  row.has_distance = false;  // no position -> nothing to classify
  row.aircraft.vertical_rate = 10.0f;

  std::vector<TrackedFlight> rows = {row};
  classifyPhases(rows, 5.0f, 3.0f);

  ASSERT_ENUM_EQ(Phase::NONE, rows[0].phase);
}

}  // namespace

void run_tracked_flight_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_build_enriched_records_found_true);
  RUN_TEST(test_build_enriched_records_found_false_when_lookup_missing);
  RUN_TEST(test_build_enriched_records_missing_route_renders_as_dash);
  RUN_TEST(test_build_enriched_records_found_false_from_cached_miss);
  RUN_TEST(test_build_enriched_records_empty_aircraft_list);
  RUN_TEST(test_annotate_fills_distance_for_positioned_aircraft);
  RUN_TEST(test_annotate_leaves_no_position_aircraft_unset);
  RUN_TEST(test_classify_phases_fills_phase_from_distance_and_vertical_rate);
  RUN_TEST(test_classify_phases_no_distance_forces_none);
}
