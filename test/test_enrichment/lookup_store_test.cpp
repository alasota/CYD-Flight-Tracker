// lookup_store: NVS record encode / decode of the lookup caches. CLAUDE.md "Code conventions" (lookup_store).
#include <unity.h>

#include <string>

#include "enrichment/domain/aircraft_lookup.h"
#include "enrichment/domain/lookup_store.h"
#include "enrichment/domain/route_lookup.h"

namespace {

// ---- round trips ----------------------------------------------------------------

static void test_aircraft_round_trip(void) {
  AircraftInfo in;
  in.found = true;
  in.airline = "LOT Polish Airlines";
  in.aircraft_type = "737MAX 8";
  in.icao_type = "B38M";
  in.registration = "SP-LVA";
  AircraftInfo out;
  TEST_ASSERT_TRUE(decodeAircraftInfo(encodeAircraftInfo(in), out));
  TEST_ASSERT_TRUE(out.found);
  TEST_ASSERT_EQUAL_STRING("LOT Polish Airlines", out.airline.c_str());
  TEST_ASSERT_EQUAL_STRING("737MAX 8", out.aircraft_type.c_str());
  TEST_ASSERT_EQUAL_STRING("B38M", out.icao_type.c_str());
  TEST_ASSERT_EQUAL_STRING("SP-LVA", out.registration.c_str());
}

static void test_confirmed_miss_round_trips_too(void) {
  // "hexdb.io doesn't know it" is worth remembering as well.
  AircraftInfo miss;
  AircraftInfo out;
  out.found = true;
  TEST_ASSERT_TRUE(decodeAircraftInfo(encodeAircraftInfo(miss), out));
  TEST_ASSERT_FALSE(out.found);
}

static void test_route_round_trip(void) {
  RouteInfo in;
  in.found = true;
  in.origin_icao = "LTAI";
  in.dest_icao = "EPWA";
  RouteInfo out;
  TEST_ASSERT_TRUE(decodeRouteInfo(encodeRouteInfo(in), out));
  TEST_ASSERT_TRUE(out.found);
  TEST_ASSERT_EQUAL_STRING("LTAI", out.origin_icao.c_str());
  TEST_ASSERT_EQUAL_STRING("EPWA", out.dest_icao.c_str());
}

static void test_airport_round_trip(void) {
  AirportInfo in;
  in.found = true;
  in.iata_code = "WAW";
  in.country_code = "PL";
  in.name = "Warsaw Chopin Airport";
  AirportInfo out;
  TEST_ASSERT_TRUE(decodeAirportInfo(encodeAirportInfo(in), out));
  TEST_ASSERT_TRUE(out.found);
  TEST_ASSERT_EQUAL_STRING("WAW", out.iata_code.c_str());
  TEST_ASSERT_EQUAL_STRING("PL", out.country_code.c_str());
  TEST_ASSERT_EQUAL_STRING("Warsaw Chopin Airport", out.name.c_str());
}

// ---- corrupt / foreign data is rejected, never half-applied ------------------------

static void test_decode_rejects_garbage(void) {
  AircraftInfo a;
  RouteInfo r;
  AirportInfo p;
  TEST_ASSERT_FALSE(decodeAircraftInfo("", a));
  TEST_ASSERT_FALSE(decodeAircraftInfo("hello", a));
  TEST_ASSERT_FALSE(decodeRouteInfo("9\x1F" "1\x1F" "A\x1F" "B", r));  // unknown version
  TEST_ASSERT_FALSE(decodeAirportInfo(encodeRouteInfo(RouteInfo{}), p));  // wrong field count
}

static void test_separator_inside_a_value_does_not_break_the_record(void) {
  AircraftInfo in;
  in.found = true;
  in.airline = std::string("Odd\x1F" "Name");
  AircraftInfo out;
  TEST_ASSERT_TRUE(decodeAircraftInfo(encodeAircraftInfo(in), out));
  TEST_ASSERT_EQUAL_STRING("Odd Name", out.airline.c_str());  // separator replaced
}

// ---- seeding the RAM caches from storage --------------------------------------------

static void test_seeded_entries_count_as_cached(void) {
  AircraftInfo a;
  a.found = true;
  a.airline = "SunExpress";
  aircraftLookupSeedCache("4bb1a2", a);
  TEST_ASSERT_TRUE(aircraftLookupIsCached("4bb1a2"));

  RouteInfo r;
  r.found = true;
  routeLookupSeedCache("SXS6JZ", r);
  TEST_ASSERT_TRUE(routeLookupIsCached("SXS6JZ"));

  AirportInfo p;
  p.found = true;
  airportLookupSeedCache("LTAI", p);
  TEST_ASSERT_TRUE(airportLookupIsCached("LTAI"));
}

}  // namespace

void run_lookup_store_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_aircraft_round_trip);
  RUN_TEST(test_confirmed_miss_round_trips_too);
  RUN_TEST(test_route_round_trip);
  RUN_TEST(test_airport_round_trip);
  RUN_TEST(test_decode_rejects_garbage);
  RUN_TEST(test_separator_inside_a_value_does_not_break_the_record);
  RUN_TEST(test_seeded_entries_count_as_cached);

}
