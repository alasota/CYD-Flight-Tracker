#include <unity.h>

#include "route_lookup.h"

// Both caches are shared static state across the whole test binary —
// reset them before every test so cases don't leak into each other.
void setUp(void) {
  routeLookupClearCacheForTesting();
  airportLookupClearCacheForTesting();
}
void tearDown(void) {}

// ---- parseRouteLookupResponse ----------------------------------------------

static void test_parse_route_splits_origin_dest(void) {
  RouteInfo info = parseRouteLookupResponse(
      R"({"flight":"EIN17A","route":"EIDW-EGLL","updatetime":1397991739})");

  TEST_ASSERT_TRUE(info.found);
  TEST_ASSERT_EQUAL_STRING("EIDW", info.origin_icao.c_str());
  TEST_ASSERT_EQUAL_STRING("EGLL", info.dest_icao.c_str());
}

static void test_parse_route_not_found(void) {
  RouteInfo info = parseRouteLookupResponse(R"({"status":"404","error":"Route not found."})");

  TEST_ASSERT_FALSE(info.found);
  TEST_ASSERT_TRUE(info.origin_icao.empty());
  TEST_ASSERT_TRUE(info.dest_icao.empty());
}

static void test_parse_route_empty_body(void) {
  // hexdb.io's real 404 for this endpoint may carry no body at all.
  RouteInfo info = parseRouteLookupResponse("");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_route_malformed_json(void) {
  RouteInfo info = parseRouteLookupResponse("not json at all");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_route_missing_separator(void) {
  RouteInfo info = parseRouteLookupResponse(R"({"route":"EIDWEGLL"})");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_route_rejects_oversized_body(void) {
  std::string oversized(kMaxRouteResponseBytes + 1, 'x');
  RouteInfo info = parseRouteLookupResponse(oversized);
  TEST_ASSERT_FALSE(info.found);
}

// ---- parseAirportLookupResponse --------------------------------------------

static void test_parse_airport_found(void) {
  AirportInfo info = parseAirportLookupResponse(
      R"({"country_code":"GB","region_name":"England","iata":"LHR","icao":"EGLL",)"
      R"("airport":"Heathrow Airport","latitude":51.4775,"longitude":-0.461389})");

  TEST_ASSERT_TRUE(info.found);
  TEST_ASSERT_EQUAL_STRING("GB", info.country_code.c_str());
  TEST_ASSERT_EQUAL_STRING("LHR", info.iata_code.c_str());
}

static void test_parse_airport_not_found(void) {
  AirportInfo info = parseAirportLookupResponse(R"({"status":"404","error":"Airport not found."})");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_airport_empty_body(void) {
  AirportInfo info = parseAirportLookupResponse("");
  TEST_ASSERT_FALSE(info.found);
}

static void test_parse_airport_rejects_oversized_body(void) {
  std::string oversized(kMaxAirportResponseBytes + 1, 'x');
  AirportInfo info = parseAirportLookupResponse(oversized);
  TEST_ASSERT_FALSE(info.found);
}

// ---- lookupRouteWithFetcher / caching --------------------------------------

static int gRouteFetchCallCount = 0;

static HexdbFetchResult stubRouteFetchFound(const std::string &callsign) {
  gRouteFetchCallCount++;
  HexdbFetchResult r;
  r.response_ok = true;
  r.body = R"({"flight":"EIN17A","route":"EIDW-EGLL","updatetime":1397991739})";
  return r;
}

static HexdbFetchResult stubRouteFetchConfirmedNotFound(const std::string &callsign) {
  gRouteFetchCallCount++;
  HexdbFetchResult r;
  r.response_ok = true;  // a confirmed 404, per hexdb.io's real behavior
  r.body = "";
  return r;
}

static HexdbFetchResult stubRouteFetchTransportFailure(const std::string &callsign) {
  gRouteFetchCallCount++;
  return HexdbFetchResult{};  // response_ok=false
}

static void test_lookup_route_hits_http_once_then_caches(void) {
  gRouteFetchCallCount = 0;

  RouteInfo first = lookupRouteWithFetcher("EIN17A", stubRouteFetchFound);
  RouteInfo second = lookupRouteWithFetcher("EIN17A", stubRouteFetchFound);
  RouteInfo third = lookupRouteWithFetcher("EIN17A", stubRouteFetchFound);

  TEST_ASSERT_EQUAL_INT(1, gRouteFetchCallCount);
  TEST_ASSERT_TRUE(first.found);
  TEST_ASSERT_TRUE(second.found);
  TEST_ASSERT_TRUE(third.found);
  TEST_ASSERT_EQUAL_STRING("EIDW", third.origin_icao.c_str());
  TEST_ASSERT_EQUAL_STRING("EGLL", third.dest_icao.c_str());
}

static void test_lookup_route_caches_confirmed_not_found(void) {
  gRouteFetchCallCount = 0;

  RouteInfo first = lookupRouteWithFetcher("ZZZ000", stubRouteFetchConfirmedNotFound);
  RouteInfo second = lookupRouteWithFetcher("ZZZ000", stubRouteFetchConfirmedNotFound);

  TEST_ASSERT_EQUAL_INT(1, gRouteFetchCallCount);
  TEST_ASSERT_FALSE(first.found);
  TEST_ASSERT_FALSE(second.found);
}

static void test_lookup_route_does_not_cache_transport_failure(void) {
  gRouteFetchCallCount = 0;

  RouteInfo first = lookupRouteWithFetcher("EIN17A", stubRouteFetchTransportFailure);
  RouteInfo second = lookupRouteWithFetcher("EIN17A", stubRouteFetchTransportFailure);

  TEST_ASSERT_EQUAL_INT(2, gRouteFetchCallCount);
  TEST_ASSERT_FALSE(first.found);
  TEST_ASSERT_FALSE(second.found);
}

static void test_lookup_route_fetches_separately_per_callsign(void) {
  gRouteFetchCallCount = 0;

  lookupRouteWithFetcher("EIN17A", stubRouteFetchFound);
  lookupRouteWithFetcher("DLH9LH", stubRouteFetchFound);

  TEST_ASSERT_EQUAL_INT(2, gRouteFetchCallCount);
}

// ---- lookupAirportWithFetcher / caching ------------------------------------

static int gAirportFetchCallCount = 0;

static HexdbFetchResult stubAirportFetchFound(const std::string &code) {
  gAirportFetchCallCount++;
  HexdbFetchResult r;
  r.response_ok = true;
  r.body = R"({"country_code":"GB","iata":"LHR","icao":"EGLL"})";
  return r;
}

static void test_lookup_airport_hits_http_once_then_caches(void) {
  gAirportFetchCallCount = 0;

  // Keyed by ICAO code (e.g. "EGLL"), not IATA — so it chains directly off
  // RouteInfo::origin_icao/dest_icao without a code-system conversion.
  AirportInfo first = lookupAirportWithFetcher("EGLL", stubAirportFetchFound);
  AirportInfo second = lookupAirportWithFetcher("EGLL", stubAirportFetchFound);

  TEST_ASSERT_EQUAL_INT(1, gAirportFetchCallCount);
  TEST_ASSERT_TRUE(first.found);
  TEST_ASSERT_TRUE(second.found);
  TEST_ASSERT_EQUAL_STRING("GB", second.country_code.c_str());
  TEST_ASSERT_EQUAL_STRING("LHR", second.iata_code.c_str());
}

static void test_lookup_airport_fetches_separately_per_code(void) {
  gAirportFetchCallCount = 0;

  lookupAirportWithFetcher("EGLL", stubAirportFetchFound);
  lookupAirportWithFetcher("EIDW", stubAirportFetchFound);

  TEST_ASSERT_EQUAL_INT(2, gAirportFetchCallCount);
}

// ---- routeLookupIsCached (review notes 1.1 pattern) ------------------------

static void test_route_is_cached_reflects_confirmed_results_only(void) {
  TEST_ASSERT_FALSE(routeLookupIsCached("EIN17A"));

  lookupRouteWithFetcher("EIN17A", stubRouteFetchFound);
  TEST_ASSERT_TRUE(routeLookupIsCached("EIN17A"));

  TEST_ASSERT_FALSE(routeLookupIsCached("ZZZ000"));
  lookupRouteWithFetcher("ZZZ000", stubRouteFetchConfirmedNotFound);
  TEST_ASSERT_TRUE(routeLookupIsCached("ZZZ000"));  // a confirmed miss counts as cached too

  TEST_ASSERT_FALSE(routeLookupIsCached("QQQ111"));
  lookupRouteWithFetcher("QQQ111", stubRouteFetchTransportFailure);
  TEST_ASSERT_FALSE(routeLookupIsCached("QQQ111"));  // a failed attempt does not
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_route_splits_origin_dest);
  RUN_TEST(test_parse_route_not_found);
  RUN_TEST(test_parse_route_empty_body);
  RUN_TEST(test_parse_route_malformed_json);
  RUN_TEST(test_parse_route_missing_separator);
  RUN_TEST(test_parse_route_rejects_oversized_body);

  RUN_TEST(test_parse_airport_found);
  RUN_TEST(test_parse_airport_not_found);
  RUN_TEST(test_parse_airport_empty_body);
  RUN_TEST(test_parse_airport_rejects_oversized_body);

  RUN_TEST(test_lookup_route_hits_http_once_then_caches);
  RUN_TEST(test_lookup_route_caches_confirmed_not_found);
  RUN_TEST(test_lookup_route_does_not_cache_transport_failure);
  RUN_TEST(test_lookup_route_fetches_separately_per_callsign);

  RUN_TEST(test_lookup_airport_hits_http_once_then_caches);
  RUN_TEST(test_lookup_airport_fetches_separately_per_code);

  RUN_TEST(test_route_is_cached_reflects_confirmed_results_only);

  return UNITY_END();
}
