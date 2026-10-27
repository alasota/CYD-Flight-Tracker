// poll_pipeline: one /states/all answer -> DataSnapshot, betweenFetches hook. CLAUDE.md "Concurrency — two cores", spec/04, spec 09.
#include <unity.h>

#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fixtures.h"
#include "enrichment/domain/lookup_cache.h"
#include "airspace/app/poll_pipeline.h"
#include "test_support.h"

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

// ---- betweenFetches: the settings page between phase-B lookups (spec 09 §3) ----

// Two airborne aircraft, nothing cached: 2 aircraft + 2 routes + 3 airports
// (AAA1 EPWA-LIRF, BBB2 EPWA-EGLL) = 7 hexdb.io requests in phase B.
static std::vector<Aircraft> twoNewAircraft() {
  return {makeAircraft("aaa001", "AAA1", 52.05f, 21.0f),
          makeAircraft("bbb002", "BBB2", 52.10f, 21.0f)};
}

// stubPorts() with a route per callsign; the hook (if any) is set by the test.
static LookupPorts hookPorts() {
  LookupPorts p = stubPorts();
  p.route = [](const std::string &k) {
    calls.push_back("rt:" + k);
    RouteInfo r;
    r.found = true;
    r.origin_icao = "EPWA";
    r.dest_icao = k == "BBB2" ? "EGLL" : "LIRF";
    return r;
  };
  return p;
}

// Records "hook" between the stub's fetch records; returns false on call
// number `stopAt` (1-based, 0 = never).
static int hookCalls = 0;
static std::function<bool()> recordingHook(int stopAt = 0) {
  hookCalls = 0;
  return [stopAt] {
    ++hookCalls;
    calls.push_back("hook");
    return stopAt == 0 || hookCalls != stopAt;
  };
}

// True when `call` ("ac:x", "rt:x", "ap:x") is for a key the stub has cached.
static bool isCachedCall(const std::string &call) {
  const std::string key = call.substr(3);
  if (call.rfind("ac:", 0) == 0) return knownAircraft.count(key) > 0;
  if (call.rfind("rt:", 0) == 0) return knownRoutes.count(key) > 0;
  if (call.rfind("ap:", 0) == 0) return knownAirports.count(key) > 0;
  return false;
}

static const AircraftRow &rowOf(const DataSnapshot &s, const char *icao24) {
  for (const AircraftRow &r : s.rows) {
    if (r.aircraft.icao24 == icao24) return r;
  }
  TEST_FAIL_MESSAGE((std::string("no row for ") + icao24).c_str());
  return s.rows.front();
}

// Hak jest wołany po każdym rzeczywistym zapytaniu hexdb.io
static void test_between_fetches_called_once_per_fetch(void) {
  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook();
  const DataSnapshot s = buildDataSnapshot(twoNewAircraft(), params(4, 3), ports, 0);

  TEST_ASSERT_EQUAL_UINT32(7, s.fetches);
  TEST_ASSERT_EQUAL_INT(7, hookCalls);
  // fetch, hook, fetch, hook, ... — each call right after its request.
  TEST_ASSERT_EQUAL_size_t(14, calls.size());
  for (size_t i = 0; i < calls.size(); ++i) {
    if (i % 2 == 1) {
      ASSERT_STR_EQ("hook", calls[i]);
    } else if (calls[i] == "hook") {
      TEST_FAIL_MESSAGE(("hook before its request, at " + std::to_string(i)).c_str());
    }
  }
}

// Trafienia w cache nie wołają haka
static void test_between_fetches_not_called_for_cache_hits(void) {
  for (const char *k : {"aaa001", "bbb002"}) knownAircraft.insert(k);
  for (const char *k : {"AAA1", "BBB2"}) knownRoutes.insert(k);
  for (const char *k : {"EPWA", "LIRF", "EGLL"}) knownAirports.insert(k);
  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook();

  const DataSnapshot a = buildDataSnapshot(twoNewAircraft(), cacheOnly(params(4, 3)), ports, 0);
  const DataSnapshot b = buildDataSnapshot(twoNewAircraft(), params(4, 3), ports, 0);
  TEST_ASSERT_EQUAL_UINT32(0, a.fetches);
  TEST_ASSERT_EQUAL_UINT32(0, b.fetches);
  TEST_ASSERT_EQUAL_INT(0, hookCalls);
  TEST_ASSERT_FALSE(rowOf(b, "bbb002").dest_airport_pending);  // all of it resolved from cache
}

