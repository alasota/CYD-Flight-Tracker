// Suite test_airspace: the sky over home: phase, overhead pass, sky snapshot. See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_flight_phase_tests();
void run_cpa_predictor_tests();
void run_poll_pipeline_tests();
void run_aircraft_state_tests();
void run_tracked_flight_tests();
void run_flight_ordering_tests();
void run_now_pick_tests();

void setUp(void) { resetAllForTesting(); }
void tearDown(void) {}

// -DTEST_MODULE=<module> runs one module's tests (test/README.md). Not
// TEST_ONLY as spec 07 first said: unity.h already defines TEST_ONLY().
#ifdef TEST_MODULE
#define RUN_MODULE(m) testModuleIs(TEST_SUPPORT_STR(TEST_MODULE), #m)
#else
#define RUN_MODULE(m) true
#endif

int main(int, char **) {
  UNITY_BEGIN();
  if (RUN_MODULE(flight_phase)) run_flight_phase_tests();
  if (RUN_MODULE(cpa_predictor)) run_cpa_predictor_tests();
  if (RUN_MODULE(poll_pipeline)) run_poll_pipeline_tests();
  if (RUN_MODULE(aircraft_state)) run_aircraft_state_tests();
  if (RUN_MODULE(tracked_flight)) run_tracked_flight_tests();
  if (RUN_MODULE(flight_ordering)) run_flight_ordering_tests();
  if (RUN_MODULE(now_pick)) run_now_pick_tests();
  return UNITY_END();
}
