#include <unity.h>

#include <string>

#include "web_auth.h"

void setUp(void) {}
void tearDown(void) {}

// ---- validateWebPassword

static void test_empty_password_means_none_and_is_ok(void) {
  TEST_ASSERT_TRUE(validateWebPassword("") == WebPasswordCheck::Ok);
}

static void test_length_bounds(void) {
  TEST_ASSERT_TRUE(validateWebPassword("1234567") == WebPasswordCheck::TooShort);
  TEST_ASSERT_TRUE(validateWebPassword("12345678") == WebPasswordCheck::Ok);
  TEST_ASSERT_TRUE(validateWebPassword(std::string(64, 'a')) == WebPasswordCheck::Ok);
  TEST_ASSERT_TRUE(validateWebPassword(std::string(65, 'a')) == WebPasswordCheck::TooLong);
}

static void test_printable_ascii_only(void) {
  TEST_ASSERT_TRUE(validateWebPassword("with space ~!") == WebPasswordCheck::Ok);
  TEST_ASSERT_TRUE(validateWebPassword("za\xC5\xBC\xC3\xB3\xC5\x82\x63") ==
                   WebPasswordCheck::BadChar);  // "zażółc"
  TEST_ASSERT_TRUE(validateWebPassword("tab\there1") == WebPasswordCheck::BadChar);
  TEST_ASSERT_TRUE(validateWebPassword("abc\x7F" "defgh") == WebPasswordCheck::BadChar);
}

// ---- decidePasswordChange

static void test_both_fields_empty_leaves_password_alone(void) {
  TEST_ASSERT_TRUE(decidePasswordChange("", "", false).outcome ==
                   PasswordChange::Outcome::Unchanged);
}

static void test_matching_valid_password_is_set(void) {
  PasswordChange c = decidePasswordChange("hunter22", "hunter22", false);
  TEST_ASSERT_TRUE(c.outcome == PasswordChange::Outcome::Set);
  TEST_ASSERT_EQUAL_STRING("hunter22", c.value.c_str());
}

static void test_mismatch_and_invalid_save_nothing(void) {
  TEST_ASSERT_TRUE(decidePasswordChange("hunter22", "hunter23", false).outcome ==
                   PasswordChange::Outcome::Mismatch);
  TEST_ASSERT_TRUE(decidePasswordChange("hunter22", "", false).outcome ==
                   PasswordChange::Outcome::Mismatch);
  PasswordChange c = decidePasswordChange("short", "short", false);
  TEST_ASSERT_TRUE(c.outcome == PasswordChange::Outcome::Invalid);
  TEST_ASSERT_TRUE(c.check == WebPasswordCheck::TooShort);
  TEST_ASSERT_TRUE(c.value.empty());
}

static void test_clear_wins_over_typed_fields(void) {
  TEST_ASSERT_TRUE(decidePasswordChange("hunter22", "nope", true).outcome ==
                   PasswordChange::Outcome::Cleared);
}

// ---- AuthThrottle

static void test_locks_after_five_failures_in_a_row(void) {
  AuthThrottle t;
  for (int i = 0; i < 4; ++i) t.onFailure(1000);
  TEST_ASSERT_FALSE(t.isLocked(1000));
  t.onFailure(1000);
  TEST_ASSERT_TRUE(t.isLocked(1000));
  TEST_ASSERT_EQUAL_UINT32(60, t.retryAfterS(1000));
}

static void test_lock_lasts_exactly_sixty_seconds(void) {
  AuthThrottle t;
  for (int i = 0; i < 5; ++i) t.onFailure(5000);
  TEST_ASSERT_TRUE(t.isLocked(5000 + 59999));
  TEST_ASSERT_EQUAL_UINT32(1, t.retryAfterS(5000 + 59999));
  TEST_ASSERT_FALSE(t.isLocked(5000 + 60000));
  TEST_ASSERT_EQUAL_UINT32(0, t.retryAfterS(5000 + 60000));
}

static void test_failures_while_locked_do_not_extend_it(void) {
  AuthThrottle t;
  for (int i = 0; i < 5; ++i) t.onFailure(0);
  t.onFailure(30000);
  TEST_ASSERT_FALSE(t.isLocked(60000));
}