// Bez haka faza B działa jak dotąd
static void test_between_fetches_optional(void) {
  const LookupPorts ports = hookPorts();  // no betweenFetches
  const DataSnapshot s = buildDataSnapshot(twoNewAircraft(), params(4, 3), ports, 0);
  TEST_ASSERT_EQUAL_UINT32(7, s.fetches);
  const std::vector<std::string> expected = {"ac:aaa001", "ac:bbb002", "rt:AAA1", "rt:BBB2",
                                             "ap:EPWA",   "ap:LIRF",   "ap:EGLL"};
  TEST_ASSERT_EQUAL_size_t(expected.size(), calls.size());
  for (size_t i = 0; i < expected.size(); ++i) ASSERT_STR_EQ(expected[i], calls[i]);
  const AircraftRow &a = rowOf(s, "aaa001");
  const AircraftRow &b = rowOf(s, "bbb002");
  ASSERT_STR_EQ("Airline aaa001", a.info.airline);
  ASSERT_STR_EQ("WAW", a.origin_airport.iata_code);
  ASSERT_STR_EQ("FCO", a.dest_airport.iata_code);
  TEST_ASSERT_FALSE(b.dest_airport_pending);
  TEST_ASSERT_FALSE(b.route_pending);
}

// Hak każe przerwać — faza B kończy się bez dalszych zapytań, z częściowym snapshotem
static void test_between_fetches_stop_ends_phase_b_with_partial_snapshot(void) {
  // Two new aircraft plus one whose airframe, route and origin airport are cached.
  std::vector<Aircraft> sky3 = twoNewAircraft();
  sky3.push_back(makeAircraft("ccc003", "CCC3", 52.20f, 21.0f));
  knownAircraft.insert("ccc003");
  knownRoutes.insert("CCC3");
  knownAirports.insert("EPWA");

  const DataSnapshot ref = buildDataSnapshot(sky3, cacheOnly(params(4, 3)), hookPorts(), 0);
  calls.clear();

  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook(/*stopAt=*/2);  // "an OTA update started"
  const DataSnapshot s = buildDataSnapshot(sky3, params(4, 3), ports, 0);

  // No hexdb.io request after the hook said stop (cached keys may still resolve).
  size_t lastHook = 0;
  for (size_t i = 0; i < calls.size(); ++i) {
    if (calls[i] == "hook") lastHook = i;
  }
  TEST_ASSERT_EQUAL_INT(2, hookCalls);
  for (size_t i = lastHook + 1; i < calls.size(); ++i) {
    if (!isCachedCall(calls[i])) TEST_FAIL_MESSAGE(("request after stop: " + calls[i]).c_str());
  }
  TEST_ASSERT_EQUAL_UINT32(2, s.fetches);

  // Fetched data is in, the rest is pending ("..."), not unknown ("--").
  const AircraftRow &a = rowOf(s, "aaa001");
  const AircraftRow &b = rowOf(s, "bbb002");
  const AircraftRow &c = rowOf(s, "ccc003");
  ASSERT_STR_EQ("Airline aaa001", a.info.airline);
  ASSERT_STR_EQ("Airline bbb002", b.info.airline);
  TEST_ASSERT_FALSE(a.info_pending);
  TEST_ASSERT_TRUE(a.route_pending);
  TEST_ASSERT_TRUE(b.route_pending);
  TEST_ASSERT_FALSE(a.route.found);
  // Cached data still reaches its row.
  ASSERT_STR_EQ("Airline ccc003", c.info.airline);
  TEST_ASSERT_TRUE(c.route.found);
  ASSERT_STR_EQ("WAW", c.origin_airport.iata_code);
  TEST_ASSERT_TRUE(c.dest_airport_pending);  // LIRF: not cached, not fetched

  // Positions, distances, phases and the NOW pick as usual.
  TEST_ASSERT_EQUAL_size_t(ref.rows.size(), s.rows.size());
  for (size_t i = 0; i < ref.rows.size(); ++i) {
    ASSERT_STR_EQ(ref.rows[i].aircraft.icao24, s.rows[i].aircraft.icao24);
    TEST_ASSERT_TRUE(s.rows[i].has_distance);
    TEST_ASSERT_EQUAL_FLOAT(ref.rows[i].distance_km, s.rows[i].distance_km);
    ASSERT_ENUM_EQ(ref.rows[i].phase, s.rows[i].phase);
  }
  TEST_ASSERT_EQUAL_INT(ref.nowRowIndex, s.nowRowIndex);
  TEST_ASSERT_TRUE(s.cpa.found);
  TEST_ASSERT_EQUAL_FLOAT(ref.cpa.t_cpa_seconds, s.cpa.t_cpa_seconds);
}

