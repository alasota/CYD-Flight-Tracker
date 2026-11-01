// font_set: the characters the display fonts carry. CLAUDE.md "Language — Polish / English".
#include <unity.h>

#include "display/core/text/font_set.h"
#include "test_support.h"

namespace {

static void test_font_safe_accepts_ascii_and_polish_only(void) {
  TEST_ASSERT_TRUE(isFontSafeText("ZBLI\xC5\xBB" "A SI\xC4\x98"));  // ZBLIŻA SIĘ
  TEST_ASSERT_TRUE(isFontSafeText("Krak\xC3\xB3w"));                // Kraków
  TEST_ASSERT_TRUE(isFontSafeText("plain ASCII 123"));
  TEST_ASSERT_FALSE(isFontSafeText("Z\xC3\xBCrich"));               // ü — not guaranteed
  TEST_ASSERT_FALSE(isFontSafeText("\xE2\x82\xAC"));                // € — not in the fonts
  TEST_ASSERT_FALSE(isFontSafeText("tab\there"));                   // control char
  TEST_ASSERT_FALSE(isFontSafeText("\xC5"));                        // truncated sequence
}

}  // namespace

void run_font_set_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_font_safe_accepts_ascii_and_polish_only);
}
