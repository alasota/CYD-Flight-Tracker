// aircraft_state: when an aircraft counts as on the ground. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "airspace/core/aircraft_state.h"

namespace {

static void test_infer_on_ground_rules(void) {
  AircraftState a;
  a.on_ground = false;
  a.has_altitude = true;
  a.baro_altitude = 900.0f;
  a.has_velocity = true;
  a.velocity = 100.0f;
  TEST_ASSERT_FALSE(inferOnGround(a));
  a.velocity = 10.0f;  // altitude reported but crawling -> stale altitude at the gate
  TEST_ASSERT_TRUE(inferOnGround(a));
}

}  // namespace

void run_aircraft_state_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_infer_on_ground_rules);
}
