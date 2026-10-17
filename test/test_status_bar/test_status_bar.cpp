#include <unity.h>

#include <cstring>

#include "screen_nav.h"  // kScreen* indices
#include "status_bar.h"
#include "time_sync.h"

void setUp(void) {}
void tearDown(void) {}

// 1500000000 -> 2017-07-14 02:40:00 UTC (see test_time_sync); its date
// is "14/07/2017" and its "HH:MM" is "02:40".
static constexpr std::time_t kEpoch = 1500000000;

// ---- statusBarScreenName ------------------------------------------------

static void test_screen_name_per_index_english(void) {
  TEST_ASSERT_EQUAL_STRING("FLIGHTS", statusBarScreenName(kScreenFlights, Language::EN));
  TEST_ASSERT_EQUAL_STRING("NOW", statusBarScreenName(kScreenFlight, Language::EN));
  TEST_ASSERT_EQUAL_STRING("RADAR", statusBarScreenName(kScreenRadar, Language::EN));
}

static void test_screen_name_per_index_polish(void) {
  TEST_ASSERT_EQUAL_STRING("LOTY", statusBarScreenName(kScreenFlights, Language::PL));
  TEST_ASSERT_EQUAL_STRING("TERAZ", statusBarScreenName(kScreenFlight, Language::PL));
  TEST_ASSERT_EQUAL_STRING("RADAR", statusBarScreenName(kScreenRadar, Language::PL));
}

static void test_screen_name_out_of_range(void) {
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(-1, Language::PL));
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(3, Language::EN));
  TEST_ASSERT_EQUAL_STRING("?", statusBarScreenName(99, Language::PL));
}

static void test_screen_names_fit_the_header_block(void) {
  // CLAUDE.md: the orange block is sized for 7 characters ("FLIGHTS"); no
  // name in any language may be longer.
  for (Language lang : {Language::PL, Language::EN}) {
    for (int i = 0; i < kScreenCount; ++i) {
      TEST_ASSERT_TRUE(std::strlen(statusBarScreenName(i, lang)) <= 7);
    }
  }
}

// ---- statusBarDate ----------------------------------------------------

static void test_date_placeholder_until_synced(void) {
  TEST_ASSERT_EQUAL_STRING("--/--/----", statusBarDate(kEpoch, false).c_str());
}

static void test_date_uses_time_sync_when_synced(void) {
  TEST_ASSERT_EQUAL_STRING(formatDate(kEpoch).c_str(), statusBarDate(kEpoch, true).c_str());
  TEST_ASSERT_EQUAL_STRING("14/07/2017", statusBarDate(kEpoch, true).c_str());
}

static void test_date_placeholder_same_length_as_date(void) {
  // Same width in a monospace-ish font, so the header text doesn't jump
  // when the first NTP sync lands.
  TEST_ASSERT_EQUAL_size_t(statusBarDate(kEpoch, true).size(), statusBarDate(kEpoch, false).size());
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
  TEST_ASSERT_EQUAL_STRING("", statusBarHealthTag(OpenSkyHealth::Ok, Language::PL));
  TEST_ASSERT_EQUAL_STRING("", statusBarHealthTag(OpenSkyHealth::Ok, Language::EN));
}

static void test_health_tag_per_failure_mode(void) {
  TEST_ASSERT_EQUAL_STRING("RATE", statusBarHealthTag(OpenSkyHealth::RateLimited, Language::EN));
  TEST_ASSERT_EQUAL_STRING("AUTH", statusBarHealthTag(OpenSkyHealth::AuthError, Language::EN));
  TEST_ASSERT_EQUAL_STRING("NET", statusBarHealthTag(OpenSkyHealth::NetworkError, Language::EN));
  TEST_ASSERT_EQUAL_STRING("LIMIT", statusBarHealthTag(OpenSkyHealth::RateLimited, Language::PL));
  TEST_ASSERT_EQUAL_STRING("SIEC", statusBarHealthTag(OpenSkyHealth::NetworkError, Language::PL));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_screen_name_per_index_english);
  RUN_TEST(test_screen_name_per_index_polish);
  RUN_TEST(test_screen_name_out_of_range);
  RUN_TEST(test_screen_names_fit_the_header_block);

  RUN_TEST(test_date_placeholder_until_synced);
  RUN_TEST(test_date_uses_time_sync_when_synced);
  RUN_TEST(test_date_placeholder_same_length_as_date);

  RUN_TEST(test_clock_placeholder_until_synced);
  RUN_TEST(test_clock_uses_time_sync_when_synced);

  RUN_TEST(test_health_tag_empty_when_ok);
  RUN_TEST(test_health_tag_per_failure_mode);

  return UNITY_END();
}
