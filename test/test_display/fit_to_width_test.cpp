// fit_to_width: cutting text to a pixel width, UTF-8 safe. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "display/core/text/fit_to_width.h"
#include "display/core/text/font_set.h"  // isFontSafeText()
#include "test_support.h"

namespace {

// ---- fitToWidth -------------------------------------------------------------
//
// Measured with fake fixed-pitch fonts so expectations are hand-checkable:
// kOnePx (1px per char — width == character count) and kSixPx (Font0's
// 6px advance).

static int onePx(const std::string &s) { return static_cast<int>(s.size()); }
static int sixPx(const std::string &s) { return static_cast<int>(s.size()) * 6; }
static const TextMeasure kOnePx = onePx;
static const TextMeasure kSixPx = sixPx;

static void test_fit_short_string_unchanged(void) {
  TEST_ASSERT_EQUAL_STRING("Ryanair", fitToWidth("Ryanair", 15, kOnePx).c_str());
}

static void test_fit_exact_width_unchanged(void) {
  // width == maxPx is a fit, not an overflow.
  TEST_ASSERT_EQUAL_STRING("123456789012345", fitToWidth("123456789012345", 15, kOnePx).c_str());
}

static void test_fit_long_string_gets_ellipsis(void) {
  std::string out = fitToWidth("LOT Polish Airlines", 13, kOnePx);
  TEST_ASSERT_EQUAL_STRING("LOT Polish...", out.c_str());
}

static void test_fit_trims_trailing_space_before_ellipsis(void) {
  // Head would end "LOT " — the space is dropped so it's not "LOT ...".
  TEST_ASSERT_EQUAL_STRING("LOT...", fitToWidth("LOT Airlines", 7, kOnePx).c_str());
}

static void test_fit_result_never_exceeds_width(void) {
  const std::string name = "Scandinavian Airlines System Denmark Norway Sweden";
  for (int px = 0; px <= 130; ++px) {
    TEST_ASSERT_TRUE(sixPx(fitToWidth(name, px, kSixPx)) <= px);
  }
}

static void test_fit_too_tight_for_ellipsis_hard_cuts(void) {
  // < 4 chars of room can't fit "X..." — plain cut, no ellipsis.
  TEST_ASSERT_EQUAL_STRING("Lu", fitToWidth("Lufthansa", 2, kOnePx).c_str());
  TEST_ASSERT_EQUAL_STRING("Luf", fitToWidth("Lufthansa", 3, kOnePx).c_str());
}

static void test_fit_nonpositive_width_is_empty(void) {
  TEST_ASSERT_EQUAL_STRING("", fitToWidth("Lufthansa", 0, kOnePx).c_str());
  TEST_ASSERT_EQUAL_STRING("", fitToWidth("Lufthansa", -5, kOnePx).c_str());
}

static void test_fit_uses_pixel_width_not_char_count(void) {
  // 106px at 6px/char = 17 chars of room: "Wizz Air Hungary Ltd" (20) -> 14 + "...".
  std::string out = fitToWidth("Wizz Air Hungary Ltd", 106, kSixPx);
  TEST_ASSERT_EQUAL_STRING("Wizz Air Hunga...", out.c_str());
  TEST_ASSERT_TRUE(sixPx(out) <= 106);
}

static void test_fit_ascii_ellipsis_only(void) {
  // Must stay within the bitmap fonts' 0x20-0x7E range (no "…").
  std::string out = fitToWidth("Some Very Long Airline Name Ltd", 12, kOnePx);
  for (char c : out) {
    TEST_ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 &&
                     static_cast<unsigned char>(c) <= 0x7E);
  }
}

static void test_fit_never_splits_a_utf8_character(void) {
  // "Łódź Łódź" with a 1px-per-byte measure: every cut must land on a
  // character boundary, i.e. the result is still font-safe UTF-8.
  const std::string s = "\xC5\x81\xC3\xB3" "d\xC5\xBA \xC5\x81\xC3\xB3" "d\xC5\xBA";
  for (int px = 0; px <= static_cast<int>(s.size()); ++px) {
    TEST_ASSERT_TRUE(isFontSafeText(fitToWidth(s, px, kOnePx)));
  }
}

}  // namespace

void run_fit_to_width_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_fit_short_string_unchanged);
  RUN_TEST(test_fit_exact_width_unchanged);
  RUN_TEST(test_fit_long_string_gets_ellipsis);
  RUN_TEST(test_fit_trims_trailing_space_before_ellipsis);
  RUN_TEST(test_fit_result_never_exceeds_width);
  RUN_TEST(test_fit_too_tight_for_ellipsis_hard_cuts);
  RUN_TEST(test_fit_nonpositive_width_is_empty);
  RUN_TEST(test_fit_uses_pixel_width_not_char_count);
  RUN_TEST(test_fit_ascii_ellipsis_only);
  RUN_TEST(test_fit_never_splits_a_utf8_character);
}
