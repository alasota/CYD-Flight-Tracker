// service_screen: OTA progress / password reset screens. CLAUDE.md "Firmware updates (OTA)", spec/01.
#include <unity.h>

#include "i18n.h"
#include "lcars_theme.h"
#include "service_screen.h"
#include "test_support.h"

namespace {

static void test_progress_bar_is_centred_on_screen(void) {
  const Rect r = serviceProgressBarRect(320, 240);
  TEST_ASSERT_EQUAL_INT(320, 2 * r.x + r.w);
  TEST_ASSERT_TRUE(r.y > 0 && r.y + r.h < 240);
}

static void test_progress_fill(void) {
  TEST_ASSERT_EQUAL_INT(0, serviceProgressFillPx(-1, 240));
  TEST_ASSERT_EQUAL_INT(0, serviceProgressFillPx(0, 240));
  TEST_ASSERT_EQUAL_INT(120, serviceProgressFillPx(50, 240));
  TEST_ASSERT_EQUAL_INT(240, serviceProgressFillPx(100, 240));
  TEST_ASSERT_EQUAL_INT(240, serviceProgressFillPx(150, 240));
}

static void test_progress_text(void) {
  TEST_ASSERT_EQUAL_STRING("0%", otaProgressText(0, 1000).c_str());
  TEST_ASSERT_EQUAL_STRING("45%", otaProgressText(450, 1000).c_str());
  TEST_ASSERT_EQUAL_STRING("100%", otaProgressText(2000, 1000).c_str());
  TEST_ASSERT_EQUAL_STRING("812 KB", otaProgressText(812 * 1024 + 5, 0).c_str());
}

static void test_version_line(void) {
  TEST_ASSERT_EQUAL_STRING("v1.2.0 \xE2\x86\x92 v1.3.0", otaVersionLine("v1.2.0", "v1.3.0").c_str());
  TEST_ASSERT_EQUAL_STRING("v1.3.0", otaVersionLine("", "v1.3.0").c_str());
  TEST_ASSERT_EQUAL_STRING("", otaVersionLine("v1.2.0", "").c_str());
  TEST_ASSERT_TRUE(isFontSafeText(otaVersionLine("v1.2.0", "v1.3.0")));
}

static void test_reset_buttons_fit_and_do_not_overlap(void) {
  const Rect yes = confirmButtonRect(true, 320, 240);
  const Rect no = confirmButtonRect(false, 320, 240);
  TEST_ASSERT_TRUE(yes.x >= 0 && no.x + no.w <= 320);
  TEST_ASSERT_TRUE(yes.x + yes.w < no.x);
  TEST_ASSERT_TRUE(yes.y + yes.h <= 240);
}

static void test_reset_hit_test(void) {
  const Rect yes = confirmButtonRect(true, 320, 240);
  const Rect no = confirmButtonRect(false, 320, 240);
  ASSERT_ENUM_EQ(ResetChoice::Yes, confirmHit(yes.x + 5, yes.y + 5, 320, 240));
  ASSERT_ENUM_EQ(ResetChoice::No, confirmHit(no.x + 5, no.y + 5, 320, 240));
  ASSERT_ENUM_EQ(ResetChoice::None, confirmHit(160, 20, 320, 240));
  ASSERT_ENUM_EQ(ResetChoice::None, confirmHit(yes.x + yes.w + 5, yes.y + 5, 320, 240));
}

static void test_screen_texts_fit_one_line(void) {
  // 16 px efont, 8 px per character, 320 px wide panel.
  for (Str id : {Str::OtaTitle, Str::OtaDontPowerOff, Str::OtaRestarting, Str::OtaFailed,
                 Str::ResetTitle, Str::ResetHold, Str::ResetConfirm, Str::ResetDone,
                 Str::OtaFromGithub, Str::UpdateTitle, Str::UpdateAsk}) {
    for (Language lang : {Language::PL, Language::EN}) {
      TEST_ASSERT_TRUE_MESSAGE(utf8Length(tr(id, lang)) * LCARS_FONT_BODY_CHAR_PX <= 300,
                               tr(id, lang));
    }
  }
}

static void test_update_prompt_hint_fits_in_the_small_font(void) {
  for (Language lang : {Language::PL, Language::EN}) {
    TEST_ASSERT_TRUE(utf8Length(tr(Str::UpdateHint, lang)) * LCARS_FONT_SMALL_CHAR_PX <= 300);
    TEST_ASSERT_TRUE(isFontSafeText(tr(Str::UpdateHint, lang)));
  }
  // The longest demo line ("v1.2.0 → v1.3.0") fits the body font too.
  TEST_ASSERT_TRUE(utf8Length(otaVersionLine("v1.10.10", "v1.10.11-rc.1")) * LCARS_FONT_BODY_CHAR_PX <=
                   300);
}

static void test_prompt_timer_strip_runs_out(void) {
  TEST_ASSERT_EQUAL_INT16(200, promptTimerFillPx(30000, 30000, 200));
  TEST_ASSERT_EQUAL_INT16(200, promptTimerFillPx(40000, 30000, 200));
  TEST_ASSERT_EQUAL_INT16(100, promptTimerFillPx(15000, 30000, 200));
  TEST_ASSERT_EQUAL_INT16(0, promptTimerFillPx(0, 30000, 200));
  TEST_ASSERT_EQUAL_INT16(0, promptTimerFillPx(1000, 0, 200));
}


// ---- the alert message screen (spec 08 §3.5) ----------------------------------

static bool overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static bool onScreen(const Rect &r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= 320 && r.y + r.h <= 240; }

static void test_alert_layout_fits_without_overlap(void) {
  for (int count = 1; count <= 5; ++count) {
    const AlertScreenLayout l = alertScreenLayout(320, 240, count);
    TEST_ASSERT_TRUE(onScreen(l.title));
    TEST_ASSERT_TRUE(onScreen(l.hint));
    TEST_ASSERT_TRUE(onScreen(l.button));
    TEST_ASSERT_TRUE(l.button.h >= kAlertCloseButtonMinH);
    TEST_ASSERT_FALSE(overlaps(l.hint, l.button));
    for (int i = 0; i < kAlertScreenMaxRows; ++i) {
      TEST_ASSERT_TRUE(onScreen(l.entries[i]));
      TEST_ASSERT_FALSE(overlaps(l.entries[i], l.title));
      TEST_ASSERT_FALSE(overlaps(l.entries[i], l.hint));
      TEST_ASSERT_FALSE(overlaps(l.entries[i], l.button));
      if (i > 0) TEST_ASSERT_FALSE(overlaps(l.entries[i - 1], l.entries[i]));
    }
  }
}

static void test_alert_layout_counts_and_more_row(void) {
  TEST_ASSERT_EQUAL_INT(1, alertScreenLayout(320, 240, 1).shown);
  TEST_ASSERT_EQUAL_INT(0, alertScreenLayout(320, 240, 1).more);
  TEST_ASSERT_EQUAL_INT(2, alertScreenLayout(320, 240, 2).shown);
  TEST_ASSERT_EQUAL_INT(3, alertScreenLayout(320, 240, 3).shown);
  TEST_ASSERT_EQUAL_INT(0, alertScreenLayout(320, 240, 3).more);
  const AlertScreenLayout five = alertScreenLayout(320, 240, 5);
  TEST_ASSERT_EQUAL_INT(2, five.shown);
  TEST_ASSERT_EQUAL_INT(3, five.more);  // "+ 3 more" in the third row
  TEST_ASSERT_EQUAL_INT16(five.entries[2].y, five.moreRow.y);
}

static void test_alert_texts_fit(void) {
  // Titles in the body font right of the icon, the rest in the small font.
  for (Language lang : {Language::PL, Language::EN}) {
    for (Str id : {Str::AlertLoginTitle, Str::AlertQuotaTitle, Str::AlertBudgetTitle}) {
      TEST_ASSERT_TRUE(utf8Length(tr(id, lang)) * LCARS_FONT_BODY_CHAR_PX <= 304 - 26);
    }
    TEST_ASSERT_TRUE(utf8Length(tr(Str::AlertDetailsHint, lang)) * LCARS_FONT_SMALL_CHAR_PX <= 304);
    const std::string more = fillTemplate(tr(Str::AlertMore, lang), {"9"});
    TEST_ASSERT_TRUE(utf8Length(more) * LCARS_FONT_SMALL_CHAR_PX <= 304 - 26);
    TEST_ASSERT_TRUE(isFontSafeText(more));
    // "ZAMKNIJ  15 s" inside the 160 px button.
    TEST_ASSERT_TRUE(utf8Length(alertCloseLabel(15, lang)) * LCARS_FONT_BODY_CHAR_PX <= 160 - 16);
  }
  ASSERT_STR_EQ("ZAMKNIJ  15 s", alertCloseLabel(15, Language::PL));
  ASSERT_STR_EQ("CLOSE  1 s", alertCloseLabel(1, Language::EN));
}

static AlertScreenInput at(uint32_t nowMs, bool pressed = false, int16_t x = 0, int16_t y = 0) {
  AlertScreenInput in;
  in.nowMs = nowMs;
  in.pressed = pressed;
  in.x = x;
  in.y = y;
  return in;
}

static void buttonCentre(int16_t *x, int16_t *y) {
  const Rect b = alertScreenLayout(320, 240, 1).button;
  *x = static_cast<int16_t>(b.x + b.w / 2);
  *y = static_cast<int16_t>(b.y + b.h / 2);
}

static void test_alert_screen_closes_on_a_fresh_tap_on_close(void) {
  int16_t bx, by;
  buttonCentre(&bx, &by);
  AlertScreen s = openAlertScreen(1000);
  // The opening finger is still down, and slides onto the button: no close.
  AlertScreenStepResult r = alertScreenStep(s, at(1100, true, bx, by));
  TEST_ASSERT_FALSE(r.close);
  s = r.state;
  r = alertScreenStep(s, at(1200));  // lifted
  TEST_ASSERT_FALSE(r.close);
  s = r.state;
  r = alertScreenStep(s, at(1500, true, bx, by));  // a fresh tap on CLOSE
  TEST_ASSERT_TRUE(r.close);
  TEST_ASSERT_FALSE(r.state.open);
}

static void test_alert_screen_tap_elsewhere_changes_nothing(void) {
  AlertScreen s = openAlertScreen(0);
  s = alertScreenStep(s, at(100)).state;  // lifted
  AlertScreenStepResult r = alertScreenStep(s, at(2000, true, 160, 100));  // on an entry
  TEST_ASSERT_FALSE(r.close);
  TEST_ASSERT_TRUE(r.state.open);
  // ...and doesn't extend the time.
  r = alertScreenStep(r.state, at(kAlertScreenTimeoutMs));
  TEST_ASSERT_TRUE(r.close);
}

static void test_alert_screen_closes_after_exactly_15_s(void) {
  const AlertScreen s = openAlertScreen(5000);
  TEST_ASSERT_FALSE(alertScreenStep(s, at(5000 + 14999)).close);
  TEST_ASSERT_TRUE(alertScreenStep(s, at(5000 + 15000)).close);
}

static void test_alert_screen_closes_when_the_alerts_go(void) {
  const AlertScreen s = openAlertScreen(0);
  AlertScreenInput in = at(3000);
  in.anyDisplayAlerts = false;
  TEST_ASSERT_TRUE(alertScreenStep(s, in).close);
}

static void test_alert_screen_countdown_ticks_each_second(void) {
  AlertScreen s = openAlertScreen(0);
  TEST_ASSERT_EQUAL_UINT32(15, s.secondsShown);
  TEST_ASSERT_EQUAL_UINT32(15, alertScreenSecondsLeft(s, 999));
  TEST_ASSERT_EQUAL_UINT32(14, alertScreenSecondsLeft(s, 1000));
  TEST_ASSERT_EQUAL_UINT32(1, alertScreenSecondsLeft(s, 14999));
  TEST_ASSERT_EQUAL_UINT32(0, alertScreenSecondsLeft(s, 15000));
  AlertScreenStepResult r = alertScreenStep(s, at(500));
  TEST_ASSERT_FALSE(r.redrawTimer);
  r = alertScreenStep(r.state, at(1000));
  TEST_ASSERT_TRUE(r.redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(14, r.state.secondsShown);
  r = alertScreenStep(r.state, at(1500));
  TEST_ASSERT_FALSE(r.redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(1000, alertScreenMsToNextSecond(s, 0));
  TEST_ASSERT_EQUAL_UINT32(500, alertScreenMsToNextSecond(s, 500));
  TEST_ASSERT_EQUAL_UINT32(1, alertScreenMsToNextSecond(s, 999));
}

static void test_alert_screen_survives_millis_wrap(void) {
  const uint32_t start = UINT32_MAX - 2000;
  const AlertScreen s = openAlertScreen(start);
  TEST_ASSERT_FALSE(alertScreenStep(s, at(start + 10000)).close);  // wrapped past 0
  TEST_ASSERT_EQUAL_UINT32(5, alertScreenSecondsLeft(s, start + 10000));
  TEST_ASSERT_TRUE(alertScreenStep(s, at(start + 15000)).close);
}

}  // namespace

void run_service_screen_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_progress_bar_is_centred_on_screen);
  RUN_TEST(test_progress_fill);
  RUN_TEST(test_progress_text);
  RUN_TEST(test_version_line);
  RUN_TEST(test_reset_buttons_fit_and_do_not_overlap);
  RUN_TEST(test_reset_hit_test);
  RUN_TEST(test_screen_texts_fit_one_line);
  RUN_TEST(test_update_prompt_hint_fits_in_the_small_font);
  RUN_TEST(test_prompt_timer_strip_runs_out);
  RUN_TEST(test_alert_layout_fits_without_overlap);
  RUN_TEST(test_alert_layout_counts_and_more_row);
  RUN_TEST(test_alert_texts_fit);
  RUN_TEST(test_alert_screen_closes_on_a_fresh_tap_on_close);
  RUN_TEST(test_alert_screen_tap_elsewhere_changes_nothing);
  RUN_TEST(test_alert_screen_closes_after_exactly_15_s);
  RUN_TEST(test_alert_screen_closes_when_the_alerts_go);
  RUN_TEST(test_alert_screen_countdown_ticks_each_second);
  RUN_TEST(test_alert_screen_survives_millis_wrap);
}
