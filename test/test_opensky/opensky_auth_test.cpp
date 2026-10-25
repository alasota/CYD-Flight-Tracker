// opensky_auth: did the OpenSky login work, 30 min between rejected logins, Bearer / retry decisions, daily limit. spec/done/06-opensky-login-fallback.md + 08 §2.
#include <unity.h>

#include <cstdint>
#include <cstdio>

#include "opensky_auth.h"
#include "test_support.h"

namespace {

using Mode = OpenSkyAuthState::Mode;

constexpr uint32_t kT = 1000000u;  // "now" of the rejection in most tests

OpenSkyAuthState account(uint32_t hash = 1) { return onCredentials(OpenSkyAuthState{}, hash, true); }

// LoginFailed built by hand, so tests of other functions don't depend on onTokenResult().
OpenSkyAuthState failedState(int code, uint32_t sinceMs, uint32_t hash = 1) {
  OpenSkyAuthState s = account(hash);
  s.mode = Mode::LoginFailed;
  s.lastHttpCode = code;
  s.failedSinceMs = sinceMs;
  s.nextTokenTryMs = sinceMs + kLoginRetryMs;
  return s;
}

// "http=401" — names the table row in a failure message.
const char *row(int code) {
  static char buf[24];
  std::snprintf(buf, sizeof(buf), "http=%d", code);
  return buf;
}

void assertSameAuth(const OpenSkyAuthState &want, const OpenSkyAuthState &got, int code) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(want.mode), static_cast<int>(got.mode), row(code));
  TEST_ASSERT_EQUAL_INT_MESSAGE(want.lastHttpCode, got.lastHttpCode, row(code));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.credentialsHash, got.credentialsHash, row(code));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.failedSinceMs, got.failedSinceMs, row(code));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.nextTokenTryMs, got.nextTokenTryMs, row(code));
}

void assertStep(StatesStep want, StatesStep got, int code) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(want), static_cast<int>(got), row(code));
}

// ---- existing (spec 08 first slice) -----------------------------------------

static void test_no_credentials(void) {
  OpenSkyAuthState s;
  s.mode = Mode::LoginFailed;
  s.lastHttpCode = 401;
  const OpenSkyAuthState n = onCredentials(s, 123, false);
  ASSERT_ENUM_EQ(Mode::NoCredentials, n.mode);
  TEST_ASSERT_EQUAL_INT(0, n.lastHttpCode);
}

static void test_new_credentials_get_a_fair_try(void) {
  const OpenSkyAuthState a = onCredentials(OpenSkyAuthState{}, 7, true);
  ASSERT_ENUM_EQ(Mode::Account, a.mode);
  TEST_ASSERT_EQUAL_UINT32(7, a.credentialsHash);
  const OpenSkyAuthState failed = failedState(401, kT, 7);
  // The same credentials again: still failed.
  ASSERT_ENUM_EQ(Mode::LoginFailed, onCredentials(failed, 7, true).mode);
  // Others (saved on the config page): Account until a token says otherwise.
  const OpenSkyAuthState other = onCredentials(failed, 8, true);
  ASSERT_ENUM_EQ(Mode::Account, other.mode);
  TEST_ASSERT_EQUAL_INT(0, other.lastHttpCode);
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

// ---- spec 06: login state and the wait between token attempts (§3.1) ---------

// Scenario Outline: Odpowiedź serwera logowania jest klasyfikowana jako Ok, Rejected albo Transient
static void test_classify_token_response(void) {
  struct Row {
    int code;
    bool token;
    TokenOutcome want;
  };
  const Row rows[] = {
      {200, true, TokenOutcome::Ok},        {200, false, TokenOutcome::Transient},
      {400, false, TokenOutcome::Rejected}, {401, false, TokenOutcome::Rejected},
      {403, false, TokenOutcome::Rejected}, {0, false, TokenOutcome::Transient},
      {-1, false, TokenOutcome::Transient}, {500, false, TokenOutcome::Transient},
      {503, false, TokenOutcome::Transient},
  };
  for (const Row &r : rows) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(r.want),
                                  static_cast<int>(classifyTokenResponse(r.code, r.token)),
                                  row(r.code));
  }
}

