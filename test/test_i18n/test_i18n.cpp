#include <unity.h>

#include <cstring>
#include <string>

#include "i18n.h"

void setUp(void) { i18nSetLanguage(kDefaultLanguage); }
void tearDown(void) {}

static const Language kAllLanguages[] = {Language::PL, Language::EN};

// ---- table completeness ----------------------------------------------------

static void test_every_string_non_empty_in_every_language(void) {
  for (Language lang : kAllLanguages) {
    for (int i = 0; i < kStrCount; ++i) {
      const char *s = tr(static_cast<Str>(i), lang);
      TEST_ASSERT_NOT_NULL(s);
      TEST_ASSERT_TRUE_MESSAGE(std::strlen(s) > 0, "empty translation");
    }
  }
}

static void test_every_string_is_printable_ascii(void) {
  // The built-in bitmap fonts only cover 0x20-0x7E — a Polish diacritic
  // (UTF-8 multi-byte) would render as garbage, so they're ASCII-folded.
  for (Language lang : kAllLanguages) {
    for (int i = 0; i < kStrCount; ++i) {
      for (const char *p = tr(static_cast<Str>(i), lang); *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        TEST_ASSERT_TRUE_MESSAGE(c >= 0x20 && c <= 0x7E, tr(static_cast<Str>(i), lang));
      }
    }
  }
}

static void test_out_of_range_id_does_not_crash(void) {
  TEST_ASSERT_EQUAL_STRING("?", tr(static_cast<Str>(kStrCount), Language::PL));
  TEST_ASSERT_EQUAL_STRING("?", tr(static_cast<Str>(-1), Language::EN));
}

// ---- known translations -----------------------------------------------------

static void test_screen_names(void) {
  TEST_ASSERT_EQUAL_STRING("LOTY", tr(Str::ScreenFlights, Language::PL));
  TEST_ASSERT_EQUAL_STRING("FLIGHTS", tr(Str::ScreenFlights, Language::EN));
  TEST_ASSERT_EQUAL_STRING("TERAZ", tr(Str::ScreenNow, Language::PL));
  TEST_ASSERT_EQUAL_STRING("NOW", tr(Str::ScreenNow, Language::EN));
  TEST_ASSERT_EQUAL_STRING("RADAR", tr(Str::ScreenRadar, Language::PL));
  TEST_ASSERT_EQUAL_STRING("RADAR", tr(Str::ScreenRadar, Language::EN));
}

static void test_languages_actually_differ(void) {
  TEST_ASSERT_TRUE(std::strcmp(tr(Str::NoFlightsInRange, Language::PL),
                               tr(Str::NoFlightsInRange, Language::EN)) != 0);
}

// ---- current language -------------------------------------------------------

static void test_default_language_is_polish(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL), static_cast<int>(kDefaultLanguage));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL), static_cast<int>(i18nLanguage()));
}

static void test_single_arg_tr_follows_current_language(void) {
  i18nSetLanguage(Language::EN);
  TEST_ASSERT_EQUAL_STRING("NOW", tr(Str::ScreenNow));
  i18nSetLanguage(Language::PL);
  TEST_ASSERT_EQUAL_STRING("TERAZ", tr(Str::ScreenNow));
}

// ---- codes (web form / NVS) -------------------------------------------------

static void test_language_codes_round_trip(void) {
  for (Language lang : kAllLanguages) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(lang),
                          static_cast<int>(languageFromCode(languageCode(lang))));
  }
  TEST_ASSERT_EQUAL_STRING("pl", languageCode(Language::PL));
  TEST_ASSERT_EQUAL_STRING("en", languageCode(Language::EN));
}

static void test_unknown_code_falls_back_to_default(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage), static_cast<int>(languageFromCode("")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage), static_cast<int>(languageFromCode("de")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage), static_cast<int>(languageFromCode(nullptr)));
}

static void test_clamp_language_index(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL), static_cast<int>(clampLanguage(0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::EN), static_cast<int>(clampLanguage(1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage), static_cast<int>(clampLanguage(2)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage), static_cast<int>(clampLanguage(-1)));
}

// ---- plural forms -------------------------------------------------------------

static int form(int n, Language lang) { return static_cast<int>(pluralForm(n, lang)); }
static const int kOne = static_cast<int>(PluralForm::One);
static const int kFew = static_cast<int>(PluralForm::Few);
static const int kMany = static_cast<int>(PluralForm::Many);

static void test_plural_polish(void) {
  TEST_ASSERT_EQUAL_INT(kMany, form(0, Language::PL));   // 0 LOTOW
  TEST_ASSERT_EQUAL_INT(kOne, form(1, Language::PL));    // 1 LOT
  TEST_ASSERT_EQUAL_INT(kFew, form(2, Language::PL));    // 2 LOTY
  TEST_ASSERT_EQUAL_INT(kFew, form(4, Language::PL));
  TEST_ASSERT_EQUAL_INT(kMany, form(5, Language::PL));   // 5 LOTOW
  TEST_ASSERT_EQUAL_INT(kMany, form(11, Language::PL));
  TEST_ASSERT_EQUAL_INT(kMany, form(12, Language::PL));  // 12-14 are "many"
  TEST_ASSERT_EQUAL_INT(kMany, form(14, Language::PL));
  TEST_ASSERT_EQUAL_INT(kMany, form(21, Language::PL));  // 21 LOTOW (not "one")
  TEST_ASSERT_EQUAL_INT(kFew, form(22, Language::PL));   // 22 LOTY
  TEST_ASSERT_EQUAL_INT(kFew, form(104, Language::PL));
  TEST_ASSERT_EQUAL_INT(kMany, form(112, Language::PL));
}

static void test_plural_english(void) {
  TEST_ASSERT_EQUAL_INT(kMany, form(0, Language::EN));
  TEST_ASSERT_EQUAL_INT(kOne, form(1, Language::EN));
  TEST_ASSERT_EQUAL_INT(kMany, form(2, Language::EN));
  TEST_ASSERT_EQUAL_INT(kMany, form(22, Language::EN));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_plural_polish);
  RUN_TEST(test_plural_english);

  RUN_TEST(test_every_string_non_empty_in_every_language);
  RUN_TEST(test_every_string_is_printable_ascii);
  RUN_TEST(test_out_of_range_id_does_not_crash);

  RUN_TEST(test_screen_names);
  RUN_TEST(test_languages_actually_differ);

  RUN_TEST(test_default_language_is_polish);
  RUN_TEST(test_single_arg_tr_follows_current_language);

  RUN_TEST(test_language_codes_round_trip);
  RUN_TEST(test_unknown_code_falls_back_to_default);
  RUN_TEST(test_clamp_language_index);

  return UNITY_END();
}
