#include <unity.h>

#include "aircraft_lookup.h"

// The cache is shared static state across the whole test binary — reset it
// before every test so cases don't leak into each other.
void setUp(void) { aircraftLookupClearCacheForTesting(); }
void tearDown(void) {}

// ---- parseAircraftLookupResponse ----------------------------------------

static const char *kSampleFoundResponse =
    R"({"ICAOTypeCode":"A319","Manufacturer":"Airbus","ModeS":"4010EE",)"
    R"("OperatorFlagCode":"EZY","RegisteredOwners":"easyJet Airline",)"
    R"("Registration":"G-EZBZ","Type":"A319 111"})";

static const char *kSampleNotFoundResponse = R"({"status":"404","error":"Aircraft not found."})";

static void test_parse_found_response(void) {
  AircraftInfo info = parseAircraftLookupResponse(kSampleFoundResponse);

  TEST_ASSERT_TRUE(info.found);
  TEST_ASSERT_EQUAL_STRING("easyJet Airline", info.airline.c_str());
  TEST_ASSERT_EQUAL_STRING("A319 111", info.aircraft_type.c_str());
  TEST_ASSERT_EQUAL_STRING("G-EZBZ", info.registration.c_str());
}

static void test_parse_not_found_response(void) {
  AircraftInfo info = parseAircraftLookupResponse(kSampleNotFoundResponse);

  TEST_ASSERT_FALSE(info.found);
  TEST_ASSERT_TRUE(info.airline.empty());
  TEST_ASSERT_TRUE(info.aircraft_type.empty());
  TEST_ASSERT_TRUE(info.registration.empty());
}

static void test_parse_malformed_json_does_not_crash(void) {
  AircraftInfo info = parseAircraftLookupResponse("not json at all");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_empty_body(void) {
  AircraftInfo info = parseAircraftLookupResponse("");
  TEST_ASSERT_FALSE(info.found);
}

// ---- lookupAircraftWithFetcher / caching ---------------------------------

static int gFetchCallCount = 0;

static std::string stubFetchFound(const std::string &icao24) {
  gFetchCallCount++;
  return kSampleFoundResponse;
}

static std::string stubFetchNotFound(const std::string &icao24) {
  gFetchCallCount++;
  return kSampleNotFoundResponse;
}

static void test_lookup_hits_http_once_then_caches(void) {
  gFetchCallCount = 0;

  AircraftInfo first = lookupAircraftWithFetcher("4010ee", stubFetchFound);
  AircraftInfo second = lookupAircraftWithFetcher("4010ee", stubFetchFound);
  AircraftInfo third = lookupAircraftWithFetcher("4010ee", stubFetchFound);

  TEST_ASSERT_EQUAL_INT(1, gFetchCallCount);
  TEST_ASSERT_TRUE(first.found);
  TEST_ASSERT_TRUE(second.found);
  TEST_ASSERT_TRUE(third.found);
  TEST_ASSERT_EQUAL_STRING("easyJet Airline", third.airline.c_str());
}

static void test_lookup_caches_misses_too(void) {
  gFetchCallCount = 0;

  AircraftInfo first = lookupAircraftWithFetcher("ffffff", stubFetchNotFound);
  AircraftInfo second = lookupAircraftWithFetcher("ffffff", stubFetchNotFound);

  TEST_ASSERT_EQUAL_INT(1, gFetchCallCount);
  TEST_ASSERT_FALSE(first.found);
  TEST_ASSERT_FALSE(second.found);
}

static void test_lookup_fetches_separately_per_icao24(void) {
  gFetchCallCount = 0;

  lookupAircraftWithFetcher("111111", stubFetchFound);
  lookupAircraftWithFetcher("222222", stubFetchFound);

  TEST_ASSERT_EQUAL_INT(2, gFetchCallCount);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_found_response);
  RUN_TEST(test_parse_not_found_response);
  RUN_TEST(test_parse_malformed_json_does_not_crash);
  RUN_TEST(test_parse_empty_body);

  RUN_TEST(test_lookup_hits_http_once_then_caches);
  RUN_TEST(test_lookup_caches_misses_too);
  RUN_TEST(test_lookup_fetches_separately_per_icao24);

  return UNITY_END();
}