// Scenario: Odrzucone dane logowania wstrzymują próby tokenu na 30 minut
static void test_rejected_login_waits_30_min(void) {
  TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, kLoginRetryMs);
  const OpenSkyAuthState a = account();
  TEST_ASSERT_TRUE(shouldTryToken(a, kT));
  const OpenSkyAuthState f = onTokenResult(a, TokenOutcome::Rejected, 401, kT);
  ASSERT_ENUM_EQ(Mode::LoginFailed, f.mode);
  TEST_ASSERT_EQUAL_INT(401, f.lastHttpCode);
  TEST_ASSERT_EQUAL_UINT32(kT, f.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(kT + kLoginRetryMs, f.nextTokenTryMs);
  TEST_ASSERT_FALSE(shouldTryToken(f, kT));
  TEST_ASSERT_FALSE(shouldTryToken(f, kT + 15000u));
  TEST_ASSERT_FALSE(shouldTryToken(f, kT + kLoginRetryMs - 1));
  TEST_ASSERT_TRUE(shouldTryToken(f, kT + kLoginRetryMs));
  TEST_ASSERT_TRUE(shouldTryToken(f, kT + kLoginRetryMs + 60000u));
}

// Scenario: Odstęp 30 minut działa przez przepełnienie millis()
static void test_retry_wait_survives_millis_rollover(void) {
  const uint32_t t = UINT32_MAX - 1000u;
  const OpenSkyAuthState f = onTokenResult(account(), TokenOutcome::Rejected, 401, t);
  ASSERT_ENUM_EQ(Mode::LoginFailed, f.mode);
  TEST_ASSERT_EQUAL_UINT32(t, f.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(t + kLoginRetryMs, f.nextTokenTryMs);  // wrapped past zero
  TEST_ASSERT_FALSE(shouldTryToken(f, t));
  TEST_ASSERT_FALSE(shouldTryToken(f, UINT32_MAX));
  TEST_ASSERT_FALSE(shouldTryToken(f, t + 2000u));  // millis() has wrapped
  TEST_ASSERT_FALSE(shouldTryToken(f, t + kLoginRetryMs - 1));
  TEST_ASSERT_TRUE(shouldTryToken(f, t + kLoginRetryMs));
  TEST_ASSERT_TRUE(shouldTryToken(f, t + kLoginRetryMs + 1));
}

// Scenario: Kolejne odrzucenie po 30 minutach przesuwa następną próbę, ale nie zmienia „od kiedy”
static void test_repeated_rejection_keeps_first_failure_time(void) {
  const OpenSkyAuthState f = failedState(401, kT);
  const uint32_t again = kT + kLoginRetryMs;
  const OpenSkyAuthState n = onTokenResult(f, TokenOutcome::Rejected, 403, again);
  ASSERT_ENUM_EQ(Mode::LoginFailed, n.mode);
  TEST_ASSERT_EQUAL_INT(403, n.lastHttpCode);  // the latest answer
  TEST_ASSERT_EQUAL_UINT32(kT, n.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(again + kLoginRetryMs, n.nextTokenTryMs);
  TEST_ASSERT_FALSE(shouldTryToken(n, again));
  TEST_ASSERT_FALSE(shouldTryToken(n, again + kLoginRetryMs - 1));
  TEST_ASSERT_TRUE(shouldTryToken(n, again + kLoginRetryMs));
}

// Scenario: Przejściowy błąd logowania przy działającym koncie nie jest błędem logowania
static void test_transient_error_keeps_account(void) {
  const OpenSkyAuthState a = account();
  // No answer, -1 (HTTPClient's connection error), 5xx, 200 without a token.
  for (int code : {0, -1, 500, 503, 200}) {
    const OpenSkyAuthState n = onTokenResult(a, classifyTokenResponse(code, false), code, kT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(Mode::Account), static_cast<int>(n.mode),
                                  row(code));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, n.lastHttpCode, row(code));
    TEST_ASSERT_TRUE_MESSAGE(shouldTryToken(n, kT + 15000u), row(code));  // next poll tries again
  }
}

// Scenario: Przejściowy błąd w stanie LoginFailed też odsuwa próbę o 30 minut
static void test_transient_error_while_failed_waits_30_min(void) {
  const OpenSkyAuthState f = failedState(401, kT);
  const uint32_t retry = kT + kLoginRetryMs;
  const OpenSkyAuthState n = onTokenResult(f, TokenOutcome::Transient, 0, retry);
  ASSERT_ENUM_EQ(Mode::LoginFailed, n.mode);
  TEST_ASSERT_EQUAL_INT(401, n.lastHttpCode);  // not the 0 of "no answer"
  TEST_ASSERT_EQUAL_UINT32(kT, n.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(retry + kLoginRetryMs, n.nextTokenTryMs);
  TEST_ASSERT_FALSE(shouldTryToken(n, retry + kLoginRetryMs - 1));
  TEST_ASSERT_TRUE(shouldTryToken(n, retry + kLoginRetryMs));
}

// Scenario: Zapisanie innych danych logowania pozwala spróbować od razu
static void test_new_credentials_try_at_once(void) {
  const OpenSkyAuthState f = failedState(401, kT, 1);
  const OpenSkyAuthState n = onCredentials(f, 2, true);
  ASSERT_ENUM_EQ(Mode::Account, n.mode);
  TEST_ASSERT_EQUAL_INT(0, n.lastHttpCode);
  TEST_ASSERT_EQUAL_UINT32(2, n.credentialsHash);
  TEST_ASSERT_EQUAL_UINT32(0, n.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(0, n.nextTokenTryMs);
  TEST_ASSERT_TRUE(shouldTryToken(n, kT + 60000u));  // still inside the old wait
}

// Scenario: Bez danych logowania płytka nigdy nie pyta serwera logowania
static void test_no_credentials_never_tries_token(void) {
  const OpenSkyAuthState n = onCredentials(failedState(401, kT), 0, false);
  ASSERT_ENUM_EQ(Mode::NoCredentials, n.mode);
  for (uint32_t now : {0u, kT, kT + kLoginRetryMs, UINT32_MAX}) {
    TEST_ASSERT_FALSE(shouldTryToken(n, now));
  }
  TEST_ASSERT_FALSE(planStatesRequest(n, false));
  TEST_ASSERT_FALSE(planStatesRequest(n, true));
}

// Edge case: a token answer can't turn "no credentials" into anything else.
static void test_no_credentials_ignore_token_results(void) {
  const OpenSkyAuthState none;
  struct Row {
    TokenOutcome o;
    int code;
  };
  const Row rows[] = {{TokenOutcome::Ok, 200}, {TokenOutcome::Rejected, 401},
                      {TokenOutcome::Transient, 0}};
  for (const Row &r : rows) {
    assertSameAuth(none, onTokenResult(none, r.o, r.code, kT), r.code);
  }
}

// Edge case: a granted token clears every trace of the failure.
static void test_token_ok_clears_failure_fields(void) {
  const OpenSkyAuthState f = failedState(401, kT);
  const uint32_t retry = kT + kLoginRetryMs;
  const OpenSkyAuthState n = onTokenResult(f, TokenOutcome::Ok, 200, retry);
  ASSERT_ENUM_EQ(Mode::Account, n.mode);
  TEST_ASSERT_EQUAL_INT(0, n.lastHttpCode);
  TEST_ASSERT_EQUAL_UINT32(0, n.failedSinceMs);
  TEST_ASSERT_EQUAL_UINT32(0, n.nextTokenTryMs);
  TEST_ASSERT_EQUAL_UINT32(f.credentialsHash, n.credentialsHash);
  TEST_ASSERT_TRUE(shouldTryToken(n, retry));
  TEST_ASSERT_TRUE(n.tokenGranted);
  // A later rejection takes it back.
  TEST_ASSERT_FALSE(onTokenResult(n, TokenOutcome::Rejected, 401, retry + 1).tokenGranted);
}

// Edge case (review): failedSinceMs is a millis() value. A series older than
// kLoginFailedSinceMaxMs is marked stale at its next token result, so "since"
// stays hidden after millis() wraps; a new series or a granted token clears it.
static void test_long_failure_series_marks_since_stale(void) {
  const OpenSkyAuthState f = failedState(401, kT);
  TEST_ASSERT_TRUE(loginFailedSinceKnown(f, kT));
  TEST_ASSERT_TRUE(loginFailedSinceKnown(f, kT + kLoginFailedSinceMaxMs));
  TEST_ASSERT_FALSE(loginFailedSinceKnown(f, kT + kLoginFailedSinceMaxMs + 1u));
  TEST_ASSERT_FALSE(loginFailedSinceKnown(account(), kT));  // only LoginFailed has a "since"

  // Within the bound: a rejection keeps the series fresh.
  const OpenSkyAuthState early = onTokenResult(f, TokenOutcome::Rejected, 401, kT + kLoginRetryMs);
  TEST_ASSERT_FALSE(early.failedSinceStale);
  // Past it (rejected, or no answer): stale, and still stale after the wrap
  // when the plain age would look small again.
  const uint32_t late = kT + kLoginFailedSinceMaxMs + 1000u;
  const OpenSkyAuthState stale = onTokenResult(f, TokenOutcome::Rejected, 401, late);
  TEST_ASSERT_TRUE(stale.failedSinceStale);
  TEST_ASSERT_EQUAL_UINT32(kT, stale.failedSinceMs);
  TEST_ASSERT_TRUE(onTokenResult(f, TokenOutcome::Transient, 0, late).failedSinceStale);
  TEST_ASSERT_FALSE(loginFailedSinceKnown(stale, kT + 60000u));  // wrapped: age looks like 1 min

  // A granted token ends the series; the next rejection starts a fresh one.
  const OpenSkyAuthState ok = onTokenResult(stale, TokenOutcome::Ok, 200, late);
  TEST_ASSERT_FALSE(ok.failedSinceStale);
  const OpenSkyAuthState again = onTokenResult(ok, TokenOutcome::Rejected, 401, late + 1u);
  TEST_ASSERT_FALSE(again.failedSinceStale);
  TEST_ASSERT_TRUE(loginFailedSinceKnown(again, late + 1u));
}

// ---- spec 06: decisions around /states/all (§3.2) ----------------------------

// Scenario Outline: Token Bearer jest wysyłany tylko z działającym kontem i ważnym tokenem
static void test_bearer_only_with_account_and_valid_token(void) {
  TEST_ASSERT_TRUE(planStatesRequest(account(), true));
  TEST_ASSERT_FALSE(planStatesRequest(account(), false));
  TEST_ASSERT_FALSE(planStatesRequest(failedState(401, kT), true));
  TEST_ASSERT_FALSE(planStatesRequest(OpenSkyAuthState{}, false));
}

// Scenario: Pierwsze 401 na /states/all z tokenem oznacza „odśwież token raz i powtórz”
// (owner decision 2026-10-25: a 403 with a token gets the same treatment)
static void test_states_401_with_bearer_refreshes_once(void) {
  const OpenSkyAuthState a = account();
  for (int code : {401, 403}) {
    const StatesDecision d = afterStatesResponse(a, code, true, false, kT);
    assertStep(StatesStep::RefreshTokenAndRetry, d.step, code);
    assertSameAuth(a, d.auth, code);
  }
}

// Scenario Outline: Świeży token też odrzucony — LoginFailed i ten sam poll anonimowo
static void test_states_rejected_after_refresh_retries_anonymously(void) {
  const OpenSkyAuthState a = account();
  for (int code : {401, 403}) {
    const StatesDecision d = afterStatesResponse(a, code, true, true, kT);
    assertStep(StatesStep::RetryAnonymous, d.step, code);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(Mode::LoginFailed),
                                  static_cast<int>(d.auth.mode), row(code));
    TEST_ASSERT_EQUAL_INT_MESSAGE(code, d.auth.lastHttpCode, row(code));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kT, d.auth.failedSinceMs, row(code));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kT + kLoginRetryMs, d.auth.nextTokenTryMs, row(code));
    // The retry goes without a token, even if the adapter still holds one.
    TEST_ASSERT_FALSE_MESSAGE(planStatesRequest(d.auth, true), row(code));
    TEST_ASSERT_FALSE_MESSAGE(shouldTryToken(d.auth, kT + kLoginRetryMs - 1), row(code));
    TEST_ASSERT_TRUE_MESSAGE(shouldTryToken(d.auth, kT + kLoginRetryMs), row(code));
  }
}

