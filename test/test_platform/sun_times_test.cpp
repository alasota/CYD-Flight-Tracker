// sun_times: sunrise / sunset equation. docs/architecture.md "Brightness & night mode", spec/03.
#include <unity.h>

#include <cstdio>
#include <cstdlib>

#include "sun_times.h"
#include "test_support.h"

namespace {

// Reference times from api.sunrise-sunset.org (UTC), fetched 2026-10-24.
// Tolerance: 3 minutes either way (spec/03 §5.1).
static int hm(int h, int m) { return h * 60 + m; }

static void assertNear(int expected, int actual) {
  int d = std::abs(expected - actual);
  if (d > 720) d = 1440 - d;  // across midnight
  char msg[48];
  snprintf(msg, sizeof(msg), "expected %d, got %d", expected, actual);
  TEST_ASSERT_TRUE_MESSAGE(d <= 3, msg);
}

static void test_warsaw_midsummer(void) {
  const SunTimes s = computeSunTimes(2026, 6, 21, 52.2297f, 21.0122f);
  ASSERT_ENUM_EQ(SunTimes::Kind::Normal, s.kind);
  assertNear(hm(2, 14), s.sunrise_min);
  assertNear(hm(19, 1), s.sunset_min);
}

static void test_warsaw_midwinter(void) {
  const SunTimes s = computeSunTimes(2026, 12, 21, 52.2297f, 21.0122f);
  assertNear(hm(6, 43), s.sunrise_min);
  assertNear(hm(14, 25), s.sunset_min);
}

static void test_warsaw_autumn_in_local_time(void) {
  // 24.10.2026 is still CEST (UTC+2): 07:17 / 17:23 local.
  const SunTimes s = shiftSunTimes(computeSunTimes(2026, 10, 24, 52.2297f, 21.0122f), 120);
  assertNear(hm(7, 17), s.sunrise_min);
  assertNear(hm(17, 23), s.sunset_min);
}

static void test_equator_at_the_equinox(void) {
  const SunTimes s = computeSunTimes(2026, 3, 20, 0.0f, 0.0f);
  assertNear(hm(6, 4), s.sunrise_min);
  assertNear(hm(18, 11), s.sunset_min);
}

static void test_western_longitude(void) {
  const SunTimes s = computeSunTimes(2026, 3, 20, 40.7128f, -74.006f);  // New York
  assertNear(hm(10, 59), s.sunrise_min);
  assertNear(hm(23, 8), s.sunset_min);
}

static void test_sunset_after_utc_midnight_wraps(void) {
  const SunTimes s = computeSunTimes(2026, 6, 21, 34.05f, -118.24f);  // Los Angeles
  assertNear(hm(12, 42), s.sunrise_min);
  assertNear(hm(3, 8), s.sunset_min);  // 03:07 UTC the next day
}

static void test_southern_hemisphere(void) {
  const SunTimes s = computeSunTimes(2026, 12, 21, -33.8688f, 151.2093f);  // Sydney, summer
  assertNear(hm(18, 41), s.sunrise_min);  // 18:40 UTC the day before
  assertNear(hm(9, 5), s.sunset_min);
}

static void test_polar_day_and_night(void) {
  ASSERT_ENUM_EQ(SunTimes::Kind::PolarDay, computeSunTimes(2026, 6, 21, 69.6492f, 18.9553f).kind);  // Tromsø
  ASSERT_ENUM_EQ(SunTimes::Kind::PolarNight, computeSunTimes(2026, 12, 21, 69.6492f, 18.9553f).kind);
}

static void test_shift_wraps_both_ways(void) {
  SunTimes s;
  s.sunrise_min = 30;
  s.sunset_min = 1430;
  SunTimes plus = shiftSunTimes(s, 60);
  TEST_ASSERT_EQUAL_INT(90, plus.sunrise_min);
  TEST_ASSERT_EQUAL_INT(50, plus.sunset_min);
  SunTimes minus = shiftSunTimes(s, -60);
  TEST_ASSERT_EQUAL_INT(1410, minus.sunrise_min);
  TEST_ASSERT_EQUAL_INT(1370, minus.sunset_min);
}

}  // namespace

void run_sun_times_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_warsaw_midsummer);
  RUN_TEST(test_warsaw_midwinter);
  RUN_TEST(test_warsaw_autumn_in_local_time);
  RUN_TEST(test_equator_at_the_equinox);
  RUN_TEST(test_western_longitude);
  RUN_TEST(test_sunset_after_utc_midnight_wraps);
  RUN_TEST(test_southern_hemisphere);
  RUN_TEST(test_polar_day_and_night);
  RUN_TEST(test_shift_wraps_both_ways);
}