static void test_after_the_lock_counting_starts_again(void) {
  AuthThrottle t;
  for (int i = 0; i < 5; ++i) t.onFailure(0);
  t.onFailure(70000);
  TEST_ASSERT_FALSE(t.isLocked(70000));
  TEST_ASSERT_EQUAL_INT(1, t.failures());
}

static void test_success_resets_the_count(void) {
  AuthThrottle t;
  for (int i = 0; i < 4; ++i) t.onFailure(0);
  t.onSuccess();
  t.onFailure(0);
  TEST_ASSERT_FALSE(t.isLocked(0));
  TEST_ASSERT_EQUAL_INT(1, t.failures());
}

static void test_lock_survives_millis_wraparound(void) {
  AuthThrottle t;
  const uint32_t start = 0xFFFFFFFFu - 1000u;
  for (int i = 0; i < 5; ++i) t.onFailure(start);
  TEST_ASSERT_TRUE(t.isLocked(start + 30000u));  // wrapped past 0
  TEST_ASSERT_FALSE(t.isLocked(start + 60000u));
}

// ---- decideAuth

static void test_no_password_lets_everything_through(void) {
  AuthOutcome o = decideAuth(false, true, true, false);
  TEST_ASSERT_TRUE(o.decision == AuthDecision::Allow);
  TEST_ASSERT_FALSE(o.countFailure);
}

static void test_first_request_without_credentials_is_challenged_not_counted(void) {
  AuthOutcome o = decideAuth(true, false, false, false);
  TEST_ASSERT_TRUE(o.decision == AuthDecision::Challenge);
  TEST_ASSERT_FALSE(o.countFailure);
}

static void test_wrong_password_is_challenged_and_counted(void) {
  AuthOutcome o = decideAuth(true, false, true, false);
  TEST_ASSERT_TRUE(o.decision == AuthDecision::Challenge);
  TEST_ASSERT_TRUE(o.countFailure);
}

static void test_right_password_is_allowed(void) {
  AuthOutcome o = decideAuth(true, false, true, true);
  TEST_ASSERT_TRUE(o.decision == AuthDecision::Allow);
  TEST_ASSERT_TRUE(o.countSuccess);
}

static void test_locked_refuses_even_the_right_password(void) {
  TEST_ASSERT_TRUE(decideAuth(true, true, true, true).decision == AuthDecision::Locked);
}

// ---- reset gesture

static void test_reset_hold(void) {
  TEST_ASSERT_TRUE(resetHoldState(0, 0) == ResetHoldState::Holding);
  TEST_ASSERT_TRUE(resetHoldState(0, 4999) == ResetHoldState::Holding);
  TEST_ASSERT_TRUE(resetHoldState(0, 5000) == ResetHoldState::Done);
  // A dropped sample or two doesn't end the hold, a real release does.
  TEST_ASSERT_TRUE(resetHoldState(300, 2000) == ResetHoldState::Holding);
  TEST_ASSERT_TRUE(resetHoldState(301, 2000) == ResetHoldState::Released);
  TEST_ASSERT_TRUE(resetHoldState(1000, 9000) == ResetHoldState::Released);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_password_means_none_and_is_ok);
  RUN_TEST(test_length_bounds);
  RUN_TEST(test_printable_ascii_only);
  RUN_TEST(test_both_fields_empty_leaves_password_alone);
  RUN_TEST(test_matching_valid_password_is_set);
  RUN_TEST(test_mismatch_and_invalid_save_nothing);
  RUN_TEST(test_clear_wins_over_typed_fields);
  RUN_TEST(test_locks_after_five_failures_in_a_row);
  RUN_TEST(test_lock_lasts_exactly_sixty_seconds);
  RUN_TEST(test_failures_while_locked_do_not_extend_it);
  RUN_TEST(test_after_the_lock_counting_starts_again);
  RUN_TEST(test_success_resets_the_count);
  RUN_TEST(test_lock_survives_millis_wraparound);
  RUN_TEST(test_no_password_lets_everything_through);
  RUN_TEST(test_first_request_without_credentials_is_challenged_not_counted);
  RUN_TEST(test_wrong_password_is_challenged_and_counted);
  RUN_TEST(test_right_password_is_allowed);
  RUN_TEST(test_locked_refuses_even_the_right_password);
  RUN_TEST(test_reset_hold);
  return UNITY_END();
}