// Scenario Outline: Odrzucone zapytanie anonimowe to błąd autoryzacji
static void test_anonymous_401_is_auth_error(void) {
  const OpenSkyAuthState states[] = {OpenSkyAuthState{}, account(), failedState(401, kT)};
  for (const OpenSkyAuthState &s : states) {
    for (int code : {401, 403}) {
      for (bool refreshed : {false, true}) {
        const StatesDecision d = afterStatesResponse(s, code, false, refreshed, kT + 5000u);
        assertStep(StatesStep::AuthError, d.step, code);  // no retry of any kind
        assertSameAuth(s, d.auth, code);
      }
    }
  }
}

// Scenario: Odpowiedzi inne niż 401/403 są przyjmowane bez ponawiania
static void test_states_200_and_429_are_accepted(void) {
  const OpenSkyAuthState states[] = {OpenSkyAuthState{}, account(), failedState(401, kT)};
  for (const OpenSkyAuthState &s : states) {
    for (int code : {200, 429, 500}) {
      for (bool bearer : {false, true}) {
        for (bool refreshed : {false, true}) {
          const StatesDecision d = afterStatesResponse(s, code, bearer, refreshed, kT + 5000u);
          assertStep(StatesStep::Accept, d.step, code);
          assertSameAuth(s, d.auth, code);
        }
      }
    }
  }
}

