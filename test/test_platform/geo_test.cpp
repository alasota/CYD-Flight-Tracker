// geo: distance, bearing, compass point. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "shared/geo.h"
#include "test_support.h"

namespace {

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

// ---- compassPoint -----------------------------------------------------------

static void test_compass_points(void) {
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(0.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(22.0f));
  TEST_ASSERT_EQUAL_STRING("NE", compassPoint(23.0f));
  TEST_ASSERT_EQUAL_STRING("E", compassPoint(90.0f));
  TEST_ASSERT_EQUAL_STRING("SE", compassPoint(135.0f));
  TEST_ASSERT_EQUAL_STRING("S", compassPoint(180.0f));
  TEST_ASSERT_EQUAL_STRING("SW", compassPoint(225.0f));
  TEST_ASSERT_EQUAL_STRING("W", compassPoint(270.0f));
  TEST_ASSERT_EQUAL_STRING("NW", compassPoint(315.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(350.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(360.0f));
  TEST_ASSERT_EQUAL_STRING("W", compassPoint(-90.0f));
}

}  // namespace

void run_geo_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_distance_zero_when_home_equals_aircraft);
  RUN_TEST(test_distance_one_degree_north_at_equator);
  RUN_TEST(test_distance_one_degree_east_at_equator);
  RUN_TEST(test_distance_london_to_paris_sanity_check);
  RUN_TEST(test_compass_points);
}
