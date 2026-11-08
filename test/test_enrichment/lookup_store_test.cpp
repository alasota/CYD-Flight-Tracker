// lookup_store: NVS record encode / decode of the lookup caches. CLAUDE.md "Code conventions" (lookup_store).
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "enrichment/core/aircraft_lookup.h"
#include "enrichment/core/lookup_store.h"
#include "enrichment/core/route_lookup.h"
#include "test_support.h"

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

// ---- a full NVS partition (docs/data-sources.md "Caching and the per-poll budget") ---

// NVS as the store sees it: slots, not bytes. A string takes one header slot
// plus one per started 32 bytes (with its NUL); a new value is written before
// the old one is released.
class FakeNvs : public LookupNvs {
 public:
  explicit FakeNvs(size_t capacity) : capacity_(capacity) {}

  bool put(const char *ns, const std::string &key, const std::string &value) override {
    if (slotsFor(value) > freeEntries()) return false;
    data_[ns][key] = value;
    return true;
  }
  void clear(const char *ns) override {
    data_.erase(ns);
    cleared_.push_back(ns);
  }
  size_t freeEntries() override { return capacity_ - used(); }

  std::string get(const char *ns, const std::string &key) const {
    const auto n = data_.find(ns);
    if (n == data_.end()) return "";
    const auto k = n->second.find(key);
    return k == n->second.end() ? "" : k->second;
  }
  const std::vector<std::string> &cleared() const { return cleared_; }

 private:
  static size_t slotsFor(const std::string &value) { return 1 + (value.size() + 1 + 31) / 32; }
  size_t used() const {
    size_t total = 0;
    for (const auto &n : data_) {
      for (const auto &k : n.second) total += slotsFor(k.second);
    }
    return total;
  }

  size_t capacity_;
  std::map<std::string, std::map<std::string, std::string>> data_;
  std::vector<std::string> cleared_;
};

constexpr char kRecord[] = "a record of a few dozen characters, two data slots";

// Fills `ns` with records until not one more fits.
void fillUp(FakeNvs &nvs, const char *ns) {
  for (int i = 0; nvs.freeEntries() > 0; ++i) {
    if (!nvs.put(ns, "key" + std::to_string(i), "x")) break;
  }
}

// Regression: an airport answer is lost when aircraft records fill NVS and the airport namespace has nothing to wipe
static void test_full_nvs_stores_a_record_even_when_its_own_namespace_is_empty(void) {
  FakeNvs nvs(100);
  fillUp(nvs, "lk_ac");
  TEST_ASSERT_EQUAL_UINT32(0, nvs.freeEntries());

  storeSaveRecord(nvs, "lk_ap", "EKBI", kRecord);

  ASSERT_STR_EQ(kRecord, nvs.get("lk_ap", "EKBI"));
}

// Regression: lookup records fill the partition to the last slot, so a settings or WiFi save finds no room
static void test_lookup_records_always_leave_room_for_settings(void) {
  FakeNvs nvs(400);
  for (int i = 0; i < 400; ++i) {
    storeSaveRecord(nvs, "lk_ac", "key" + std::to_string(i), kRecord);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kLookupStoreReserveEntries, nvs.freeEntries());
  }
}

// Whatever gets evicted to make room, the settings namespace never does.
static void test_full_nvs_never_wipes_the_settings(void) {
  FakeNvs nvs(100);
  nvs.put("cydsky", "os_secret", "secret");
  fillUp(nvs, "lk_ac");

  storeSaveRecord(nvs, "lk_rt", "LOT282", kRecord);

  ASSERT_NOT_CONTAINS(nvs.cleared(), "cydsky");
  ASSERT_STR_EQ("secret", nvs.get("cydsky", "os_secret"));
}

// With only settings in the partition nothing can be evicted: the caller is told.
static void test_save_reports_failure_when_nothing_can_be_evicted(void) {
  FakeNvs nvs(10);
  fillUp(nvs, "cydsky");

  TEST_ASSERT_FALSE(storeSaveRecord(nvs, "lk_ap", "EKBI", kRecord));
}

