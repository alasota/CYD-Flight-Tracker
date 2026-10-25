// web_i18n: web page string table. CLAUDE.md "Language — Polish / English" (web_i18n).
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "web_i18n.h"

namespace {

static void test_every_string_has_both_translations(void) {
  for (int i = 0; i < kWebStrCount; ++i) {
    const WebStr id = static_cast<WebStr>(i);
    TEST_ASSERT_TRUE(std::strlen(webTr(id, Language::PL)) > 0);
    TEST_ASSERT_TRUE(std::strlen(webTr(id, Language::EN)) > 0);
  }
}

static void test_strings_are_safe_to_insert_raw(void) {
  // Inserted unescaped into HTML and into a JS "..." string table.
  for (int i = 0; i < kWebStrCount; ++i) {
    for (Language lang : {Language::PL, Language::EN}) {
      const char *s = webTr(static_cast<WebStr>(i), lang);
      TEST_ASSERT_NULL_MESSAGE(std::strpbrk(s, "\"<>\\&"), s);
    }
  }
}

static void test_languages_differ_where_expected(void) {
  TEST_ASSERT_EQUAL_STRING("Lokalizacja", webTr(WebStr::TabLocation, Language::PL));
  TEST_ASSERT_EQUAL_STRING("Location", webTr(WebStr::TabLocation, Language::EN));
  TEST_ASSERT_EQUAL_STRING("Zapisz", webTr(WebStr::Save, Language::PL));
  TEST_ASSERT_EQUAL_STRING("Save", webTr(WebStr::Save, Language::EN));
}

static void test_out_of_range_id_is_empty(void) {
  TEST_ASSERT_EQUAL_STRING("", webTr(WebStr::Count, Language::PL));
  TEST_ASSERT_EQUAL_STRING("", webTr(static_cast<WebStr>(9999), Language::EN));
}

}  // namespace

void run_web_i18n_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_every_string_has_both_translations);
  RUN_TEST(test_strings_are_safe_to_insert_raw);
  RUN_TEST(test_languages_differ_where_expected);
  RUN_TEST(test_out_of_range_id_is_empty);
}
