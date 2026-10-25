// Suite test_opensky: the OpenSky data source (parsing, limits, login). See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_opensky_client_tests();
void run_opensky_quota_tests();

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
  if (RUN_MODULE(opensky_client)) run_opensky_client_tests();
  if (RUN_MODULE(opensky_quota)) run_opensky_quota_tests();
  return UNITY_END();
}