// Exactly the reserve left after the record (kRecord takes 3 slots): no wipe.
static void test_save_wipes_nothing_when_the_reserve_just_holds(void) {
  FakeNvs nvs(kLookupStoreReserveEntries + 3);

  TEST_ASSERT_TRUE(storeSaveRecord(nvs, "lk_ap", "EKBI", kRecord));

  TEST_ASSERT_EQUAL_UINT32(0, nvs.cleared().size());
}

// Fills `count` two-slot records into `ns`.
void addRecords(FakeNvs &nvs, const char *ns, int count) {
  for (int i = 0; i < count; ++i) nvs.put(ns, "key" + std::to_string(i), "x");
}

// The other kinds' records survive when wiping the record's own kind is enough.
static void test_save_stops_wiping_once_the_reserve_is_back(void) {
  FakeNvs nvs(100);
  addRecords(nvs, "lk_ap", 20);
  addRecords(nvs, "lk_ac", 15);

  TEST_ASSERT_TRUE(storeSaveRecord(nvs, "lk_ap", "EKBI", kRecord));

  TEST_ASSERT_EQUAL_UINT32(1, nvs.cleared().size());
  ASSERT_STR_EQ("lk_ap", nvs.cleared()[0]);
  ASSERT_STR_EQ("x", nvs.get("lk_ac", "key0"));
}

static void test_save_wipes_its_own_kind_before_the_others(void) {
  FakeNvs nvs(100);
  addRecords(nvs, "lk_ap", 5);
  addRecords(nvs, "lk_ac", 30);

  TEST_ASSERT_TRUE(storeSaveRecord(nvs, "lk_ap", "EKBI", kRecord));

  TEST_ASSERT_EQUAL_UINT32(2, nvs.cleared().size());
  ASSERT_STR_EQ("lk_ap", nvs.cleared()[0]);
  ASSERT_STR_EQ("lk_ac", nvs.cleared()[1]);
}

// NVS can refuse a write despite free slots (fragmented pages).
class RefusingNvs : public FakeNvs {
 public:
  RefusingNvs(size_t capacity, int refusals) : FakeNvs(capacity), refusals_(refusals) {}
  bool put(const char *ns, const std::string &key, const std::string &value) override {
    if (refusals_ > 0) {
      --refusals_;
      return false;
    }
    return FakeNvs::put(ns, key, value);
  }

 private:
  int refusals_;
};

static void test_save_wipes_the_next_kind_and_retries_after_a_refused_write(void) {
  RefusingNvs nvs(200, 1);

  TEST_ASSERT_TRUE(storeSaveRecord(nvs, "lk_rt", "LOT282", kRecord));

  TEST_ASSERT_EQUAL_UINT32(1, nvs.cleared().size());
  ASSERT_STR_EQ("lk_rt", nvs.cleared()[0]);
  ASSERT_STR_EQ(kRecord, nvs.get("lk_rt", "LOT282"));
}

static void test_save_gives_up_after_wiping_every_lookup_kind(void) {
  RefusingNvs nvs(200, 100);

  TEST_ASSERT_FALSE(storeSaveRecord(nvs, "lk_rt", "LOT282", kRecord));

  TEST_ASSERT_EQUAL_UINT32(3, nvs.cleared().size());
  ASSERT_NOT_CONTAINS(nvs.cleared(), "cydsky");
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
  RUN_TEST(test_full_nvs_stores_a_record_even_when_its_own_namespace_is_empty);
  RUN_TEST(test_lookup_records_always_leave_room_for_settings);
  RUN_TEST(test_full_nvs_never_wipes_the_settings);
  RUN_TEST(test_save_reports_failure_when_nothing_can_be_evicted);
  RUN_TEST(test_save_wipes_nothing_when_the_reserve_just_holds);
  RUN_TEST(test_save_stops_wiping_once_the_reserve_is_back);
  RUN_TEST(test_save_wipes_its_own_kind_before_the_others);
  RUN_TEST(test_save_wipes_the_next_kind_and_retries_after_a_refused_write);
  RUN_TEST(test_save_gives_up_after_wiping_every_lookup_kind);

}
