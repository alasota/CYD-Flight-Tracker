// Suite test_platform: concurrency, perf, WiFi, time, the shared core. See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_core_link_tests();
void run_perf_stats_tests();
void run_wifi_manager_tests();
void run_time_sync_tests();
void run_sun_times_tests();

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
  if (RUN_MODULE(core_link)) run_core_link_tests();
  if (RUN_MODULE(perf_stats)) run_perf_stats_tests();
  if (RUN_MODULE(wifi_manager)) run_wifi_manager_tests();
  if (RUN_MODULE(time_sync)) run_time_sync_tests();
  if (RUN_MODULE(sun_times)) run_sun_times_tests();
  return UNITY_END();
}
