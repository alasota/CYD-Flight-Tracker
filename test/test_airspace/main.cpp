// Suite test_airspace: the sky over home: phase, overhead pass, sky snapshot. See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_flight_phase_tests();
void run_cpa_predictor_tests();
void run_poll_pipeline_tests();

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
  return UNITY_END();
}
