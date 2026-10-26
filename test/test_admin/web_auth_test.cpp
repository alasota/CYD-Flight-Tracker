// web_auth: panel password, digest decision, throttle, cross-site origin check (spec 10). CLAUDE.md "Firmware updates (OTA) & panel password".
#include <unity.h>

#include <string>
#include <vector>

#include "test_support.h"
#include "web_auth.h"

namespace {

// ---- validateWebPassword

static void test_empty_password_means_none_and_is_ok(void) {
  ASSERT_ENUM_EQ(WebPasswordCheck::Ok, validateWebPassword(""));
}

static void test_length_bounds(void) {
  ASSERT_ENUM_EQ(WebPasswordCheck::TooShort, validateWebPassword("1234567"));
  ASSERT_ENUM_EQ(WebPasswordCheck::Ok, validateWebPassword("12345678"));
  ASSERT_ENUM_EQ(WebPasswordCheck::Ok, validateWebPassword(std::string(64, 'a')));
  ASSERT_ENUM_EQ(WebPasswordCheck::TooLong, validateWebPassword(std::string(65, 'a')));
}

static void test_printable_ascii_only(void) {
  ASSERT_ENUM_EQ(WebPasswordCheck::Ok, validateWebPassword("with space ~!"));
  ASSERT_ENUM_EQ(WebPasswordCheck::BadChar, validateWebPassword("za\xC5\xBC\xC3\xB3\xC5\x82\x63"));  // "zażółc"
  ASSERT_ENUM_EQ(WebPasswordCheck::BadChar, validateWebPassword("tab\there1"));
  ASSERT_ENUM_EQ(WebPasswordCheck::BadChar, validateWebPassword("abc\x7F" "defgh"));
}

// ---- decidePasswordChange

static void test_both_fields_empty_leaves_password_alone(void) {
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Unchanged, decidePasswordChange("", "", false).outcome);
}

static void test_matching_valid_password_is_set(void) {
  PasswordChange c = decidePasswordChange("hunter22", "hunter22", false);
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Set, c.outcome);
  TEST_ASSERT_EQUAL_STRING("hunter22", c.value.c_str());
}

static void test_mismatch_and_invalid_save_nothing(void) {
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Mismatch, decidePasswordChange("hunter22", "hunter23", false).outcome);
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Mismatch, decidePasswordChange("hunter22", "", false).outcome);
  PasswordChange c = decidePasswordChange("short", "short", false);
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Invalid, c.outcome);
  ASSERT_ENUM_EQ(WebPasswordCheck::TooShort, c.check);
  TEST_ASSERT_TRUE(c.value.empty());
}

static void test_clear_wins_over_typed_fields(void) {
  ASSERT_ENUM_EQ(PasswordChange::Outcome::Cleared, decidePasswordChange("hunter22", "nope", true).outcome);
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
  ASSERT_ENUM_EQ(AuthDecision::Allow, o.decision);
  TEST_ASSERT_FALSE(o.countFailure);
}

static void test_first_request_without_credentials_is_challenged_not_counted(void) {
  AuthOutcome o = decideAuth(true, false, false, false);
  ASSERT_ENUM_EQ(AuthDecision::Challenge, o.decision);
  TEST_ASSERT_FALSE(o.countFailure);
}

static void test_wrong_password_is_challenged_and_counted(void) {
  AuthOutcome o = decideAuth(true, false, true, false);
  ASSERT_ENUM_EQ(AuthDecision::Challenge, o.decision);
  TEST_ASSERT_TRUE(o.countFailure);
}

static void test_right_password_is_allowed(void) {
  AuthOutcome o = decideAuth(true, false, true, true);
  ASSERT_ENUM_EQ(AuthDecision::Allow, o.decision);
  TEST_ASSERT_TRUE(o.countSuccess);
}

static void test_locked_refuses_even_the_right_password(void) {
  ASSERT_ENUM_EQ(AuthDecision::Locked, decideAuth(true, true, true, true).decision);
}

// ---- reset gesture

static void test_reset_hold(void) {
  ASSERT_ENUM_EQ(ResetHoldState::Holding, resetHoldState(0, 0));
  ASSERT_ENUM_EQ(ResetHoldState::Holding, resetHoldState(0, 4999));
  ASSERT_ENUM_EQ(ResetHoldState::Done, resetHoldState(0, 5000));
  // A dropped sample or two doesn't end the hold, a real release does.
  ASSERT_ENUM_EQ(ResetHoldState::Holding, resetHoldState(300, 2000));
  ASSERT_ENUM_EQ(ResetHoldState::Released, resetHoldState(301, 2000));
  ASSERT_ENUM_EQ(ResetHoldState::Released, resetHoldState(1000, 9000));
}

