#include <unity.h>

#include "i18n.h"
#include "lcars_theme.h"
#include "service_screen.h"

void setUp(void) {}
void tearDown(void) {}

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
  TEST_ASSERT_TRUE(confirmHit(yes.x + 5, yes.y + 5, 320, 240) == ResetChoice::Yes);
  TEST_ASSERT_TRUE(confirmHit(no.x + 5, no.y + 5, 320, 240) == ResetChoice::No);
  TEST_ASSERT_TRUE(confirmHit(160, 20, 320, 240) == ResetChoice::None);
  TEST_ASSERT_TRUE(confirmHit(yes.x + yes.w + 5, yes.y + 5, 320, 240) == ResetChoice::None);
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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_progress_bar_is_centred_on_screen);
  RUN_TEST(test_progress_fill);
  RUN_TEST(test_progress_text);
  RUN_TEST(test_version_line);
  RUN_TEST(test_reset_buttons_fit_and_do_not_overlap);
  RUN_TEST(test_reset_hit_test);
  RUN_TEST(test_screen_texts_fit_one_line);
  RUN_TEST(test_update_prompt_hint_fits_in_the_small_font);
  RUN_TEST(test_prompt_timer_strip_runs_out);
  return UNITY_END();
}
