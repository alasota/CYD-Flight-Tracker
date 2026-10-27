// device_alerts: the rules, the definitions table, texts, badge colours. spec/done/08-device-alerts.md.
#include <unity.h>

#include <set>
#include <string>

#include "admin/app/config_page.h"  // SettingsTab, openSkyDailyCredits()
#include "admin/domain/device_alerts.h"
#include "display/domain/theme/lcars_theme.h"  // THEME_ALERT / THEME_WARNING, isFontSafeText()
#include "test_support.h"

namespace {

constexpr Language kLangs[] = {Language::PL, Language::EN};

// Healthy: an account, credits known, 30 s polls (2880 a day < 4000).
AlertInputs healthyInputs() {
  AlertInputs in;
  in.auth.mode = OpenSkyAuthState::Mode::Account;
  in.quota.state = OpenSkyQuota::State::Available;
  in.quota.remaining = 3000;
  in.radiusDeg = 2.5f;
  in.pollIntervalS = 30;
  in.dailyLimit = openSkyDailyLimit(in.auth);
  in.estimatedDailyCredits = openSkyDailyCredits(in.radiusDeg, in.pollIntervalS, 0);
  return in;
}

AlertSet only(AlertId id) {
  AlertSet s;
  s.set(id);
  return s;
}

// ---- rules ---------------------------------------------------------------------

static void test_healthy_state_has_no_alerts(void) {
  const AlertSet s = collectAlerts(healthyInputs());
  TEST_ASSERT_FALSE(s.any());
  ASSERT_ENUM_EQ(AlertSeverity::None, s.displaySeverity());
}

static void test_login_failed_rule(void) {
  AlertInputs in = healthyInputs();
  in.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
  TEST_ASSERT_TRUE(collectAlerts(in).has(AlertId::OpenSkyLoginFailed));
  in.auth.mode = OpenSkyAuthState::Mode::NoCredentials;  // anonymous on purpose: nothing to fix
  TEST_ASSERT_FALSE(collectAlerts(in).has(AlertId::OpenSkyLoginFailed));
  in.auth.mode = OpenSkyAuthState::Mode::Account;
  TEST_ASSERT_FALSE(collectAlerts(in).has(AlertId::OpenSkyLoginFailed));
}

static void test_quota_exhausted_rule(void) {
  AlertInputs in = healthyInputs();
  in.quota.state = OpenSkyQuota::State::Exhausted;
  TEST_ASSERT_TRUE(collectAlerts(in).has(AlertId::OpenSkyQuotaExhausted));
  in.quota.state = OpenSkyQuota::State::Unknown;  // the pool came back
  TEST_ASSERT_FALSE(collectAlerts(in).has(AlertId::OpenSkyQuotaExhausted));
}

static void test_budget_rule(void) {
  AlertInputs in = healthyInputs();
  in.pollIntervalS = 15;  // 5760 a day > 4000
  in.estimatedDailyCredits = openSkyDailyCredits(in.radiusDeg, in.pollIntervalS, 0);
  TEST_ASSERT_TRUE(collectAlerts(in).has(AlertId::OpenSkyBudgetOverLimit));
  in.estimatedDailyCredits = in.dailyLimit;  // exactly the limit fits
  TEST_ASSERT_FALSE(collectAlerts(in).has(AlertId::OpenSkyBudgetOverLimit));
  // Anonymous: 30 s (2880) is over 400.
  in = healthyInputs();
  in.auth.mode = OpenSkyAuthState::Mode::NoCredentials;
  in.dailyLimit = openSkyDailyLimit(in.auth);
  TEST_ASSERT_TRUE(collectAlerts(in).has(AlertId::OpenSkyBudgetOverLimit));
}

static void test_login_failed_and_budget_together(void) {
  AlertInputs in = healthyInputs();
  in.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
  in.dailyLimit = openSkyDailyLimit(in.auth);  // 400 in LoginFailed
  const AlertSet s = collectAlerts(in);
  TEST_ASSERT_TRUE(s.has(AlertId::OpenSkyLoginFailed));
  TEST_ASSERT_TRUE(s.has(AlertId::OpenSkyBudgetOverLimit));
  TEST_ASSERT_FALSE(s.has(AlertId::OpenSkyQuotaExhausted));
}

static void test_display_severity(void) {
  ASSERT_ENUM_EQ(AlertSeverity::Warning, only(AlertId::OpenSkyBudgetOverLimit).displaySeverity());
  ASSERT_ENUM_EQ(AlertSeverity::Error, only(AlertId::OpenSkyLoginFailed).displaySeverity());
  AlertSet both = only(AlertId::OpenSkyBudgetOverLimit);
  both.set(AlertId::OpenSkyQuotaExhausted);
  ASSERT_ENUM_EQ(AlertSeverity::Error, both.displaySeverity());
  ASSERT_ENUM_EQ(AlertSeverity::None, AlertSet{}.displaySeverity());
}

static void test_details_carry_the_numbers(void) {
  AlertInputs in = healthyInputs();
  in.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
  in.auth.lastHttpCode = 401;
  in.quota.state = OpenSkyQuota::State::Exhausted;
  in.quota.reset_in_s = 3600;
  in.quota.observed_ms = 1234;
  in.pollIntervalS = 15;
  in.dailyLimit = 400;
  in.estimatedDailyCredits = 5760;
  const AlertDetails d = collectAlertDetails(in);
  TEST_ASSERT_EQUAL_INT(401, d.loginHttpCode);
  TEST_ASSERT_EQUAL_UINT32(3600, d.quotaResetInS);
  TEST_ASSERT_EQUAL_UINT32(1234, d.quotaObservedMs);
  TEST_ASSERT_EQUAL_UINT32(5760, d.estimatedDailyCredits);
  TEST_ASSERT_EQUAL_UINT32(400, d.dailyLimit);
  TEST_ASSERT_EQUAL_UINT32(15, d.pollIntervalS);
  TEST_ASSERT_EQUAL_UINT32(216, d.minPollIntervalS);
  // No login failure / no 429: no stale numbers either.
  const AlertDetails h = collectAlertDetails(healthyInputs());
  TEST_ASSERT_EQUAL_INT(0, h.loginHttpCode);
  TEST_ASSERT_EQUAL_UINT32(0, h.quotaResetInS);
}

// ---- definitions ---------------------------------------------------------------

static void test_definitions_complete_in_enum_order_unique_codes(void) {
  std::set<std::string> codes;
  for (int i = 0; i < kAlertCount; ++i) {
    const AlertDef &d = alertDef(static_cast<AlertId>(i));
    ASSERT_ENUM_EQ(static_cast<AlertId>(i), d.id);
    TEST_ASSERT_NOT_NULL(d.code);
    TEST_ASSERT_TRUE(codes.insert(d.code).second);
    ASSERT_ENUM_NE(AlertSeverity::None, d.severity);
    for (Language lang : kLangs) {
      TEST_ASSERT_TRUE(isFontSafeText(tr(d.displayTitle, lang)));
      TEST_ASSERT_TRUE(*webTr(d.webTitle, lang) != '\0');
      TEST_ASSERT_TRUE(*webTr(d.webAction, lang) != '\0');
    }
  }
  ASSERT_ENUM_EQ(AlertSeverity::Error, alertDef(AlertId::OpenSkyLoginFailed).severity);
  ASSERT_ENUM_EQ(AlertSeverity::Error, alertDef(AlertId::OpenSkyQuotaExhausted).severity);
  ASSERT_ENUM_EQ(AlertSeverity::Warning, alertDef(AlertId::OpenSkyBudgetOverLimit).severity);
  for (int i = 0; i < kAlertCount; ++i) {
    ASSERT_ENUM_EQ(SettingsTab::OpenSky, alertDef(static_cast<AlertId>(i)).tab);
    TEST_ASSERT_TRUE(alertDef(static_cast<AlertId>(i)).onDisplay);
  }
}

static void test_badge_colours(void) {
  TEST_ASSERT_EQUAL_HEX16(THEME_ALERT, alertBadgeColor(AlertSeverity::Error));
  TEST_ASSERT_EQUAL_HEX16(THEME_WARNING, alertBadgeColor(AlertSeverity::Warning));
  TEST_ASSERT_EQUAL_HEX16(LCARS_BLACK, alertBadgeColor(AlertSeverity::None));
  TEST_ASSERT_TRUE(THEME_WARNING != THEME_CHROME_ACCENT);  // not the active tab's peach
  ASSERT_STR_EQ("err", alertWebClass(AlertSeverity::Error));
  ASSERT_STR_EQ("warn", alertWebClass(AlertSeverity::Warning));
  ASSERT_STR_EQ("", alertWebClass(AlertSeverity::None));
}

static void test_order_errors_before_warnings(void) {
  AlertSet s;
  s.set(AlertId::OpenSkyBudgetOverLimit);
  s.set(AlertId::OpenSkyQuotaExhausted);
  s.set(AlertId::OpenSkyLoginFailed);
  const std::vector<AlertId> v = alertsInOrder(s, true);
  TEST_ASSERT_EQUAL_size_t(3, v.size());
  ASSERT_ENUM_EQ(AlertId::OpenSkyLoginFailed, v[0]);
  ASSERT_ENUM_EQ(AlertId::OpenSkyQuotaExhausted, v[1]);
  ASSERT_ENUM_EQ(AlertId::OpenSkyBudgetOverLimit, v[2]);
  TEST_ASSERT_TRUE(alertsInOrder(AlertSet{}, false).empty());
  const std::vector<AlertId> w = alertsInOrder(only(AlertId::OpenSkyBudgetOverLimit), false);
  TEST_ASSERT_EQUAL_size_t(1, w.size());
}

// ---- texts ---------------------------------------------------------------------

AlertDetails demoDetails() { return alertDemoState().details; }

static void test_display_bodies_both_languages_font_safe(void) {
  const AlertDetails d = demoDetails();
  for (Language lang : kLangs) {
    for (int i = 0; i < kAlertCount; ++i) {
      const std::string body = alertDisplayBody(static_cast<AlertId>(i), d, 0, lang);
      TEST_ASSERT_FALSE(body.empty());
      TEST_ASSERT_TRUE(isFontSafeText(body));
      ASSERT_NOT_CONTAINS(body, "{}");
    }
  }
  ASSERT_STR_EQ("Działa anonimowo: 400 kredytów/dzień",
                alertDisplayBody(AlertId::OpenSkyLoginFailed, d, 0, Language::PL));
  ASSERT_STR_EQ("5760 of 400 credits a day",
                alertDisplayBody(AlertId::OpenSkyBudgetOverLimit, d, 0, Language::EN));
}

static void test_display_body_quota_counts_down(void) {
  AlertDetails d;
  d.quotaResetInS = 25012;
  d.quotaObservedMs = 1000;
  ASSERT_STR_EQ("Renews in about 6 h 57 min",
                alertDisplayBody(AlertId::OpenSkyQuotaExhausted, d, 1000, Language::EN));
  ASSERT_STR_EQ("Odnowienie za ok. 6 godz. 57 min",
                alertDisplayBody(AlertId::OpenSkyQuotaExhausted, d, 1000, Language::PL));
  ASSERT_STR_EQ("Renews in about 6 h 56 min",
                alertDisplayBody(AlertId::OpenSkyQuotaExhausted, d, 1000 + 60000, Language::EN));
  // Past due, and OpenSky never said.
  ASSERT_STR_EQ("Should renew any moment",
                alertDisplayBody(AlertId::OpenSkyQuotaExhausted, d, 1000 + 25012000, Language::EN));
  d.quotaResetInS = 0;
  ASSERT_STR_EQ("OpenSky didn't say when it renews",
                alertDisplayBody(AlertId::OpenSkyQuotaExhausted, d, 0, Language::EN));
}

static void test_web_why_both_languages(void) {
  const AlertDetails d = demoDetails();
  for (Language lang : kLangs) {
    for (int i = 0; i < kAlertCount; ++i) {
      const std::string why = alertWebWhy(static_cast<AlertId>(i), d, 0, lang);
      TEST_ASSERT_FALSE(why.empty());
      ASSERT_NOT_CONTAINS(why, "{}");
    }
  }
  const std::string login = alertWebWhy(AlertId::OpenSkyLoginFailed, d, 0, Language::PL);
  ASSERT_CONTAINS(login, "HTTP 401");
  ASSERT_CONTAINS(login, "400 kredytów dziennie zamiast 4000");
  const std::string quota = alertWebWhy(AlertId::OpenSkyQuotaExhausted, d, 0, Language::EN);
  ASSERT_CONTAINS(quota, "429");
  ASSERT_CONTAINS(quota, "6 h 57 min");
  const std::string budget = alertWebWhy(AlertId::OpenSkyBudgetOverLimit, d, 0, Language::EN);
  ASSERT_CONTAINS(budget, "Every 15 s");
  ASSERT_CONTAINS(budget, "5760");
  ASSERT_CONTAINS(budget, "the limit is 400");
  ASSERT_CONTAINS(budget, "216 s");
}

static void test_change_log(void) {
  ASSERT_STR_EQ("", alertChangeLog(AlertSet{}, AlertSet{}));
  ASSERT_STR_EQ("[alert] + opensky_login_failed",
                alertChangeLog(AlertSet{}, only(AlertId::OpenSkyLoginFailed)));
  ASSERT_STR_EQ("[alert] - opensky_budget_over_limit",
                alertChangeLog(only(AlertId::OpenSkyBudgetOverLimit), AlertSet{}));
  ASSERT_STR_EQ("[alert] - opensky_login_failed\n[alert] + opensky_quota_exhausted",
                alertChangeLog(only(AlertId::OpenSkyLoginFailed),
                               only(AlertId::OpenSkyQuotaExhausted)));
}

static void test_min_poll_interval_for_limit(void) {
  TEST_ASSERT_EQUAL_UINT32(22, minPollIntervalForLimit(2.5f, 4000, 0));
  TEST_ASSERT_EQUAL_UINT32(216, minPollIntervalForLimit(2.5f, 400, 0));
  // It really is the shortest: one second less goes over.
  TEST_ASSERT_TRUE(openSkyDailyCredits(2.5f, 22, 0) <= 4000);
  TEST_ASSERT_TRUE(openSkyDailyCredits(2.5f, 21, 0) > 4000);
  // A night pause (23:00-06:30 = 450 min) allows a shorter interval.
  const uint32_t night = minPollIntervalForLimit(2.5f, 4000, 450);
  TEST_ASSERT_TRUE(night < 22);
  TEST_ASSERT_TRUE(openSkyDailyCredits(2.5f, night, 450) <= 4000);
  TEST_ASSERT_TRUE(openSkyDailyCredits(2.5f, night - 1, 450) > 4000);
  // 2 credits a request (radius 4) doubles it.
  TEST_ASSERT_EQUAL_UINT32(44, minPollIntervalForLimit(4.0f, 4000, 0));
  TEST_ASSERT_EQUAL_UINT32(1, minPollIntervalForLimit(2.5f, 4000, 1440));
  TEST_ASSERT_EQUAL_UINT32(0, minPollIntervalForLimit(11.0f, 3, 0));  // 4 a request: none fits
}

static void test_fill_template(void) {
  ASSERT_STR_EQ("a 1 b 2", fillTemplate("a {} b {}", {"1", "2"}));
  ASSERT_STR_EQ("a  b", fillTemplate("a {} b", {}));
  ASSERT_STR_EQ("no args", fillTemplate("no args", {"x"}));
  ASSERT_STR_EQ("{x}", fillTemplate("{x}", {"1"}));
}

static void test_demo_has_all_three(void) {
  const AlertDemo demo = alertDemoState();
  for (int i = 0; i < kAlertCount; ++i) TEST_ASSERT_TRUE(demo.alerts.has(static_cast<AlertId>(i)));
}

}  // namespace

void run_device_alerts_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_healthy_state_has_no_alerts);
  RUN_TEST(test_login_failed_rule);
  RUN_TEST(test_quota_exhausted_rule);
  RUN_TEST(test_budget_rule);
  RUN_TEST(test_login_failed_and_budget_together);
  RUN_TEST(test_display_severity);
  RUN_TEST(test_details_carry_the_numbers);
  RUN_TEST(test_definitions_complete_in_enum_order_unique_codes);
  RUN_TEST(test_badge_colours);
  RUN_TEST(test_order_errors_before_warnings);
  RUN_TEST(test_display_bodies_both_languages_font_safe);
  RUN_TEST(test_display_body_quota_counts_down);
  RUN_TEST(test_web_why_both_languages);
  RUN_TEST(test_change_log);
  RUN_TEST(test_min_poll_interval_for_limit);
  RUN_TEST(test_fill_template);
  RUN_TEST(test_demo_has_all_three);
}
