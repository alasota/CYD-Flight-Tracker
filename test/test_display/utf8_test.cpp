// utf8: character count and cut points in UTF-8 text. CLAUDE.md "Language — Polish / English".
#include <unity.h>

#include "display/core/text/utf8.h"
#include "test_support.h"

namespace {

static void test_utf8_length_counts_characters_not_bytes(void) {
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("SKAD"));
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("SK\xC4\x84" "D"));        // SKĄD: 5 bytes, 4 chars
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("\xC5\x81\xC3\xB3" "d\xC5\xBA"));  // Łódź
  TEST_ASSERT_EQUAL_size_t(0, utf8Length(""));
}

static void test_char_boundary_on_ascii_and_continuation_bytes(void) {
  const std::string s = "A\xC5\x81" "b";  // "AŁb": A, Ł (2 bytes), b
  TEST_ASSERT_TRUE(utf8IsCharBoundary(s, 0));         // start, before ASCII
  TEST_ASSERT_TRUE(utf8IsCharBoundary(s, 1));         // before the lead byte of Ł
  TEST_ASSERT_FALSE(utf8IsCharBoundary(s, 2));        // continuation byte of Ł
  TEST_ASSERT_TRUE(utf8IsCharBoundary(s, 3));         // before "b"
  TEST_ASSERT_TRUE(utf8IsCharBoundary(s, s.size()));  // the end is a boundary
  TEST_ASSERT_TRUE(utf8IsCharBoundary("", 0));
}

}  // namespace

void run_utf8_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_utf8_length_counts_characters_not_bytes);
  RUN_TEST(test_char_boundary_on_ascii_and_continuation_bytes);
}
