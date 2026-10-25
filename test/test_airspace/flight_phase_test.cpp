// flight_phase: takeoff / landing / overflight classification. CLAUDE.md "Flight phase".
#include <unity.h>

#include "flight_phase.h"
#include "test_support.h"

namespace {

// ---- one case per Phase value ----------------------------------------------

static void test_on_ground_forces_none(void) {
  // Even with signals that would otherwise say "taking off" or "landing",
  // on_ground wins.
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(1.0f, 10.0f, true, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(1.0f, -10.0f, true, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(50.0f, 0.0f, true, 5.0f, 3.0f));
}

static void test_takeoff_near_airport_climbing(void) {
  ASSERT_ENUM_EQ(Phase::TAKEOFF, classifyPhase(2.0f, 5.0f, false, 5.0f, 3.0f));
}

static void test_landing_near_airport_descending(void) {
  ASSERT_ENUM_EQ(Phase::LANDING, classifyPhase(2.0f, -5.0f, false, 5.0f, 3.0f));
}

static void test_far_from_airport_has_no_phase(void) {
  // CLAUDE.md "Flight phase": beyond near_airport_km -> NONE, no icon. (The
  // first implementation returned OVERFLIGHT here, so every distant aircraft
  // showed the same icon and the FAZA column told you nothing.)
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(50.0f, 10.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(50.0f, -10.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(50.0f, 0.0f, false, 5.0f, 3.0f));
}

static void test_overflight_near_airport_but_level(void) {
  // Near an airport but not climbing/descending fast enough - level flyover.
  ASSERT_ENUM_EQ(Phase::OVERFLIGHT, classifyPhase(2.0f, 1.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::OVERFLIGHT, classifyPhase(2.0f, -1.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::OVERFLIGHT, classifyPhase(2.0f, 0.0f, false, 5.0f, 3.0f));
}

// ---- boundary: exactly at near_airport_km ----------------------------------

static void test_exactly_at_near_airport_threshold_counts_as_near(void) {
  // distance_km == near_airport_km -> inclusive, still "near".
  ASSERT_ENUM_EQ(Phase::TAKEOFF, classifyPhase(5.0f, 5.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::LANDING, classifyPhase(5.0f, -5.0f, false, 5.0f, 3.0f));
}

static void test_just_past_near_airport_threshold_is_none(void) {
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(5.01f, 5.0f, false, 5.0f, 3.0f));
  ASSERT_ENUM_EQ(Phase::NONE, classifyPhase(5.01f, 0.0f, false, 5.0f, 3.0f));
}

// ---- boundary: exactly at climb_threshold_mps ------------------------------

static void test_exactly_at_climb_threshold_counts_as_takeoff(void) {
  ASSERT_ENUM_EQ(Phase::TAKEOFF, classifyPhase(1.0f, 3.0f, false, 5.0f, 3.0f));
}

static void test_just_below_climb_threshold_is_overflight(void) {
  ASSERT_ENUM_EQ(Phase::OVERFLIGHT, classifyPhase(1.0f, 2.99f, false, 5.0f, 3.0f));
}

static void test_exactly_at_negative_climb_threshold_counts_as_landing(void) {
  ASSERT_ENUM_EQ(Phase::LANDING, classifyPhase(1.0f, -3.0f, false, 5.0f, 3.0f));
}

static void test_just_above_negative_climb_threshold_is_overflight(void) {
  ASSERT_ENUM_EQ(Phase::OVERFLIGHT, classifyPhase(1.0f, -2.99f, false, 5.0f, 3.0f));
}

}  // namespace

void run_flight_phase_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_on_ground_forces_none);
  RUN_TEST(test_takeoff_near_airport_climbing);
  RUN_TEST(test_landing_near_airport_descending);
  RUN_TEST(test_far_from_airport_has_no_phase);
  RUN_TEST(test_overflight_near_airport_but_level);

  RUN_TEST(test_exactly_at_near_airport_threshold_counts_as_near);
  RUN_TEST(test_just_past_near_airport_threshold_is_none);

  RUN_TEST(test_exactly_at_climb_threshold_counts_as_takeoff);
  RUN_TEST(test_just_below_climb_threshold_is_overflight);
  RUN_TEST(test_exactly_at_negative_climb_threshold_counts_as_landing);
  RUN_TEST(test_just_above_negative_climb_threshold_is_overflight);

}