// Hak wywołany w trakcie haka nic nie robi
static void test_between_fetches_guard_ignores_nested_call(void) {
  bool busy = false;
  int outerServed = 0;
  int nestedServed = 0;
  bool nestedResult = false;
  const std::function<bool()> noUpdate = [] { return false; };
  const std::function<void()> nestedServe = [&] { ++nestedServed; };
  const std::function<void()> outerServe = [&] {
    ++outerServed;
    nestedResult = serveBetweenFetches(busy, nestedServe, noUpdate);  // a handler re-entering
  };

  const bool outerResult = serveBetweenFetches(busy, outerServe, noUpdate);
  TEST_ASSERT_EQUAL_INT(1, outerServed);
  TEST_ASSERT_EQUAL_INT(0, nestedServed);  // the nested call doesn't serve the page
  TEST_ASSERT_TRUE(nestedResult);          // ... and doesn't stop phase B
  TEST_ASSERT_TRUE(outerResult);
  TEST_ASSERT_FALSE(busy);
}

// Aktualizacja rozpoczęta w trakcie haka kończy fazę B
// (Examples: wgrano firmware i czeka restart / trwa zapis obrazu OTA)
static void test_between_fetches_stops_when_update_started(void) {
  for (int example = 0; example < 2; ++example) {
    bool restartPending = false;
    bool otaInProgress = false;
    bool busy = false;
    int served = 0;
    const bool result = serveBetweenFetches(
        busy,
        [&] {
          ++served;
          if (example == 0) restartPending = true;  // firmware uploaded, restart pending
          else otaInProgress = true;                // OTA image being written
        },
        [&] { return restartPending || otaInProgress; });
    TEST_ASSERT_EQUAL_INT(1, served);
    TEST_ASSERT_FALSE_MESSAGE(result, example == 0 ? "restart pending" : "OTA in progress");
    TEST_ASSERT_FALSE(busy);
  }
}

// ---- betweenFetches edge cases (plan, no scenario) ---------------------------

// "Stop" on the very first request: exactly one fetch.
static void test_between_fetches_stop_on_first_fetch(void) {
  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook(/*stopAt=*/1);
  const DataSnapshot s = buildDataSnapshot(twoNewAircraft(), params(4, 3), ports, 0);
  TEST_ASSERT_EQUAL_UINT32(1, s.fetches);
  TEST_ASSERT_EQUAL_size_t(2, calls.size());
  ASSERT_STR_EQ("hook", calls[1]);
  TEST_ASSERT_TRUE(rowOf(s, "bbb002").info_pending);
}

// "Stop" during the airport stage keeps every aircraft / route already fetched.
static void test_between_fetches_stop_in_airport_stage_keeps_fetched_info(void) {
  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook(/*stopAt=*/5);  // after the first airport
  const DataSnapshot s = buildDataSnapshot(twoNewAircraft(), params(4, 3), ports, 0);
  TEST_ASSERT_EQUAL_UINT32(5, s.fetches);
  TEST_ASSERT_EQUAL_INT(5, hookCalls);
  for (const char *hex : {"aaa001", "bbb002"}) {
    const AircraftRow &r = rowOf(s, hex);
    TEST_ASSERT_FALSE(r.info_pending);
    TEST_ASSERT_FALSE(r.route_pending);
    TEST_ASSERT_TRUE(r.route.found);
    ASSERT_STR_EQ("WAW", r.origin_airport.iata_code);  // EPWA, fetched before the stop
    TEST_ASSERT_TRUE(r.dest_airport_pending);
  }
}

// Phase A (cacheOnly) never calls the hook, even with uncached keys around.
static void test_between_fetches_not_called_in_phase_a(void) {
  LookupPorts ports = hookPorts();
  ports.betweenFetches = recordingHook();
  const DataSnapshot s = buildDataSnapshot(twoNewAircraft(), cacheOnly(params(4, 3)), ports, 0);
  TEST_ASSERT_EQUAL_UINT32(0, s.fetches);
  TEST_ASSERT_EQUAL_INT(0, hookCalls);
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
  RUN_TEST(test_between_fetches_called_once_per_fetch);
  RUN_TEST(test_between_fetches_not_called_for_cache_hits);
  RUN_TEST(test_between_fetches_optional);
  RUN_TEST(test_between_fetches_stop_ends_phase_b_with_partial_snapshot);
  RUN_TEST(test_between_fetches_guard_ignores_nested_call);
  RUN_TEST(test_between_fetches_stops_when_update_started);
  RUN_TEST(test_between_fetches_stop_on_first_fetch);
  RUN_TEST(test_between_fetches_stop_in_airport_stage_keeps_fetched_info);
  RUN_TEST(test_between_fetches_not_called_in_phase_a);
}
