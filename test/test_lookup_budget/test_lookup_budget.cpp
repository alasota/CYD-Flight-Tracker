#include <unity.h>

#include <set>

#include "lookup_budget.h"

void setUp(void) {}
void tearDown(void) {}

static std::function<bool(const std::string &)> cachedSet(std::set<std::string> keys) {
  return [keys](const std::string &k) { return keys.count(k) > 0; };
}

static std::function<bool(const std::string &)> nothingCached() {
  return [](const std::string &) { return false; };
}

static bool contains(const std::vector<std::string> &v, const std::string &s) {
  for (const auto &e : v) {
    if (e == s) return true;
  }
  return false;
}

// ---- budget spending ---------------------------------------------------

static void test_new_fetches_are_capped_at_budget(void) {
  std::vector<LookupKeys> pri = {
      {"a1", "CS1"}, {"a2", "CS2"}, {"a3", "CS3"}, {"a4", "CS4"}};

  LookupPlan plan = planLookups(pri, nothingCached(), nothingCached(), 2);

  // 2 new fetches total, shared across both kinds, spent nearest-first.
  size_t total = plan.icao24.size() + plan.callsigns.size();
  TEST_ASSERT_EQUAL_size_t(2, total);
  TEST_ASSERT_TRUE(contains(plan.icao24, "a1"));
  TEST_ASSERT_TRUE(contains(plan.callsigns, "CS1"));
  TEST_ASSERT_FALSE(contains(plan.icao24, "a2"));
}

static void test_cache_hits_are_always_included_and_free(void) {
  std::vector<LookupKeys> pri = {
      {"a1", "CS1"}, {"a2", "CS2"}, {"a3", "CS3"}};

  // a1/CS1 and a3/CS3 cached; only a2/CS2 are new.
  LookupPlan plan = planLookups(pri, cachedSet({"a1", "a3"}), cachedSet({"CS1", "CS3"}), 2);

  TEST_ASSERT_TRUE(contains(plan.icao24, "a1"));
  TEST_ASSERT_TRUE(contains(plan.icao24, "a3"));
  TEST_ASSERT_TRUE(contains(plan.icao24, "a2"));  // new, within budget
  TEST_ASSERT_TRUE(contains(plan.callsigns, "CS1"));
  TEST_ASSERT_TRUE(contains(plan.callsigns, "CS3"));
  TEST_ASSERT_TRUE(contains(plan.callsigns, "CS2"));
}

static void test_cache_hits_do_not_consume_budget(void) {
  std::vector<LookupKeys> pri = {
      {"cached1", "CACHED1"}, {"cached2", "CACHED2"}, {"new1", "NEW1"}};

  LookupPlan plan = planLookups(pri, cachedSet({"cached1", "cached2"}),
                                cachedSet({"CACHED1", "CACHED2"}), 2);

  // Both new keys still fit — the two cache hits didn't eat the budget.
  TEST_ASSERT_TRUE(contains(plan.icao24, "new1"));
  TEST_ASSERT_TRUE(contains(plan.callsigns, "NEW1"));
}

static void test_zero_budget_returns_only_cache_hits(void) {
  std::vector<LookupKeys> pri = {{"a1", "CS1"}, {"a2", "CS2"}};

  LookupPlan plan = planLookups(pri, cachedSet({"a1"}), cachedSet({}), 0);

  TEST_ASSERT_EQUAL_size_t(1, plan.icao24.size());
  TEST_ASSERT_TRUE(contains(plan.icao24, "a1"));
  TEST_ASSERT_EQUAL_size_t(0, plan.callsigns.size());
}

static void test_empty_callsign_is_skipped_and_not_charged(void) {
  std::vector<LookupKeys> pri = {{"a1", ""}, {"a2", "CS2"}};

  LookupPlan plan = planLookups(pri, nothingCached(), nothingCached(), 2);

  TEST_ASSERT_FALSE(contains(plan.callsigns, ""));
  // a1 (icao), a2 (icao), CS2 (route) would be 3 — but budget 2, spent
  // nearest-first: a1, a2. CS2 misses out because the empty callsign
  // didn't waste a slot but a1+a2 filled it.
  TEST_ASSERT_TRUE(contains(plan.icao24, "a1"));
  TEST_ASSERT_TRUE(contains(plan.icao24, "a2"));
  TEST_ASSERT_FALSE(contains(plan.callsigns, "CS2"));
}

static void test_duplicate_keys_are_deduped(void) {
  std::vector<LookupKeys> pri = {{"a1", "SHARED"}, {"a1", "SHARED"}, {"a2", "SHARED"}};

  LookupPlan plan = planLookups(pri, nothingCached(), nothingCached(), 5);

  int a1count = 0;
  for (const auto &k : plan.icao24) {
    if (k == "a1") ++a1count;
  }
  TEST_ASSERT_EQUAL_INT(1, a1count);

  int sharedCount = 0;
  for (const auto &k : plan.callsigns) {
    if (k == "SHARED") ++sharedCount;
  }
  TEST_ASSERT_EQUAL_INT(1, sharedCount);
}

static void test_empty_priority_list_is_empty_plan(void) {
  LookupPlan plan = planLookups({}, nothingCached(), nothingCached(), 5);
  TEST_ASSERT_EQUAL_size_t(0, plan.icao24.size());
  TEST_ASSERT_EQUAL_size_t(0, plan.callsigns.size());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_new_fetches_are_capped_at_budget);
  RUN_TEST(test_cache_hits_are_always_included_and_free);
  RUN_TEST(test_cache_hits_do_not_consume_budget);
  RUN_TEST(test_zero_budget_returns_only_cache_hits);
  RUN_TEST(test_empty_callsign_is_skipped_and_not_charged);
  RUN_TEST(test_duplicate_keys_are_deduped);
  RUN_TEST(test_empty_priority_list_is_empty_plan);
  return UNITY_END();
}
