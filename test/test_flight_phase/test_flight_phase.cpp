#include <unity.h>

#include "flight_phase.h"

void setUp(void) {}
void tearDown(void) {}

// ---- one case per Phase value ----------------------------------------------

static void test_on_ground_forces_none(void) {
  // Even with signals that would otherwise say "taking off" or "landing",
  // on_ground wins.
  TEST_ASSERT_TRUE(classifyPhase(1.0f, 10.0f, true, 5.0f, 3.0f) == Phase::NONE);
  TEST_ASSERT_TRUE(classifyPhase(1.0f, -10.0f, true, 5.0f, 3.0f) == Phase::NONE);
  TEST_ASSERT_TRUE(classifyPhase(50.0f, 0.0f, true, 5.0f, 3.0f) == Phase::NONE);
}

static void test_takeoff_near_airport_climbing(void) {
  TEST_ASSERT_TRUE(classifyPhase(2.0f, 5.0f, false, 5.0f, 3.0f) == Phase::TAKEOFF);
}

static void test_landing_near_airport_descending(void) {
  TEST_ASSERT_TRUE(classifyPhase(2.0f, -5.0f, false, 5.0f, 3.0f) == Phase::LANDING);
}

static void test_overflight_far_from_airport(void) {
  TEST_ASSERT_TRUE(classifyPhase(50.0f, 10.0f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
}

static void test_overflight_near_airport_but_level(void) {
  // Near an airport but not climbing/descending fast enough - level flyover.
  TEST_ASSERT_TRUE(classifyPhase(2.0f, 1.0f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
  TEST_ASSERT_TRUE(classifyPhase(2.0f, -1.0f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
  TEST_ASSERT_TRUE(classifyPhase(2.0f, 0.0f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
}

// ---- boundary: exactly at near_airport_km ----------------------------------

static void test_exactly_at_near_airport_threshold_counts_as_near(void) {
  // distance_km == near_airport_km -> inclusive, still "near".
  TEST_ASSERT_TRUE(classifyPhase(5.0f, 5.0f, false, 5.0f, 3.0f) == Phase::TAKEOFF);
  TEST_ASSERT_TRUE(classifyPhase(5.0f, -5.0f, false, 5.0f, 3.0f) == Phase::LANDING);
}

static void test_just_past_near_airport_threshold_is_overflight(void) {
  TEST_ASSERT_TRUE(classifyPhase(5.01f, 5.0f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
}

// ---- boundary: exactly at climb_threshold_mps ------------------------------

static void test_exactly_at_climb_threshold_counts_as_takeoff(void) {
  TEST_ASSERT_TRUE(classifyPhase(1.0f, 3.0f, false, 5.0f, 3.0f) == Phase::TAKEOFF);
}

static void test_just_below_climb_threshold_is_overflight(void) {
  TEST_ASSERT_TRUE(classifyPhase(1.0f, 2.99f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
}

static void test_exactly_at_negative_climb_threshold_counts_as_landing(void) {
  TEST_ASSERT_TRUE(classifyPhase(1.0f, -3.0f, false, 5.0f, 3.0f) == Phase::LANDING);
}

static void test_just_above_negative_climb_threshold_is_overflight(void) {
  TEST_ASSERT_TRUE(classifyPhase(1.0f, -2.99f, false, 5.0f, 3.0f) == Phase::OVERFLIGHT);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_on_ground_forces_none);
  RUN_TEST(test_takeoff_near_airport_climbing);
  RUN_TEST(test_landing_near_airport_descending);
  RUN_TEST(test_overflight_far_from_airport);
  RUN_TEST(test_overflight_near_airport_but_level);

  RUN_TEST(test_exactly_at_near_airport_threshold_counts_as_near);
  RUN_TEST(test_just_past_near_airport_threshold_is_overflight);

  RUN_TEST(test_exactly_at_climb_threshold_counts_as_takeoff);
  RUN_TEST(test_just_below_climb_threshold_is_overflight);
  RUN_TEST(test_exactly_at_negative_climb_threshold_counts_as_landing);
  RUN_TEST(test_just_above_negative_climb_threshold_is_overflight);

  return UNITY_END();
}
