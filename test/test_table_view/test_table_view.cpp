#include <unity.h>

#include "table_view.h"

void setUp(void) {}
void tearDown(void) {}

// ---- computeDistanceBearing -----------------------------------------------

static void test_distance_zero_when_home_equals_aircraft(void) {
  DistanceBearing db = computeDistanceBearing(51.5074f, -0.1278f, 51.5074f, -0.1278f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, db.distance_km);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, db.bearing_deg);
}

static void test_distance_one_degree_north_at_equator(void) {
  // 1 deg of latitude is ~111.19 km anywhere on a sphere of radius 6371km.
  DistanceBearing db = computeDistanceBearing(0.0f, 0.0f, 1.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, db.distance_km);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, db.bearing_deg);  // due north
}

static void test_distance_one_degree_east_at_equator(void) {
  // At the equator, 1 deg of longitude is also ~111.19 km.
  DistanceBearing db = computeDistanceBearing(0.0f, 0.0f, 0.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, db.distance_km);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 90.0f, db.bearing_deg);  // due east
}

static void test_distance_london_to_paris_sanity_check(void) {
  // Well-known great-circle distance, commonly cited as ~344 km.
  DistanceBearing db = computeDistanceBearing(51.5074f, -0.1278f, 48.8566f, 2.3522f);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 344.0f, db.distance_km);
  TEST_ASSERT_TRUE(db.bearing_deg > 90.0f && db.bearing_deg < 180.0f);  // south-east
}

// ---- annotateDistances -----------------------------------------------------

static void test_annotate_fills_distance_for_positioned_aircraft(void) {
  AircraftRow row;
  row.aircraft.icao24 = "a";
  row.aircraft.lat = 1.0f;
  row.aircraft.lon = 0.0f;
  row.aircraft.has_position = true;

  std::vector<AircraftRow> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_TRUE(rows[0].has_distance);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, rows[0].distance_km);
}

static void test_annotate_leaves_no_position_aircraft_unset(void) {
  AircraftRow row;
  row.aircraft.icao24 = "noPos";
  row.aircraft.has_position = false;

  std::vector<AircraftRow> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_FALSE(rows[0].has_distance);
}

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

// ---- rowsPerPage -------------------------------------------------------------

static void test_rows_per_page(void) {
  TEST_ASSERT_EQUAL_INT(0, rowsPerPage(10));    // shorter than one row
  TEST_ASSERT_EQUAL_INT(1, rowsPerPage(20));
  TEST_ASSERT_EQUAL_INT(10, rowsPerPage(200));
  TEST_ASSERT_EQUAL_INT(10, rowsPerPage(209));  // partial row doesn't count
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_distance_zero_when_home_equals_aircraft);
  RUN_TEST(test_distance_one_degree_north_at_equator);
  RUN_TEST(test_distance_one_degree_east_at_equator);
  RUN_TEST(test_distance_london_to_paris_sanity_check);

  RUN_TEST(test_annotate_fills_distance_for_positioned_aircraft);
  RUN_TEST(test_annotate_leaves_no_position_aircraft_unset);

  RUN_TEST(test_sort_orders_ascending_by_distance);
  RUN_TEST(test_sort_is_stable_for_equal_distances);
  RUN_TEST(test_sort_puts_rows_without_distance_last);
  RUN_TEST(test_sort_empty_array_does_not_crash);

  RUN_TEST(test_rows_per_page);

  return UNITY_END();
}