// ---- spec 06: log (§3.2 p. 5) ------------------------------------------------

// Scenario: Przejście w LoginFailed daje jedną linię logu, kolejne odrzucenia żadnej
static void test_auth_log_line_only_on_change(void) {
  const OpenSkyAuthState a = account();
  const OpenSkyAuthState f = failedState(401, kT);
  ASSERT_STR_EQ("login failed (http=401) — check client_id/secret; using anonymous access, "
                "next try in 30 min",
                authChangeLogLine(a, f));
  // Another rejection while already failed: nothing.
  ASSERT_STR_EQ("", authChangeLogLine(f, failedState(401, kT + kLoginRetryMs)));
  ASSERT_STR_EQ("", authChangeLogLine(a, a));
  // The code in the line is the one the server sent.
  ASSERT_STR_EQ("login failed (http=403) — check client_id/secret; using anonymous access, "
                "next try in 30 min",
                authChangeLogLine(a, failedState(403, kT)));
}

// Scenario: Powrót do konta daje linię „login ok”
static void test_auth_log_line_login_ok(void) {
  const OpenSkyAuthState f = failedState(401, kT);
  ASSERT_STR_EQ("login ok — using account",
                authChangeLogLine(f, onTokenResult(f, TokenOutcome::Ok, 200, kT + kLoginRetryMs)));
}

