#include <unity.h>

#include <cstring>
#include <string>

#include "place_names.h"

void setUp(void) {}
void tearDown(void) {}

// ---- asciiFold ---------------------------------------------------------------

static void test_fold_keeps_plain_ascii(void) {
  TEST_ASSERT_EQUAL_STRING("Warsaw Chopin Airport", asciiFold("Warsaw Chopin Airport").c_str());
}

static void test_fold_en_dash_to_hyphen(void) {
  // hexdb.io: "Rome–Fiumicino International Airport"
  TEST_ASSERT_EQUAL_STRING("Rome-Fiumicino", asciiFold("Rome\xE2\x80\x93" "Fiumicino").c_str());
}

static void test_fold_polish_and_common_accents(void) {
  TEST_ASSERT_EQUAL_STRING("Krakow", asciiFold("Krak\xC3\xB3w").c_str());           // ó
  TEST_ASSERT_EQUAL_STRING("Gdansk", asciiFold("Gda\xC5\x84sk").c_str());           // ń
  TEST_ASSERT_EQUAL_STRING("Lodz", asciiFold("\xC5\x81\xC3\xB3" "d\xC5\xBA").c_str());  // Łódź
  TEST_ASSERT_EQUAL_STRING("Zurich", asciiFold("Z\xC3\xBCrich").c_str());           // ü
  TEST_ASSERT_EQUAL_STRING("Malaga", asciiFold("M\xC3\xA1laga").c_str());           // á
}

static void test_fold_output_is_always_printable_ascii(void) {
  const char *nasty = "\xF0\x9F\x9B\xA9 Pla\xC3\xA7" "a \xE2\x80\x94 \xE6\x9D\xB1\xE4\xBA\xAC";
  for (char c : asciiFold(nasty)) {
    TEST_ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) <= 0x7E);
  }
}

// ---- shortAirportName ------------------------------------------------------------

static void test_short_name_strips_generic_words(void) {
  TEST_ASSERT_EQUAL_STRING("Warsaw Chopin", shortAirportName("Warsaw Chopin Airport").c_str());
  TEST_ASSERT_EQUAL_STRING("Rome-Fiumicino",
                           shortAirportName("Rome-Fiumicino International Airport").c_str());
  TEST_ASSERT_EQUAL_STRING("Narita", shortAirportName("Narita International Airport").c_str());
  TEST_ASSERT_EQUAL_STRING("Heathrow", shortAirportName("Heathrow Airport").c_str());
}

static void test_short_name_keeps_everything_else(void) {
  TEST_ASSERT_EQUAL_STRING("John Paul II Krakow-Balice",
                           shortAirportName("John Paul II International Airport Krakow-Balice").c_str());
}

static void test_short_name_never_empty_if_input_is_not(void) {
  // A name that is *only* generic words keeps its original text.
  TEST_ASSERT_EQUAL_STRING("International Airport",
                           shortAirportName("International Airport").c_str());
  TEST_ASSERT_EQUAL_STRING("", shortAirportName("").c_str());
}

// ---- cityForIata (built-in table) -----------------------------------------------

static void test_city_table_polish_and_english(void) {
  TEST_ASSERT_EQUAL_STRING("Warszawa", cityForIata("WAW", Language::PL));
  TEST_ASSERT_EQUAL_STRING("Warsaw", cityForIata("WAW", Language::EN));
  TEST_ASSERT_EQUAL_STRING("Londyn", cityForIata("LHR", Language::PL));
  TEST_ASSERT_EQUAL_STRING("London", cityForIata("LHR", Language::EN));
  TEST_ASSERT_EQUAL_STRING("Rzym", cityForIata("FCO", Language::PL));
  TEST_ASSERT_EQUAL_STRING("Tokio", cityForIata("NRT", Language::PL));
  TEST_ASSERT_EQUAL_STRING("Tokyo", cityForIata("NRT", Language::EN));
}

static void test_city_table_unknown_is_null(void) {
  TEST_ASSERT_NULL(cityForIata("ZZZ", Language::PL));
  TEST_ASSERT_NULL(cityForIata("", Language::EN));
  TEST_ASSERT_NULL(cityForIata("WA", Language::EN));
}

static void test_city_table_is_sorted_unique_ascii(void) {
  // Binary search relies on strict IATA order; fonts need ASCII.
  const int n = cityTableSize();
  TEST_ASSERT_TRUE(n > 50);
  for (int i = 0; i < n; ++i) {
    const CityEntry &e = cityTableEntry(i);
    TEST_ASSERT_EQUAL_size_t(3, std::strlen(e.iata));
    if (i > 0) TEST_ASSERT_TRUE(std::strcmp(cityTableEntry(i - 1).iata, e.iata) < 0);
    for (const char *s : {e.pl, e.en}) {
      TEST_ASSERT_TRUE(std::strlen(s) > 0);
      for (const char *p = s; *p; ++p) {
        TEST_ASSERT_TRUE(static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) <= 0x7E);
      }
    }
  }
}

// ---- placeName: table -> short airport name -> IATA -> ICAO ----------------------

static AirportInfo airport(const char *iata, const char *name) {
  AirportInfo a;
  a.found = true;
  a.iata_code = iata;
  a.country_code = "XX";
  a.name = name;
  return a;
}

static void test_place_name_prefers_city_table(void) {
  TEST_ASSERT_EQUAL_STRING("Warszawa",
                           placeName(airport("WAW", "Warsaw Chopin Airport"), "EPWA", Language::PL).c_str());
}

static void test_place_name_falls_back_to_short_airport_name(void) {
  TEST_ASSERT_EQUAL_STRING("Sample Field",
                           placeName(airport("QQQ", "Sample Field Airport"), "XQQQ", Language::PL).c_str());
}

static void test_place_name_falls_back_to_codes(void) {
  TEST_ASSERT_EQUAL_STRING("QQQ", placeName(airport("QQQ", ""), "XQQQ", Language::PL).c_str());
  AirportInfo unknown;  // hexdb.io had no record
  TEST_ASSERT_EQUAL_STRING("XQQQ", placeName(unknown, "XQQQ", Language::EN).c_str());
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_fold_keeps_plain_ascii);
  RUN_TEST(test_fold_en_dash_to_hyphen);
  RUN_TEST(test_fold_polish_and_common_accents);
  RUN_TEST(test_fold_output_is_always_printable_ascii);

  RUN_TEST(test_short_name_strips_generic_words);
  RUN_TEST(test_short_name_keeps_everything_else);
  RUN_TEST(test_short_name_never_empty_if_input_is_not);

  RUN_TEST(test_city_table_polish_and_english);
  RUN_TEST(test_city_table_unknown_is_null);
  RUN_TEST(test_city_table_is_sorted_unique_ascii);

  RUN_TEST(test_place_name_prefers_city_table);
  RUN_TEST(test_place_name_falls_back_to_short_airport_name);
  RUN_TEST(test_place_name_falls_back_to_codes);

  return UNITY_END();
}
