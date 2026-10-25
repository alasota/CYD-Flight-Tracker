// opensky_auth: did the OpenSky login work, daily limit. spec/planned/06 (first slice) + 08 §2.
#include <unity.h>

#include "opensky_auth.h"
#include "test_support.h"

namespace {

using Mode = OpenSkyAuthState::Mode;

static void test_no_credentials(void) {
  OpenSkyAuthState s;
  s.mode = Mode::LoginFailed;
  s.lastHttpCode = 401;
  const OpenSkyAuthState n = onCredentials(s, 123, false);
  ASSERT_ENUM_EQ(Mode::NoCredentials, n.mode);
  TEST_ASSERT_EQUAL_INT(0, n.lastHttpCode);
  // A token answer can't turn "no credentials" into anything else.
  ASSERT_ENUM_EQ(Mode::NoCredentials, onTokenResult(n, 401, false).mode);
}

static void test_new_credentials_get_a_fair_try(void) {
  const OpenSkyAuthState a = onCredentials(OpenSkyAuthState{}, 7, true);
  ASSERT_ENUM_EQ(Mode::Account, a.mode);
  TEST_ASSERT_EQUAL_UINT32(7, a.credentialsHash);
  OpenSkyAuthState failed = onTokenResult(a, 401, false);
  ASSERT_ENUM_EQ(Mode::LoginFailed, failed.mode);
  // The same credentials again: still failed.
  ASSERT_ENUM_EQ(Mode::LoginFailed, onCredentials(failed, 7, true).mode);
  // Others (saved on the config page): Account until a token says otherwise.
  const OpenSkyAuthState other = onCredentials(failed, 8, true);
  ASSERT_ENUM_EQ(Mode::Account, other.mode);
  TEST_ASSERT_EQUAL_INT(0, other.lastHttpCode);
}

static void test_token_results(void) {
  const OpenSkyAuthState a = onCredentials(OpenSkyAuthState{}, 1, true);
  for (int code : {400, 401, 403}) {
    const OpenSkyAuthState f = onTokenResult(a, code, false);
    ASSERT_ENUM_EQ(Mode::LoginFailed, f.mode);
    TEST_ASSERT_EQUAL_INT(code, f.lastHttpCode);
  }
  // Transient: no answer, 5xx, 200 without a token — unchanged either way.
  for (int code : {0, 500, 503, 200}) {
    ASSERT_ENUM_EQ(Mode::Account, onTokenResult(a, code, false).mode);
    const OpenSkyAuthState f = onTokenResult(a, 401, false);
    ASSERT_ENUM_EQ(Mode::LoginFailed, onTokenResult(f, code, false).mode);
  }
  // A granted token clears a failure.
  const OpenSkyAuthState ok = onTokenResult(onTokenResult(a, 401, false), 200, true);
  ASSERT_ENUM_EQ(Mode::Account, ok.mode);
  TEST_ASSERT_EQUAL_INT(0, ok.lastHttpCode);
}

static void test_daily_limit(void) {
  OpenSkyAuthState s;
  TEST_ASSERT_EQUAL_UINT32(400, openSkyDailyLimit(s));
  s.mode = Mode::Account;
  TEST_ASSERT_EQUAL_UINT32(4000, openSkyDailyLimit(s));
  s.mode = Mode::LoginFailed;  // runs anonymously
  TEST_ASSERT_EQUAL_UINT32(400, openSkyDailyLimit(s));
}

static void test_credentials_hash(void) {
  TEST_ASSERT_EQUAL_UINT32(credentialsHash("id", "secret"), credentialsHash("id", "secret"));
  TEST_ASSERT_TRUE(credentialsHash("id", "secret") != credentialsHash("id", "secreT"));
  // The separator keeps "ab"+"c" and "a"+"bc" apart.
  TEST_ASSERT_TRUE(credentialsHash("ab", "c") != credentialsHash("a", "bc"));
}

}  // namespace

void run_opensky_auth_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_no_credentials);
  RUN_TEST(test_new_credentials_get_a_fair_try);
  RUN_TEST(test_token_results);
  RUN_TEST(test_daily_limit);
  RUN_TEST(test_credentials_hash);
}
