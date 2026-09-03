#include <unity.h>

#include "cpa_predictor.h"

void setUp(void) {}
void tearDown(void) {}

// Home is placed on the equator/prime meridian for every test so the
// equirectangular projection is exactly 111.32 km per degree in both
// axes (cos(0) == 1) — that keeps the expected times hand-checkable:
// 1 degree ~= 111.32 km, so 10 km north is lat 10/111.32 deg.
static constexpr double kHomeLat = 0.0;
static constexpr double kHomeLon = 0.0;
static constexpr double kKmPerDeg = 111.32;

static double kmNorth(double km) { return km / kKmPerDeg; }  // -> delta latitude
static double kmEast(double km) { return km / kKmPerDeg; }   // -> delta longitude

// ---- basic closing geometry ----------------------------------------------

static void test_inbound_from_north_flying_south(void) {
  // 10 km due north, tracking 180 (due south) at 100 m/s (0.1 km/s).
  // CPA is straight overhead in 10 km / 0.1 km/s = 100 s.
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(10.0), 0.0, 100.0, 180.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, p.t_cpa_seconds);
}

static void test_inbound_from_south_flying_north(void) {
  // 8 km due south, tracking 0 (due north) at 100 m/s -> 80 s to overhead.
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(-8.0), 0.0, 100.0, 0.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, p.t_cpa_seconds);
}

static void test_outbound_cpa_already_passed_is_negative(void) {
  // 10 km due north, tracking 0 (flying further north, away from home).
  // Closest approach was 100 s ago.
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(10.0), 0.0, 100.0, 0.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -100.0f, p.t_cpa_seconds);
}

static void test_crossing_track_perpendicular(void) {
  // 5 km north and 10 km west of home, tracking 90 (due east) at 100 m/s.
  // It closes the 10 km of easting in 100 s, at which point it's directly
  // north of home (5 km away) — that's the CPA.
  CpaPrediction p =
      predictCpa(kHomeLat, kHomeLon, kmNorth(5.0), kmEast(-10.0), 100.0, 90.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, p.t_cpa_seconds);
}

static void test_speed_scales_time_inversely(void) {
  // Same 10 km inbound geometry, double the speed -> half the time.
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(10.0), 0.0, 200.0, 180.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, p.t_cpa_seconds);
}

// ---- edge case: stationary aircraft ------------------------------------

static void test_zero_speed_gives_no_prediction(void) {
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(10.0), 0.0, 0.0, 180.0);
  TEST_ASSERT_FALSE(p.found);
}

static void test_near_zero_speed_gives_no_prediction(void) {
  // ~1 mm/s — on the ground / hovering, not a real closing velocity.
  CpaPrediction p = predictCpa(kHomeLat, kHomeLon, kmNorth(10.0), 0.0, 0.001, 180.0);
  TEST_ASSERT_FALSE(p.found);
}

static void test_default_constructed_result_is_not_found(void) {
  CpaPrediction p;
  TEST_ASSERT_FALSE(p.found);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.t_cpa_seconds);
}

// ---- non-equatorial home: longitude scaling still consistent ----------

static void test_high_latitude_home_inbound_from_east(void) {
  // Home at 60 N. A point due east by X degrees is only
  // X*111.32*cos(60) = X*55.66 km away. Put the aircraft 0.2 deg east
  // (~11.132 km) tracking 270 (due west) at 111.32 m/s (~0.11132 km/s)
  // -> 11.132 / 0.11132 = 100 s.
  double home_lat = 60.0;
  double ac_lon = 0.2;  // 0.2 deg east of home
  CpaPrediction p = predictCpa(home_lat, 0.0, home_lat, ac_lon, 111.32, 270.0);
  TEST_ASSERT_TRUE(p.found);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, p.t_cpa_seconds);
}

// ---- extrapolateCpa (local ticking between polls) ------------------------

static void test_extrapolate_counts_down_by_elapsed_seconds(void) {
  CpaPrediction polled;
  polled.found = true;
  polled.t_cpa_seconds = 42.0f;

  CpaPrediction now = extrapolateCpa(polled, 5000);  // 5s later
  TEST_ASSERT_TRUE(now.found);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 37.0f, now.t_cpa_seconds);
}

static void test_extrapolate_can_go_negative(void) {
  CpaPrediction polled;
  polled.found = true;
  polled.t_cpa_seconds = 3.0f;

  CpaPrediction now = extrapolateCpa(polled, 8000);  // 8s later
  TEST_ASSERT_TRUE(now.found);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, now.t_cpa_seconds);
}

static void test_extrapolate_zero_elapsed_is_identity(void) {
  CpaPrediction polled;
  polled.found = true;
  polled.t_cpa_seconds = 12.5f;

  CpaPrediction now = extrapolateCpa(polled, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, now.t_cpa_seconds);
}

static void test_extrapolate_not_found_stays_not_found(void) {
  CpaPrediction polled;  // found == false
  polled.t_cpa_seconds = 999.0f;

  CpaPrediction now = extrapolateCpa(polled, 5000);
  TEST_ASSERT_FALSE(now.found);
  TEST_ASSERT_EQUAL_FLOAT(999.0f, now.t_cpa_seconds);  // untouched
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_inbound_from_north_flying_south);
  RUN_TEST(test_inbound_from_south_flying_north);
  RUN_TEST(test_outbound_cpa_already_passed_is_negative);
  RUN_TEST(test_crossing_track_perpendicular);
  RUN_TEST(test_speed_scales_time_inversely);

  RUN_TEST(test_zero_speed_gives_no_prediction);
  RUN_TEST(test_near_zero_speed_gives_no_prediction);
  RUN_TEST(test_default_constructed_result_is_not_found);

  RUN_TEST(test_high_latitude_home_inbound_from_east);

  RUN_TEST(test_extrapolate_counts_down_by_elapsed_seconds);
  RUN_TEST(test_extrapolate_can_go_negative);
  RUN_TEST(test_extrapolate_zero_elapsed_is_identity);
  RUN_TEST(test_extrapolate_not_found_stays_not_found);

  return UNITY_END();
}