// Edge case (review): saving other credentials while LoginFailed is not a
// login yet — no line; the first token granted for them logs "login ok",
// and a later refresh of a working account logs nothing.
static void test_auth_log_line_login_ok_only_after_token(void) {
  const OpenSkyAuthState f = failedState(401, kT, 1);
  const OpenSkyAuthState saved = onCredentials(f, 2, true);
  ASSERT_ENUM_EQ(Mode::Account, saved.mode);
  ASSERT_STR_EQ("", authChangeLogLine(f, saved));
  // A still-wrong secret: the usual "login failed" line, no "login ok" before it.
  ASSERT_STR_EQ("login failed (http=401) — check client_id/secret; using anonymous access, "
                "next try in 30 min",
                authChangeLogLine(saved, onTokenResult(saved, TokenOutcome::Rejected, 401, kT)));
  const OpenSkyAuthState granted = onTokenResult(saved, TokenOutcome::Ok, 200, kT);
  ASSERT_STR_EQ("login ok — using account", authChangeLogLine(saved, granted));
  ASSERT_STR_EQ("", authChangeLogLine(granted, onTokenResult(granted, TokenOutcome::Ok, 200, kT)));
  // No answer from the login server is not a login result either.
  ASSERT_STR_EQ("", authChangeLogLine(saved, onTokenResult(saved, TokenOutcome::Transient, 0, kT)));
}

// Edge case: setting up or removing credentials isn't a login result — no line.
static void test_auth_log_line_silent_without_login_result(void) {
  ASSERT_STR_EQ("", authChangeLogLine(OpenSkyAuthState{}, account()));
  ASSERT_STR_EQ("", authChangeLogLine(account(), OpenSkyAuthState{}));
  ASSERT_STR_EQ("", authChangeLogLine(failedState(401, kT), OpenSkyAuthState{}));
}

}  // namespace

void run_opensky_auth_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_no_credentials);
  RUN_TEST(test_new_credentials_get_a_fair_try);
  RUN_TEST(test_daily_limit);
  RUN_TEST(test_credentials_hash);
  RUN_TEST(test_classify_token_response);
  RUN_TEST(test_rejected_login_waits_30_min);
  RUN_TEST(test_retry_wait_survives_millis_rollover);
  RUN_TEST(test_repeated_rejection_keeps_first_failure_time);
  RUN_TEST(test_transient_error_keeps_account);
  RUN_TEST(test_transient_error_while_failed_waits_30_min);
  RUN_TEST(test_new_credentials_try_at_once);
  RUN_TEST(test_no_credentials_never_tries_token);
  RUN_TEST(test_no_credentials_ignore_token_results);
  RUN_TEST(test_token_ok_clears_failure_fields);
  RUN_TEST(test_long_failure_series_marks_since_stale);
  RUN_TEST(test_bearer_only_with_account_and_valid_token);
  RUN_TEST(test_states_401_with_bearer_refreshes_once);
  RUN_TEST(test_states_rejected_after_refresh_retries_anonymously);
  RUN_TEST(test_anonymous_401_is_auth_error);
  RUN_TEST(test_states_200_and_429_are_accepted);
  RUN_TEST(test_auth_log_line_only_on_change);
  RUN_TEST(test_auth_log_line_login_ok);
  RUN_TEST(test_auth_log_line_login_ok_only_after_token);
  RUN_TEST(test_auth_log_line_silent_without_login_result);
}
