// opensky_quota: X-Rate-Limit headers -> credits left. docs/data-sources.md "Rate limits".
#include <unity.h>

#include "opensky_quota.h"
#include "test_support.h"

namespace {

using State = OpenSkyQuota::State;

static void test_parse_header(void) {
  uint32_t v = 7;
  TEST_ASSERT_TRUE(parseQuotaHeader("1234", &v));
  TEST_ASSERT_EQUAL_UINT32(1234u, v);
  TEST_ASSERT_TRUE(parseQuotaHeader("0", &v));
  TEST_ASSERT_EQUAL_UINT32(0u, v);
  TEST_ASSERT_TRUE(parseQuotaHeader("99999999999", &v));  // saturates
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, v);
  TEST_ASSERT_FALSE(parseQuotaHeader("", &v));
  TEST_ASSERT_FALSE(parseQuotaHeader("-5", &v));
  TEST_ASSERT_FALSE(parseQuotaHeader("12 ", &v));
  TEST_ASSERT_FALSE(parseQuotaHeader("abc", &v));
}

static void test_ok_answer_reports_remaining_credits(void) {
  OpenSkyQuota q = updateQuota(OpenSkyQuota{}, 200, "3971", "", 5000);
  ASSERT_ENUM_EQ(State::Available, q.state);
  TEST_ASSERT_EQUAL_UINT32(3971u, q.remaining);
  TEST_ASSERT_EQUAL_UINT32(5000u, q.observed_ms);
}

static void test_ok_answer_without_header_keeps_what_we_knew(void) {
  OpenSkyQuota known = updateQuota(OpenSkyQuota{}, 200, "100", "", 0);
  OpenSkyQuota q = updateQuota(known, 200, "", "", 1000);
  ASSERT_ENUM_EQ(State::Available, q.state);
  TEST_ASSERT_EQUAL_UINT32(100u, q.remaining);

  ASSERT_ENUM_EQ(State::Unknown, updateQuota(OpenSkyQuota{}, 200, "", "", 0).state);
}

static void test_429_marks_exhausted_with_the_real_wait(void) {
  // Not clamped like the client's backoff (3600 s): this is for showing
  // when the pool really renews.
  OpenSkyQuota q = updateQuota(updateQuota(OpenSkyQuota{}, 200, "5", "", 0), 429, "", "25012", 9000);
  ASSERT_ENUM_EQ(State::Exhausted, q.state);
  TEST_ASSERT_EQUAL_UINT32(0u, q.remaining);
  TEST_ASSERT_EQUAL_UINT32(25012u, q.reset_in_s);
  TEST_ASSERT_EQUAL_UINT32(9000u, q.observed_ms);

  OpenSkyQuota noHeader = updateQuota(OpenSkyQuota{}, 429, "", "", 0);
  ASSERT_ENUM_EQ(State::Exhausted, noHeader.state);
  TEST_ASSERT_EQUAL_UINT32(0u, noHeader.reset_in_s);
}

static void test_pool_back_after_exhaustion(void) {
  OpenSkyQuota ex = updateQuota(OpenSkyQuota{}, 429, "", "60", 0);
  ASSERT_ENUM_EQ(State::Available, updateQuota(ex, 200, "4000", "", 70000).state);
  ASSERT_ENUM_EQ(State::Unknown, updateQuota(ex, 200, "", "", 70000).state);
}

static void test_other_answers_change_nothing(void) {
  OpenSkyQuota known = updateQuota(OpenSkyQuota{}, 200, "321", "", 0);
  for (int code : {401, 403, 500, 503, -1}) {
    OpenSkyQuota q = updateQuota(known, code, "1", "1", 99);
    ASSERT_ENUM_EQ(State::Available, q.state);
    TEST_ASSERT_EQUAL_UINT32(321u, q.remaining);
  }
}

static void test_seconds_until_reset_counts_down(void) {
  OpenSkyQuota q = updateQuota(OpenSkyQuota{}, 429, "", "3600", 10000);
  TEST_ASSERT_EQUAL_UINT32(3600u, quotaSecondsUntilReset(q, 10000));
  TEST_ASSERT_EQUAL_UINT32(3540u, quotaSecondsUntilReset(q, 70000));
  TEST_ASSERT_EQUAL_UINT32(0u, quotaSecondsUntilReset(q, 10000 + 3600000));
  TEST_ASSERT_EQUAL_UINT32(0u, quotaSecondsUntilReset(q, 10000 + 9000000));
  // millis() wrap between the 429 and now
  OpenSkyQuota w = updateQuota(OpenSkyQuota{}, 429, "", "100", 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_UINT32(99u, quotaSecondsUntilReset(w, 0x00000500u));  // 1.5 s later
  // not exhausted / unknown wait
  TEST_ASSERT_EQUAL_UINT32(0u, quotaSecondsUntilReset(OpenSkyQuota{}, 5));
  TEST_ASSERT_EQUAL_UINT32(0u, quotaSecondsUntilReset(updateQuota(OpenSkyQuota{}, 429, "", "", 0), 5));
}

static void test_format_wait(void) {
  TEST_ASSERT_EQUAL_STRING("< 1 min", formatWait(0, Language::EN).c_str());
  TEST_ASSERT_EQUAL_STRING("< 1 min", formatWait(59, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("45 min", formatWait(45 * 60, Language::EN).c_str());
  TEST_ASSERT_EQUAL_STRING("2 h", formatWait(7200, Language::EN).c_str());
  TEST_ASSERT_EQUAL_STRING("2 godz.", formatWait(7200, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("6 h 57 min", formatWait(25012, Language::EN).c_str());  // 416.9 min
  TEST_ASSERT_EQUAL_STRING("6 godz. 57 min", formatWait(25012, Language::PL).c_str());
  TEST_ASSERT_EQUAL_STRING("1 h", formatWait(3599, Language::EN).c_str());  // rounds to 60 min
}

}  // namespace

void run_opensky_quota_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_parse_header);
  RUN_TEST(test_ok_answer_reports_remaining_credits);
  RUN_TEST(test_ok_answer_without_header_keeps_what_we_knew);
  RUN_TEST(test_429_marks_exhausted_with_the_real_wait);
  RUN_TEST(test_pool_back_after_exhaustion);
  RUN_TEST(test_other_answers_change_nothing);
  RUN_TEST(test_seconds_until_reset_counts_down);
  RUN_TEST(test_format_wait);
}
