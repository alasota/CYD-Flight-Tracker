// time_sync: date / time formatting from an injected time_t. docs/screens.md "Time".
#include <unity.h>

#include "platform/time_sync.h"

namespace {

// Reference epochs (UTC), chosen so the expected civil breakdown is easy
// to verify independently:
//   0           -> 1970-01-01 00:00:00  (Thu)
//   1000000000  -> 2001-09-09 01:46:40  (Sun)
//   1500000000  -> 2017-07-14 02:40:00  (Fri)
//   1583020800  -> 2020-03-01 00:00:00  (leap year, past Feb 29)
//   1772841600  -> 2026-03-07 00:00:00  (single-digit day and month)

// ---- formatDate -----------------------------------------------------------

static void test_format_date_epoch_zero(void) {
  TEST_ASSERT_EQUAL_STRING("01.01.1970", formatDate(0).c_str());
}

static void test_format_date_known_values(void) {
  TEST_ASSERT_EQUAL_STRING("09.09.2001", formatDate(1000000000).c_str());
  TEST_ASSERT_EQUAL_STRING("14.07.2017", formatDate(1500000000).c_str());
}

static void test_format_date_pads_day_and_month(void) {
  TEST_ASSERT_EQUAL_STRING("07.03.2026", formatDate(1772841600).c_str());
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

// ---- minuteOf -------------------------------------------------------------

static void test_minute_of_same_within_a_minute(void) {
  TEST_ASSERT_EQUAL(minuteOf(1500000000), minuteOf(1500000000 + 59));
}

static void test_minute_of_changes_at_minute_boundary(void) {
  // 1500000000 is exactly 02:40:00.
  TEST_ASSERT_NOT_EQUAL(minuteOf(1500000000 - 1), minuteOf(1500000000));
  TEST_ASSERT_NOT_EQUAL(minuteOf(1500000000 + 59), minuteOf(1500000000 + 60));
}

}  // namespace

void run_time_sync_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_format_date_epoch_zero);
  RUN_TEST(test_format_date_known_values);
  RUN_TEST(test_format_date_pads_day_and_month);
  RUN_TEST(test_format_date_leap_year_after_feb29);
  RUN_TEST(test_format_date_negative_epoch_clamps);

  RUN_TEST(test_format_time_epoch_zero);
  RUN_TEST(test_format_time_known_values);
  RUN_TEST(test_format_time_pads_single_digits);

  RUN_TEST(test_minute_of_same_within_a_minute);
  RUN_TEST(test_minute_of_changes_at_minute_boundary);

}