// ---- checkRequestOrigin / decideUploadStart / log line / headers (spec 10)

struct OriginExample {
  const char *host;
  const char *origin;
  const char *referer;
  OriginCheck want;
};

static void assertOriginExamples(const OriginExample *ex, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const std::string what = std::string("Host=") + ex[i].host + " Origin=" + ex[i].origin +
                             " Referer=" + ex[i].referer;
    const OriginCheck got = checkRequestOrigin(ex[i].origin, ex[i].referer, ex[i].host);
    if (got != ex[i].want) TEST_MESSAGE(what.c_str());  // which example, only when it fails
    ASSERT_ENUM_EQ(ex[i].want, got);
  }
}

// Origin z tym samym hostem co Host żądania to to samo pochodzenie
static void test_origin_with_same_host_is_same_origin(void) {
  const OriginExample ex[] = {
      {"cyd-sky.local", "http://cyd-sky.local", "", OriginCheck::SameOrigin},
      {"cyd-sky.local", "http://cyd-sky.local:80", "", OriginCheck::SameOrigin},
      {"cyd-sky.local:80", "http://cyd-sky.local", "", OriginCheck::SameOrigin},
      {"192.168.1.50", "http://192.168.1.50", "", OriginCheck::SameOrigin},
      {"cyd-sky.local", "http://CYD-Sky.Local", "", OriginCheck::SameOrigin},
      {"CYD-SKY.LOCAL", "http://cyd-sky.local", "", OriginCheck::SameOrigin},
  };
  assertOriginExamples(ex, sizeof(ex) / sizeof(ex[0]));
}

// Origin z innego hosta, innego schematu albo „null” to inne pochodzenie
static void test_origin_from_elsewhere_is_cross_origin(void) {
  const OriginExample ex[] = {
      {"cyd-sky.local", "http://evil.example", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "http://192.168.1.50", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "https://cyd-sky.local", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "null", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "http://cyd-sky.local.evil.com", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "http://cyd-sky.local:8080", "", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "cyd-sky.local", "", OriginCheck::CrossOrigin},
  };
  assertOriginExamples(ex, sizeof(ex) / sizeof(ex[0]));
}

// Bez Origin decyduje host z Referer
static void test_referer_alone_decides_when_origin_missing(void) {
  const OriginExample ex[] = {
      {"cyd-sky.local", "", "http://cyd-sky.local/?tab=general", OriginCheck::SameOrigin},
      {"cyd-sky.local", "", "http://cyd-sky.local:80/", OriginCheck::SameOrigin},
      {"cyd-sky.local", "", "http://evil.example/attack.html", OriginCheck::CrossOrigin},
      {"cyd-sky.local", "", "http://cyd-sky.local.evil.com/x?h=y", OriginCheck::CrossOrigin},
  };
  assertOriginExamples(ex, sizeof(ex) / sizeof(ex[0]));
}

// Origin ma pierwszeństwo przed Referer
static void test_origin_wins_over_referer(void) {
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://evil.example", "http://cyd-sky.local/", "cyd-sky.local"));
}

// Brak Origin i Referer (curl, wgrywanie OTA z PlatformIO) jest dozwolony
static void test_no_origin_and_no_referer_is_allowed(void) {
  const OriginCheck o = checkRequestOrigin("", "", "cyd-sky.local");
  ASSERT_ENUM_EQ(OriginCheck::NoHeaders, o);
  ASSERT_ENUM_EQ(UploadStart::Accept, decideUploadStart(o, true));
}

// Origin bez nagłówka Host nie przechodzi
static void test_origin_present_but_no_host_is_cross_origin(void) {
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin, checkRequestOrigin("http://cyd-sky.local", "", ""));
}

// Upload z innej strony jest odrzucany, zanim cokolwiek zostanie zapisane — nawet z dobrym hasłem
static void test_cross_origin_upload_refused_before_credentials(void) {
  const OriginCheck cross = checkRequestOrigin("http://evil.example", "", "cyd-sky.local");
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin, cross);
  ASSERT_ENUM_EQ(UploadStart::RefuseOrigin, decideUploadStart(cross, /*credentialsOk=*/true));
  ASSERT_ENUM_EQ(UploadStart::RefuseOrigin, decideUploadStart(OriginCheck::CrossOrigin, false));
  ASSERT_ENUM_EQ(UploadStart::RefuseAuth, decideUploadStart(OriginCheck::SameOrigin, false));
  ASSERT_ENUM_EQ(UploadStart::Accept, decideUploadStart(OriginCheck::SameOrigin, true));
  ASSERT_ENUM_EQ(UploadStart::Accept, decideUploadStart(OriginCheck::NoHeaders, true));
  ASSERT_ENUM_EQ(UploadStart::RefuseAuth, decideUploadStart(OriginCheck::NoHeaders, false));
}

