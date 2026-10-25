// poll_pipeline: one /states/all answer -> DataSnapshot. CLAUDE.md "Concurrency — two cores", spec/04.
#include <unity.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fixtures.h"
#include "lookup_cache.h"
#include "poll_pipeline.h"

namespace {

// Stub hexdb.io from fixtures.h (reset before every test).
std::set<std::string> &knownAircraft = stubHexdb().knownAircraft;
std::set<std::string> &knownRoutes = stubHexdb().knownRoutes;
std::set<std::string> &knownAirports = stubHexdb().knownAirports;
std::vector<std::string> &calls = stubHexdb().calls;

static constexpr float kHomeLat = 52.0f;
static constexpr float kHomeLon = 21.0f;

// near airborne, far airborne, ground right next to home, no position
static std::vector<Aircraft> sky() {
  std::vector<Aircraft> v;
  v.push_back(makeAircraft("far001", "FAR1", 52.40f, 21.0f));
  v.push_back(makeAircraft("gnd001", "GND1", 52.002f, 21.0f, groundOpts()));
  Aircraft np = makeAircraft("nopos1", "NOP1", 0.0f, 0.0f);
  np.has_position = false;
  v.push_back(np);
  v.push_back(makeAircraft("near01", "NEAR1", 52.05f, 21.0f));
  return v;
}

static PipelineParams params(int budget, int airportBudget) {
  PipelineParams p;
  p.homeLat = kHomeLat;
  p.homeLon = kHomeLon;
  p.lookupBudget = budget;
  p.airportBudget = airportBudget;
  return p;
}

static void test_rows_ordered_airborne_ground_no_position(void) {
  const DataSnapshot s = buildDataSnapshot(sky(), params(0, 0), stubPorts(), 1234);
  TEST_ASSERT_EQUAL_size_t(4, s.rows.size());
  TEST_ASSERT_EQUAL_STRING("near01", s.rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("far001", s.rows[1].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("gnd001", s.rows[2].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("nopos1", s.rows[3].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_UINT32(1234, s.polledAtMs);
}

static void test_cache_only_makes_no_requests_and_marks_pending(void) {
  const DataSnapshot s =
      buildDataSnapshot(sky(), cacheOnly(params(2, 3)), stubPorts(), 0);
  TEST_ASSERT_EQUAL_size_t(0, calls.size());
  TEST_ASSERT_EQUAL_UINT32(0, s.fetches);
  for (const AircraftRow &r : s.rows) {
    TEST_ASSERT_TRUE(r.info_pending);
    TEST_ASSERT_TRUE(r.route_pending);
  }
}

static void test_cache_hits_are_used_in_phase_a(void) {
  knownAircraft.insert("far001");
  knownRoutes.insert("FAR1");
  knownAirports.insert("EPWA");
  const DataSnapshot s = buildDataSnapshot(sky(), cacheOnly(params(2, 3)), stubPorts(), 0);
  TEST_ASSERT_EQUAL_UINT32(0, s.fetches);  // all three answered from "cache"
  const AircraftRow &far = s.rows[1];
  TEST_ASSERT_FALSE(far.info_pending);
  TEST_ASSERT_EQUAL_STRING("Airline far001", far.info.airline.c_str());
  TEST_ASSERT_TRUE(far.route.found);
  TEST_ASSERT_FALSE(far.origin_airport_pending);
  TEST_ASSERT_EQUAL_STRING("WAW", far.origin_airport.iata_code.c_str());
  TEST_ASSERT_TRUE(far.dest_airport_pending);  // LIRF not cached, no budget
  TEST_ASSERT_TRUE(s.rows[0].info_pending);    // near01 not cached
}

static void test_budget_goes_to_the_nearest_first(void) {
  const DataSnapshot s = buildDataSnapshot(sky(), params(2, 0), stubPorts(), 0);
  // Budget 2 shared: near01's aircraft + route.
  TEST_ASSERT_EQUAL_UINT32(2, s.fetches);
  TEST_ASSERT_EQUAL_STRING("ac:near01", calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("rt:NEAR1", calls[1].c_str());
  TEST_ASSERT_FALSE(s.rows[0].info_pending);
  TEST_ASSERT_FALSE(s.rows[0].route_pending);
  TEST_ASSERT_TRUE(s.rows[1].info_pending);
  // A route found, no airport budget: both ends pending ("...").
  TEST_ASSERT_TRUE(s.rows[0].origin_airport_pending);
  TEST_ASSERT_TRUE(s.rows[0].dest_airport_pending);
}

static void test_airports_of_new_routes_in_the_same_pass(void) {
  const DataSnapshot s = buildDataSnapshot(sky(), params(2, 3), stubPorts(), 0);
  TEST_ASSERT_EQUAL_UINT32(4, s.fetches);  // aircraft, route, two airports
  TEST_ASSERT_EQUAL_STRING("WAW", s.rows[0].origin_airport.iata_code.c_str());
  TEST_ASSERT_EQUAL_STRING("FCO", s.rows[0].dest_airport.iata_code.c_str());
  TEST_ASSERT_FALSE(s.rows[0].origin_airport_pending);
}

static void test_phase_b_without_new_keys_equals_phase_a(void) {
  for (const char *k : {"near01", "far001", "gnd001", "nopos1"}) knownAircraft.insert(k);
  for (const char *k : {"NEAR1", "FAR1", "GND1", "NOP1"}) knownRoutes.insert(k);
  knownAirports.insert("EPWA");
  knownAirports.insert("LIRF");
  const DataSnapshot a = buildDataSnapshot(sky(), cacheOnly(params(2, 3)), stubPorts(), 0);
  const DataSnapshot b = buildDataSnapshot(sky(), params(2, 3), stubPorts(), 0);
  TEST_ASSERT_EQUAL_UINT32(0, b.fetches);  // nothing new -> B isn't published
  TEST_ASSERT_EQUAL_size_t(a.rows.size(), b.rows.size());
  for (size_t i = 0; i < a.rows.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(a.rows[i].info.airline.c_str(), b.rows[i].info.airline.c_str());
    TEST_ASSERT_EQUAL(a.rows[i].route_pending, b.rows[i].route_pending);
  }
}

static void test_now_pick_points_at_the_sorted_row(void) {
  // near01 is 5.5 km north, flying south straight over home.
  const DataSnapshot s = buildDataSnapshot(sky(), params(0, 0), stubPorts(), 0);
  TEST_ASSERT_EQUAL_INT(0, s.nowRowIndex);
  TEST_ASSERT_TRUE(s.cpa.found);
  TEST_ASSERT_TRUE(s.cpa.t_cpa_seconds > 0.0f);
  TEST_ASSERT_EQUAL_STRING("near01",
                           s.rows[static_cast<size_t>(s.nowRowIndex)].aircraft.icao24.c_str());
}

static void test_ground_row_gets_no_phase(void) {
  const DataSnapshot s = buildDataSnapshot(sky(), params(0, 0), stubPorts(), 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::NONE), static_cast<int>(s.rows[2].phase));
}

static void test_empty_sky(void) {
  const DataSnapshot s = buildDataSnapshot({}, params(2, 3), stubPorts(), 99);
  TEST_ASSERT_EQUAL_size_t(0, s.rows.size());
  TEST_ASSERT_EQUAL_INT(-1, s.nowRowIndex);
  TEST_ASSERT_FALSE(s.cpa.found);
  TEST_ASSERT_EQUAL_UINT32(0, s.fetches);
}

// ---- bounded RAM caches over a long run (spec 05) ------------------------------

// The production wiring (net_task's productionPorts() + the lookup*()
// adapters) with the real RAM caches and a map standing in for NVS: a RAM
// miss is seeded from "NVS", only a key in neither goes to "hexdb.io".
std::map<std::string, AircraftInfo> simNvsAircraft;
std::map<std::string, RouteInfo> simNvsRoutes;
std::map<std::string, AirportInfo> simNvsAirports;
std::map<std::string, int> simFetchesPerKey;

static FetchResult simFetchAircraft(const std::string &k) {
  ++simFetchesPerKey["ac:" + k];
  FetchResult r;
  r.response_ok = true;
  r.body = R"({"RegisteredOwners":"Sim Air","ICAOTypeCode":"A320"})";
  return r;
}

static HexdbFetchResult simFetchRoute(const std::string &k) {
  ++simFetchesPerKey["rt:" + k];
  HexdbFetchResult r;
  r.response_ok = true;
  r.body = R"({"route":"EPWA-LIRF"})";
  return r;
}

static HexdbFetchResult simFetchAirport(const std::string &k) {
  ++simFetchesPerKey["ap:" + k];
  HexdbFetchResult r;
  r.response_ok = true;
  r.body = R"({"country_code":"PL","iata":"WAW"})";
  return r;
}

static LookupPorts simPorts() {
  LookupPorts p;
  p.aircraftKnown = [](const std::string &k) {
    return aircraftLookupIsCached(k) || simNvsAircraft.count(k) > 0;
  };
  p.routeKnown = [](const std::string &k) {
    return routeLookupIsCached(k) || simNvsRoutes.count(k) > 0;
  };
  p.airportKnown = [](const std::string &k) {
    return airportLookupIsCached(k) || simNvsAirports.count(k) > 0;
  };
  p.aircraft = [](const std::string &k) {
    auto nvs = simNvsAircraft.find(k);
    if (!aircraftLookupIsCached(k) && nvs != simNvsAircraft.end()) {
      aircraftLookupSeedCache(k, nvs->second);
    }
    bool hit = false;
    AircraftInfo info = lookupAircraftWithFetcher(k, simFetchAircraft, &hit);
    if (!hit) simNvsAircraft[k] = info;
    return info;
  };
  p.route = [](const std::string &k) {
    auto nvs = simNvsRoutes.find(k);
    if (!routeLookupIsCached(k) && nvs != simNvsRoutes.end()) routeLookupSeedCache(k, nvs->second);
    bool hit = false;
    RouteInfo info = lookupRouteWithFetcher(k, simFetchRoute, &hit);
    if (!hit) simNvsRoutes[k] = info;
    return info;
  };
  p.airport = [](const std::string &k) {
    auto nvs = simNvsAirports.find(k);
    if (!airportLookupIsCached(k) && nvs != simNvsAirports.end()) {
      airportLookupSeedCache(k, nvs->second);
    }
    bool hit = false;
    AirportInfo info = lookupAirportWithFetcher(k, simFetchAirport, &hit);
    if (!hit) simNvsAirports[k] = info;
    return info;
  };
  return p;
}

// Aircraft #first .. #first+count-1, spread a few km around home.
static std::vector<Aircraft> rotatingSky(int first, int count) {
  std::vector<Aircraft> v;
  for (int i = first; i < first + count; ++i) {
    char hex[16], cs[16];  // room for any int: gcc 13 -Wformat-truncation (CI)
    std::snprintf(hex, sizeof(hex), "a%05d", i);
    std::snprintf(cs, sizeof(cs), "SIM%d", i);
    v.push_back(makeAircraft(hex, cs, kHomeLat + 0.01f * static_cast<float>(i % 30), kHomeLon));
  }
  return v;
}

static void test_caches_stay_bounded_over_200_polls(void) {
  simNvsAircraft.clear();
  simNvsRoutes.clear();
  simNvsAirports.clear();
  simFetchesPerKey.clear();
  const LookupPorts ports = simPorts();
  const PipelineParams full = params(100, 10);

  // 30 aircraft in range, 5 of them new on every poll: ~1000 aircraft in all,
  // ten times the RAM cache.
  for (int poll = 0; poll < 200; ++poll) {
    const std::vector<Aircraft> now = rotatingSky(poll * 5, 30);
    const DataSnapshot a = buildDataSnapshot(now, cacheOnly(full), ports, 0);
    TEST_ASSERT_EQUAL_UINT32(0, a.fetches);
    const DataSnapshot b = buildDataSnapshot(now, full, ports, 0);
    for (const AircraftRow &row : b.rows) {
      TEST_ASSERT_FALSE(row.info_pending);
      TEST_ASSERT_FALSE(row.route_pending);
    }
    TEST_ASSERT_TRUE(aircraftLookupCacheSize() <= kAircraftCacheCapacity);
    TEST_ASSERT_TRUE(routeLookupCacheSize() <= kRouteCacheCapacity);
    TEST_ASSERT_TRUE(airportLookupCacheSize() <= kAirportCacheCapacity);
  }

  TEST_ASSERT_EQUAL_size_t(kAircraftCacheCapacity, aircraftLookupCacheSize());

  // The first 30 come back long after falling out of RAM: from "NVS", free.
  TEST_ASSERT_FALSE(aircraftLookupIsCached("a00000"));
  const DataSnapshot back = buildDataSnapshot(rotatingSky(0, 30), full, ports, 0);
  TEST_ASSERT_EQUAL_UINT32(0, back.fetches);
  TEST_ASSERT_FALSE(back.rows.front().info_pending);

  // Every aircraft, route and airport went to hexdb.io exactly once: the ones
  // still in range were RAM hits, nothing was fetched a second time.
  TEST_ASSERT_EQUAL_size_t(199 * 5 + 30, simNvsAircraft.size());
  for (const auto &kv : simFetchesPerKey) {
    if (kv.second != 1) TEST_FAIL_MESSAGE(("fetched more than once: " + kv.first).c_str());
  }
}

}  // namespace

void run_poll_pipeline_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_rows_ordered_airborne_ground_no_position);
  RUN_TEST(test_cache_only_makes_no_requests_and_marks_pending);
  RUN_TEST(test_cache_hits_are_used_in_phase_a);
  RUN_TEST(test_budget_goes_to_the_nearest_first);
  RUN_TEST(test_airports_of_new_routes_in_the_same_pass);
  RUN_TEST(test_phase_b_without_new_keys_equals_phase_a);
  RUN_TEST(test_now_pick_points_at_the_sorted_row);
  RUN_TEST(test_ground_row_gets_no_phase);
  RUN_TEST(test_empty_sky);
  RUN_TEST(test_caches_stay_bounded_over_200_polls);
}
