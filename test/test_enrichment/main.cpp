// Suite test_enrichment: aircraft, route and airport data (hexdb.io, caches, NVS). See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_aircraft_lookup_tests();
void run_route_lookup_tests();
void run_lookup_store_tests();
void run_lookup_budget_tests();
void run_aircraft_names_tests();
void run_place_names_tests();

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
  if (RUN_MODULE(aircraft_lookup)) run_aircraft_lookup_tests();
  if (RUN_MODULE(route_lookup)) run_route_lookup_tests();
  if (RUN_MODULE(lookup_store)) run_lookup_store_tests();
  if (RUN_MODULE(lookup_budget)) run_lookup_budget_tests();
  if (RUN_MODULE(aircraft_names)) run_aircraft_names_tests();
  if (RUN_MODULE(place_names)) run_place_names_tests();
  return UNITY_END();
}
