// flight_ordering: display order, featured split. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "airspace/core/flight_ordering.h"
#include "fixtures.h"
#include "test_support.h"

namespace {

// ---- sortRowsByDistance -----------------------------------------------------

static void test_sort_orders_ascending_by_distance(void) {
  AircraftRow far, mid, near;
  far.aircraft.icao24 = "far";
  far.distance_km = 300.0f;
  far.has_distance = true;
  mid.aircraft.icao24 = "mid";
  mid.distance_km = 100.0f;
  mid.has_distance = true;
  near.aircraft.icao24 = "near";
  near.distance_km = 10.0f;
  near.has_distance = true;

  std::vector<AircraftRow> rows = {far, mid, near};
  sortRowsByDistance(rows);

  TEST_ASSERT_EQUAL_STRING("near", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("mid", rows[1].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("far", rows[2].aircraft.icao24.c_str());
}

static void test_sort_is_stable_for_equal_distances(void) {
  AircraftRow a, b, c;
  a.aircraft.icao24 = "a";
  a.distance_km = 50.0f;
  a.has_distance = true;
  b.aircraft.icao24 = "b";
  b.distance_km = 50.0f;
  b.has_distance = true;
  c.aircraft.icao24 = "c";
  c.distance_km = 50.0f;
  c.has_distance = true;

  std::vector<AircraftRow> rows = {a, b, c};
  sortRowsByDistance(rows);

  // All tied at 50km — original relative order (a, b, c) must be kept.
  TEST_ASSERT_EQUAL_STRING("a", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("b", rows[1].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("c", rows[2].aircraft.icao24.c_str());
}

static void test_sort_puts_rows_without_distance_last(void) {
  AircraftRow known, unknown;
  known.aircraft.icao24 = "known";
  known.distance_km = 42.0f;
  known.has_distance = true;
  unknown.aircraft.icao24 = "unknown";
  unknown.has_distance = false;

  std::vector<AircraftRow> rows = {unknown, known};
  sortRowsByDistance(rows);

  TEST_ASSERT_EQUAL_STRING("known", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown", rows[1].aircraft.icao24.c_str());
}

static void test_sort_empty_array_does_not_crash(void) {
  std::vector<AircraftRow> rows;
  sortRowsByDistance(rows);
  TEST_ASSERT_TRUE(rows.empty());
}

// ---- sortRowsForDisplay: airborne first (improvement #9) -------------------

static void test_display_order_puts_airborne_before_ground_traffic(void) {
  // The plane taxiing at the airport 3 km away must not take the featured
  // slot from one flying 9 km away.
  std::vector<AircraftRow> rows = {makeRow("TAXI1", 3.0f, true), makeRow("AIR2", 9.0f, false),
                                   makeRow("AIR1", 5.0f, false), makeRow("TAXI2", 6.0f, true)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("AIR1", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("AIR2", rows[1].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("TAXI1", rows[2].aircraft.callsign.c_str());  // ground: nearest first
  TEST_ASSERT_EQUAL_STRING("TAXI2", rows[3].aircraft.callsign.c_str());
}

static void test_display_order_puts_positionless_last(void) {
  std::vector<AircraftRow> rows = {makeRow("NOPOS", 0.0f, false, false),
                                   makeRow("TAXI", 2.0f, true), makeRow("AIR", 20.0f, false)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("AIR", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("TAXI", rows[1].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("NOPOS", rows[2].aircraft.callsign.c_str());
}

static void test_display_order_all_on_ground_still_nearest_first(void) {
  std::vector<AircraftRow> rows = {makeRow("B", 4.0f, true), makeRow("A", 1.0f, true)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("A", rows[0].aircraft.callsign.c_str());
}

// ---- splitFeaturedAndRest ---------------------------------------------------

static void test_split_featured_and_rest_multiple_rows(void) {
  AircraftRow closest, second, third;
  closest.aircraft.icao24 = "closest";
  second.aircraft.icao24 = "second";
  third.aircraft.icao24 = "third";

  std::vector<AircraftRow> rows = {closest, second, third};  // already sorted
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_TRUE(split.hasFeatured);
  TEST_ASSERT_EQUAL_STRING("closest", split.featured.aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_size_t(2, split.rest.size());
  TEST_ASSERT_EQUAL_STRING("second", split.rest[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("third", split.rest[1].aircraft.icao24.c_str());
}

static void test_split_featured_and_rest_single_row(void) {
  AircraftRow only;
  only.aircraft.icao24 = "only";

  std::vector<AircraftRow> rows = {only};
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_TRUE(split.hasFeatured);
  TEST_ASSERT_EQUAL_STRING("only", split.featured.aircraft.icao24.c_str());
  TEST_ASSERT_TRUE(split.rest.empty());
}

static void test_split_featured_and_rest_empty(void) {
  std::vector<AircraftRow> rows;
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_FALSE(split.hasFeatured);
  TEST_ASSERT_TRUE(split.rest.empty());
}

}  // namespace

void run_flight_ordering_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_sort_orders_ascending_by_distance);
  RUN_TEST(test_sort_is_stable_for_equal_distances);
  RUN_TEST(test_sort_puts_rows_without_distance_last);
  RUN_TEST(test_sort_empty_array_does_not_crash);
  RUN_TEST(test_display_order_puts_airborne_before_ground_traffic);
  RUN_TEST(test_display_order_puts_positionless_last);
  RUN_TEST(test_display_order_all_on_ground_still_nearest_first);
  RUN_TEST(test_split_featured_and_rest_multiple_rows);
  RUN_TEST(test_split_featured_and_rest_single_row);
  RUN_TEST(test_split_featured_and_rest_empty);
}
