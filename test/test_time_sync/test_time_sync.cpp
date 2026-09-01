#include <unity.h>

#include "time_sync.h"

void setUp(void) {}
void tearDown(void) {}

// Reference epochs (UTC), chosen so the expected civil breakdown is easy
// to verify independently:
//   0           -> 1970-01-01 00:00:00  (Thu), day-of-year 1
//   1000000000  -> 2001-09-09 01:46:40  (Sun), day-of-year 252
//   1500000000  -> 2017-07-14 02:40:00  (Fri), day-of-year 195
//   1583020800  -> 2020-03-01 00:00:00  (leap year, past Feb 29), day-of-year 61

// ---- formatDate -----------------------------------------------------------

static void test_format_date_epoch_zero(void) {
  TEST_ASSERT_EQUAL_STRING("01.01.1970", formatDate(0).c_str());
}

static void test_format_date_known_values(void) {
  TEST_ASSERT_EQUAL_STRING("09.09.2001", formatDate(1000000000).c_str());
  TEST_ASSERT_EQUAL_STRING("14.07.2017", formatDate(1500000000).c_str());
}

static void test_format_date_leap_year_after_feb29(void) {
  TEST_ASSERT_EQUAL_STRING("01.03.2020", formatDate(1583020800).c_str());
}

static void test_format_date_negative_epoch_clamps(void) {
  TEST_ASSERT_EQUAL_STRING("01.01.1970", formatDate(-12345).c_str());
}

// ---- formatTime ---------------------------------------------------------

static void test_format_time_epoch_zero(void) {
  TEST_ASSERT_EQUAL_STRING("00:00", formatTime(0).c_str());
}

static void test_format_time_known_values(void) {
  TEST_ASSERT_EQUAL_STRING("01:46", formatTime(1000000000).c_str());
  TEST_ASSERT_EQUAL_STRING("02:40", formatTime(1500000000).c_str());
}

static void test_format_time_pads_single_digits(void) {
  // 1970-01-01 09:05:00
  TEST_ASSERT_EQUAL_STRING("09:05", formatTime(9 * 3600 + 5 * 60).c_str());
}

// ---- computeStardate ----------------------------------------------------
//
// stardate = (year - 2323) * 1000 + day_of_year * 2.7378, day_of_year==1
// on Jan 1. These are hand-computed from that exact formula — the test's
// job is to pin the number down, not to check it "looks like a stardate".

static void test_stardate_epoch_zero(void) {
  // (1970-2323)*1000 + 1*2.7378 = -353000 + 2.7378 = -352997.2622
  TEST_ASSERT_EQUAL_STRING("-352997.26", computeStardate(0).c_str());
}

static void test_stardate_2001_09_09(void) {
  // year 2001, day-of-year 252: (2001-2323)*1000 + 252*2.7378
  //   = -322000 + 689.9256 = -321310.0744
  TEST_ASSERT_EQUAL_STRING("-321310.07", computeStardate(1000000000).c_str());
}

static void test_stardate_2017_07_14(void) {
  // year 2017, day-of-year 195: (2017-2323)*1000 + 195*2.7378
  //   = -306000 + 533.871 = -305466.129
  TEST_ASSERT_EQUAL_STRING("-305466.13", computeStardate(1500000000).c_str());
}

static void test_stardate_is_deterministic_across_calls(void) {
  std::string a = computeStardate(1500000000);
  std::string b = computeStardate(1500000000);
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

static void test_stardate_leap_day_of_year(void) {
  // 2020-03-01 is day-of-year 61 in a leap year (31 Jan + 29 Feb + 1).
  // (2020-2323)*1000 + 61*2.7378 = -303000 + 167.0058 = -302832.9942
  TEST_ASSERT_EQUAL_STRING("-302832.99", computeStardate(1583020800).c_str());
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_format_date_epoch_zero);
  RUN_TEST(test_format_date_known_values);
  RUN_TEST(test_format_date_leap_year_after_feb29);
  RUN_TEST(test_format_date_negative_epoch_clamps);

  RUN_TEST(test_format_time_epoch_zero);
  RUN_TEST(test_format_time_known_values);
  RUN_TEST(test_format_time_pads_single_digits);

  RUN_TEST(test_stardate_epoch_zero);
  RUN_TEST(test_stardate_2001_09_09);
  RUN_TEST(test_stardate_2017_07_14);
  RUN_TEST(test_stardate_is_deterministic_across_calls);
  RUN_TEST(test_stardate_leap_day_of_year);

  return UNITY_END();
}
