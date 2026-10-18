#include <unity.h>

#include <cstring>

#include "screen_nav.h"  // kScreen* indices
#include "status_bar.h"
#include "lcars_theme.h"  // palette constants
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
  // Bottom-nav buttons (~101px of Font0) — keep names short; no
  // name in any language may be longer than 7 chars.
  for (Language lang : {Language::PL, Language::EN}) {
    for (int i = 0; i < kScreenCount; ++i) {
      TEST_ASSERT_TRUE(utf8Length(statusBarScreenName(i, lang)) <= 7);
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

// ---- statusBarBlockLabel (aircraft count in the orange block) ---------

static void test_block_label_placeholder_before_first_data(void) {
  TEST_ASSERT_EQUAL_STRING("--", statusBarBlockLabel(0, false, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("--", statusBarBlockLabel(5, false, Language::EN).c_str());
}

static void test_block_label_polish_plurals(void) {
  TEST_ASSERT_EQUAL_STRING("0 LOTÓW", statusBarBlockLabel(0, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("1 LOT", statusBarBlockLabel(1, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("3 LOTY", statusBarBlockLabel(3, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("7 LOTÓW", statusBarBlockLabel(7, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("12 LOTÓW", statusBarBlockLabel(12, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("22 LOTY", statusBarBlockLabel(22, true, Language::PL).c_str());
}

static void test_block_label_english(void) {
  TEST_ASSERT_EQUAL_STRING("0 FLIGHTS", statusBarBlockLabel(0, true, Language::EN).c_str());
  TEST_ASSERT_EQUAL_STRING("1 FLIGHT", statusBarBlockLabel(1, true, Language::EN).c_str());
  TEST_ASSERT_EQUAL_STRING("7 FLIGHTS", statusBarBlockLabel(7, true, Language::EN).c_str());
}

static void test_block_label_caps_large_counts(void) {
  // "99+" keeps the label inside the block however big the scan radius.
  TEST_ASSERT_EQUAL_STRING("99 LOTÓW", statusBarBlockLabel(99, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("99+ LOTÓW", statusBarBlockLabel(100, true, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("99+ FLIGHTS", statusBarBlockLabel(250, true, Language::EN).c_str());
}

static void test_block_label_never_longer_than_11_chars(void) {
  // kNameBlockWidth is sized for 11 chars of Font0 ("99+ FLIGHTS").
  for (Language lang : {Language::PL, Language::EN}) {
    for (int n = 0; n <= 1000; ++n) {
      TEST_ASSERT_TRUE(utf8Length(statusBarBlockLabel(n, true, lang)) <= 11);
    }
  }
}

// ---- statusBarHealthColor (the status dot) --------------------------------

static void test_health_dot_colour(void) {
  TEST_ASSERT_EQUAL_HEX16(LCARS_GREEN, statusBarHealthColor(OpenSkyHealth::Ok));
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, statusBarHealthColor(OpenSkyHealth::RateLimited));
  TEST_ASSERT_EQUAL_HEX16(THEME_ALERT, statusBarHealthColor(OpenSkyHealth::AuthError));  // red
  TEST_ASSERT_EQUAL_HEX16(THEME_ALERT, statusBarHealthColor(OpenSkyHealth::NetworkError));
}

static void test_header_uses_no_button_colour(void) {
  // Variant A: nothing in the header may look like the active nav button.
  for (OpenSkyHealth h : {OpenSkyHealth::Ok, OpenSkyHealth::RateLimited,
                          OpenSkyHealth::AuthError, OpenSkyHealth::NetworkError}) {
    TEST_ASSERT_NOT_EQUAL(THEME_CHROME_ACCENT, statusBarHealthColor(h));
  }
}

// ---- statusBarZones: name | count | date+time never overlap -----------------

static void test_zones_do_not_overlap_for_longest_texts(void) {
  // Longest possible centre label in either language, and the widest date.
  for (Language lang : {Language::PL, Language::EN}) {
    const std::string count = statusBarBlockLabel(250, true, lang);  // "99+ ..."
    HeaderZones z = statusBarZones(320, kStatusBarAppName, count, "--/--/----", "00:00");
    TEST_ASSERT_TRUE_MESSAGE(z.name_end < z.count_start, count.c_str());
    TEST_ASSERT_TRUE_MESSAGE(z.count_end < z.right_start, count.c_str());
    TEST_ASSERT_TRUE(z.right_end <= 320);
    TEST_ASSERT_TRUE(z.name_x >= 0);
  }
}

static void test_count_is_centred(void) {
  HeaderZones z = statusBarZones(320, kStatusBarAppName, "7 LOT\xC3\x93W", "18/10/2026", "08:46");
  TEST_ASSERT_INT_WITHIN(1, 160, (z.count_start + z.count_end) / 2);
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

  RUN_TEST(test_block_label_placeholder_before_first_data);
  RUN_TEST(test_block_label_polish_plurals);
  RUN_TEST(test_block_label_english);
  RUN_TEST(test_block_label_caps_large_counts);
  RUN_TEST(test_block_label_never_longer_than_11_chars);

  RUN_TEST(test_health_dot_colour);
  RUN_TEST(test_header_uses_no_button_colour);
  RUN_TEST(test_zones_do_not_overlap_for_longest_texts);
  RUN_TEST(test_count_is_centred);

  return UNITY_END();
}
