#include <unity.h>

#include "screen_nav.h"  // kScreen* indices
#include "status_bar.h"
#include "time_sync.h"

void setUp(void) {}
void tearDown(void) {}

// 1500000000 -> 2017-07-14 02:40:00 UTC (see test_time_sync); its stardate
// is "-305466.13" and its "HH:MM" is "02:40".
static constexpr std::time_t kEpoch = 1500000000;

// ---- statusBarScreenName ------------------------------------------------

static void test_screen_name_per_index(void) {
  TEST_ASSERT_EQUAL_STRING("FLIGHTS", statusBarScreenName(kScreenFlights).c_str());
  TEST_ASSERT_EQUAL_STRING("FLIGHT", statusBarScreenName(kScreenFlight).c_str());
  TEST_ASSERT_EQUAL_STRING("RADAR", statusBarScreenName(kScreenRadar).c_str());
}

static void test_screen_name_out_of_range(void) {
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(-1).c_str());
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(3).c_str());
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(99).c_str());
}

static void test_flights_is_the_longest_name(void) {
  // CLAUDE.md: the orange block must fit the longest name ("FLIGHTS").
  size_t longest = statusBarScreenName(kScreenFlights).size();
  TEST_ASSERT_TRUE(statusBarScreenName(kScreenFlight).size() <= longest);
  TEST_ASSERT_TRUE(statusBarScreenName(kScreenRadar).size() <= longest);
  TEST_ASSERT_EQUAL_size_t(7, longest);
}

// ---- statusBarStardate ------------------------------------------------

static void test_stardate_placeholder_until_synced(void) {
  TEST_ASSERT_EQUAL_STRING("STARDATE: ----.--", statusBarStardate(kEpoch, false).c_str());
}

static void test_stardate_uses_time_sync_when_synced(void) {
  std::string expected = "STARDATE: " + computeStardate(kEpoch);
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), statusBarStardate(kEpoch, true).c_str());
  TEST_ASSERT_EQUAL_STRING("STARDATE: -305466.13", statusBarStardate(kEpoch, true).c_str());
}

// ---- statusBarClock -------------------------------------------------

static void test_clock_placeholder_until_synced(void) {
  TEST_ASSERT_EQUAL_STRING("--:--", statusBarClock(kEpoch, false).c_str());
}

static void test_clock_uses_time_sync_when_synced(void) {
  TEST_ASSERT_EQUAL_STRING(formatTime(kEpoch).c_str(), statusBarClock(kEpoch, true).c_str());
  TEST_ASSERT_EQUAL_STRING("02:40", statusBarClock(kEpoch, true).c_str());
}

// ---- statusBarHealthTag ---------------------------------------------

static void test_health_tag_empty_when_ok(void) {
  TEST_ASSERT_EQUAL_STRING("", statusBarHealthTag(OpenSkyHealth::Ok));
}

static void test_health_tag_per_failure_mode(void) {
  TEST_ASSERT_EQUAL_STRING("RATE", statusBarHealthTag(OpenSkyHealth::RateLimited));
  TEST_ASSERT_EQUAL_STRING("AUTH", statusBarHealthTag(OpenSkyHealth::AuthError));
  TEST_ASSERT_EQUAL_STRING("NET", statusBarHealthTag(OpenSkyHealth::NetworkError));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_screen_name_per_index);
  RUN_TEST(test_screen_name_out_of_range);
  RUN_TEST(test_flights_is_the_longest_name);

  RUN_TEST(test_stardate_placeholder_until_synced);
  RUN_TEST(test_stardate_uses_time_sync_when_synced);

  RUN_TEST(test_clock_placeholder_until_synced);
  RUN_TEST(test_clock_uses_time_sync_when_synced);

  RUN_TEST(test_health_tag_empty_when_ok);
  RUN_TEST(test_health_tag_per_failure_mode);

  return UNITY_END();
}
