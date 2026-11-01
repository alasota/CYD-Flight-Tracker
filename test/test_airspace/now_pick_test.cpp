// now_pick: Screen 2's aircraft — the soonest overhead pass. docs/screens.md "Screen 2 — Flight (NOW / TERAZ)".
#include <unity.h>

#include <vector>

#include "airspace/core/now_pick.h"
#include "test_support.h"

namespace {

// ---- pickNowAircraft: the one that will be overhead soonest ----------------

static AircraftRow airborne() {
  AircraftRow r;
  r.aircraft.has_position = true;
  r.aircraft.on_ground = false;
  return r;
}

static CpaPrediction cpa(float t, float missKm) {
  CpaPrediction p;
  p.found = true;
  p.t_cpa_seconds = t;
  p.miss_distance_km = missKm;
  return p;
}

static void test_pick_prefers_soonest_overhead_not_nearest(void) {
  // Row 0 is nearest but passes 12 km away; row 1 will be overhead in 40 s.
  std::vector<AircraftRow> rows = {airborne(), airborne(), airborne()};
  std::vector<CpaPrediction> cpas = {cpa(20.0f, 12.0f), cpa(40.0f, 1.0f), cpa(90.0f, 0.5f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(1, p.index);
  TEST_ASSERT_TRUE(p.cpa.found);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, p.cpa.t_cpa_seconds);
}

static void test_pick_skips_ground_traffic_and_long_gone(void) {
  std::vector<AircraftRow> rows = {airborne(), airborne(), airborne()};
  rows[0].aircraft.on_ground = true;  // taxiing at the airport
  std::vector<CpaPrediction> cpas = {cpa(5.0f, 0.2f), cpa(-30.0f, 0.5f), cpa(70.0f, 2.0f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(2, p.index);
}

static void test_pick_just_passed_still_counts(void) {
  // Within the -10 s "MINĄŁ" window, the one overhead right now wins.
  std::vector<AircraftRow> rows = {airborne(), airborne()};
  std::vector<CpaPrediction> cpas = {cpa(50.0f, 1.0f), cpa(-4.0f, 0.3f)};
  TEST_ASSERT_EQUAL_INT(1, pickNowAircraft(rows, cpas, kOverheadRadiusKm).index);
}

static void test_pick_without_overhead_candidate_shows_nearest_airborne(void) {
  std::vector<AircraftRow> rows = {airborne(), airborne()};
  rows[0].aircraft.on_ground = true;
  std::vector<CpaPrediction> cpas = {cpa(5.0f, 30.0f), cpa(20.0f, 25.0f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(1, p.index);  // first airborne row (rows are nearest-first)
  TEST_ASSERT_FALSE(p.cpa.found);     // -> "NIE PRZELECI NAD TOBĄ"
}

static void test_pick_empty(void) {
  std::vector<AircraftRow> rows;
  std::vector<CpaPrediction> cpas;
  TEST_ASSERT_EQUAL_INT(-1, pickNowAircraft(rows, cpas, kOverheadRadiusKm).index);
}

}  // namespace

void run_now_pick_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_pick_prefers_soonest_overhead_not_nearest);
  RUN_TEST(test_pick_skips_ground_traffic_and_long_gone);
  RUN_TEST(test_pick_just_passed_still_counts);
  RUN_TEST(test_pick_without_overhead_candidate_shows_nearest_airborne);
  RUN_TEST(test_pick_empty);
}