// Odrzucone żądanie zostawia jedną linię w logu
static void test_cross_origin_log_line_names_path_and_origin(void) {
  ASSERT_STR_EQ("[auth] cross-origin POST /save from http://evil.example refused",
                crossOriginLogLine("POST", "/save", "http://evil.example", ""));
  ASSERT_STR_EQ("[auth] cross-origin POST /update from http://evil.example/attack.html refused",
                crossOriginLogLine("POST", "/update", "", "http://evil.example/attack.html"));
  // Origin wins when both are there.
  ASSERT_STR_EQ("[auth] cross-origin POST /lang from http://evil.example refused",
                crossOriginLogLine("POST", "/lang", "http://evil.example", "http://cyd-sky.local/"));
}

// Odpowiedzi panelu zabraniają osadzania i nie otwierają CORS
static void test_security_headers_deny_framing_and_allow_no_cors(void) {
  std::vector<std::string> lines;
  for (size_t i = 0; i < kSecurityHeaderCount; ++i) {
    lines.push_back(std::string(kSecurityHeaders[i].name) + ": " + kSecurityHeaders[i].value);
    ASSERT_NOT_CONTAINS(std::string(kSecurityHeaders[i].name), "Access-Control-");
  }
  ASSERT_CONTAINS(lines, "X-Frame-Options: DENY");
  ASSERT_CONTAINS(lines, "Content-Security-Policy: frame-ancestors 'none'");
}

// -- edge cases (no scenario)

static void test_origin_empty_string_treated_as_missing(void) {
  // WebServer::header() returns "" for an absent header: "" Origin -> Referer decides.
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("", "http://cyd-sky.local/", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("", "http://evil.example/", "cyd-sky.local"));
}

static void test_referer_with_fragment_and_no_path(void) {
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("", "http://cyd-sky.local#x", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("", "http://cyd-sky.local", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("", "http://cyd-sky.local?tab=firmware", "cyd-sky.local"));
}

static void test_host_with_other_port_must_match_exactly(void) {
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("http://cyd-sky.local:8080", "", "cyd-sky.local:8080"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://cyd-sky.local", "", "cyd-sky.local:8080"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://cyd-sky.local:8081", "", "cyd-sky.local:8080"));
}

static void test_userinfo_in_origin_is_cross(void) {
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://cyd-sky.local@evil.example", "", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("", "http://cyd-sky.local@evil.example/x", "cyd-sky.local"));
  // A prefix of the host is not the host either.
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://cyd-sky.loca", "", "cyd-sky.local"));
}

static void test_origin_is_scheme_and_host_only(void) {
  // A real Origin never carries a path; one that does isn't trusted. A
  // Referer may (and the scheme is case-insensitive, as in any URL).
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin,
                 checkRequestOrigin("http://cyd-sky.local/", "", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::SameOrigin,
                 checkRequestOrigin("HTTP://cyd-sky.local", "", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin, checkRequestOrigin("http://", "", "cyd-sky.local"));
  ASSERT_ENUM_EQ(OriginCheck::CrossOrigin, checkRequestOrigin("", "http:///x", "cyd-sky.local"));
}

static void test_log_line_without_any_header(void) {
  ASSERT_STR_EQ("[auth] cross-origin POST /save from (none) refused",
                crossOriginLogLine("POST", "/save", "", ""));
}

}  // namespace

void run_web_auth_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
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
  RUN_TEST(test_origin_with_same_host_is_same_origin);
  RUN_TEST(test_origin_from_elsewhere_is_cross_origin);
  RUN_TEST(test_referer_alone_decides_when_origin_missing);
  RUN_TEST(test_origin_wins_over_referer);
  RUN_TEST(test_no_origin_and_no_referer_is_allowed);
  RUN_TEST(test_origin_present_but_no_host_is_cross_origin);
  RUN_TEST(test_cross_origin_upload_refused_before_credentials);
  RUN_TEST(test_cross_origin_log_line_names_path_and_origin);
  RUN_TEST(test_security_headers_deny_framing_and_allow_no_cors);
  RUN_TEST(test_origin_empty_string_treated_as_missing);
  RUN_TEST(test_referer_with_fragment_and_no_path);
  RUN_TEST(test_host_with_other_port_must_match_exactly);
  RUN_TEST(test_userinfo_in_origin_is_cross);
  RUN_TEST(test_origin_is_scheme_and_host_only);
  RUN_TEST(test_log_line_without_any_header);
}
