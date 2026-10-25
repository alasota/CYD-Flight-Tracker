// lookup_cache: the size-limited (LRU) RAM cache. spec/done/05-bounded-lookup-cache.md.
#include <unity.h>

#include <string>

#include "lookup_cache.h"
#include "test_support.h"

namespace {

static void test_put_find_contains(void) {
  LookupCache<int> c(4);
  TEST_ASSERT_NULL(c.find("a"));
  TEST_ASSERT_FALSE(c.contains("a"));
  c.put("a", 1);
  c.put("b", 2);
  TEST_ASSERT_TRUE(c.contains("a"));
  TEST_ASSERT_NOT_NULL(c.find("b"));
  TEST_ASSERT_EQUAL_INT(2, *c.find("b"));
  TEST_ASSERT_EQUAL_size_t(2, c.size());
  TEST_ASSERT_EQUAL_size_t(4, c.capacity());
}

static void test_put_existing_key_overwrites_without_growing(void) {
  LookupCache<std::string> c(4);
  c.put("a", "old");
  c.put("a", "new");
  TEST_ASSERT_EQUAL_size_t(1, c.size());
  ASSERT_STR_EQ("new", *c.find("a"));
}

static void test_overflow_evicts_least_recently_used_not_oldest(void) {
  LookupCache<int> c(3);
  c.put("a", 1);
  c.put("b", 2);
  c.put("c", 3);
  TEST_ASSERT_NOT_NULL(c.find("a"));  // "a" is now the freshest, "b" the stalest
  c.put("d", 4);
  TEST_ASSERT_EQUAL_size_t(3, c.size());
  TEST_ASSERT_TRUE(c.contains("a"));
  TEST_ASSERT_FALSE(c.contains("b"));
  TEST_ASSERT_TRUE(c.contains("c"));
  TEST_ASSERT_TRUE(c.contains("d"));
}

static void test_overwrite_counts_as_use(void) {
  LookupCache<int> c(2);
  c.put("a", 1);
  c.put("b", 2);
  c.put("a", 10);  // "b" is now the stalest
  c.put("c", 3);
  TEST_ASSERT_TRUE(c.contains("a"));
  TEST_ASSERT_FALSE(c.contains("b"));
}

static void test_contains_does_not_refresh(void) {
  LookupCache<int> c(2);
  c.put("a", 1);
  c.put("b", 2);
  TEST_ASSERT_TRUE(c.contains("a"));  // only asked about — still the stalest
  c.put("c", 3);
  TEST_ASSERT_FALSE(c.contains("a"));
  TEST_ASSERT_TRUE(c.contains("b"));
  TEST_ASSERT_TRUE(c.contains("c"));
}

static void test_size_never_exceeds_capacity(void) {
  LookupCache<std::string> c(kAircraftCacheCapacity);
  for (int i = 0; i < 10000; ++i) {
    c.put("k" + std::to_string(i), "v");
    TEST_ASSERT_TRUE(c.size() <= c.capacity());
  }
  TEST_ASSERT_EQUAL_size_t(kAircraftCacheCapacity, c.size());
  TEST_ASSERT_TRUE(c.contains("k9999"));
  TEST_ASSERT_FALSE(c.contains("k0"));
}

static void test_capacity_one(void) {
  LookupCache<int> c(1);
  c.put("a", 1);
  c.put("b", 2);
  TEST_ASSERT_EQUAL_size_t(1, c.size());
  TEST_ASSERT_NULL(c.find("a"));
  TEST_ASSERT_EQUAL_INT(2, *c.find("b"));
}

static void test_capacity_zero_holds_nothing(void) {
  LookupCache<int> c(0);
  c.put("a", 1);
  TEST_ASSERT_EQUAL_size_t(0, c.size());
  TEST_ASSERT_NULL(c.find("a"));
  TEST_ASSERT_FALSE(c.contains("a"));
}

static void test_clear(void) {
  LookupCache<int> c(3);
  c.put("a", 1);
  c.put("b", 2);
  c.clear();
  TEST_ASSERT_EQUAL_size_t(0, c.size());
  TEST_ASSERT_NULL(c.find("a"));
  c.put("c", 3);  // still usable after clear()
  TEST_ASSERT_EQUAL_INT(3, *c.find("c"));
}

static void test_capacities_hold_a_polls_working_set(void) {
  // >= 2x the usual number of aircraft in range (10-40 at ±2.5°), or every
  // poll would go back to NVS (spec 05 §3.2).
  TEST_ASSERT_TRUE(kAircraftCacheCapacity >= 64);
  TEST_ASSERT_TRUE(kRouteCacheCapacity >= 64);
  TEST_ASSERT_TRUE(kAirportCacheCapacity >= 32);
}

}  // namespace

void run_lookup_cache_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_put_find_contains);
  RUN_TEST(test_put_existing_key_overwrites_without_growing);
  RUN_TEST(test_overflow_evicts_least_recently_used_not_oldest);
  RUN_TEST(test_overwrite_counts_as_use);
  RUN_TEST(test_contains_does_not_refresh);
  RUN_TEST(test_size_never_exceeds_capacity);
  RUN_TEST(test_capacity_one);
  RUN_TEST(test_capacity_zero_holds_nothing);
  RUN_TEST(test_clear);
  RUN_TEST(test_capacities_hold_a_polls_working_set);
}
