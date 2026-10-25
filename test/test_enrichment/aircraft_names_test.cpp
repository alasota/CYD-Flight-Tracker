// aircraft_names: airline from callsign prefix, friendly type names. CLAUDE.md "Code conventions" (aircraft_names).
#include <unity.h>

#include <cstring>
#include <string>

#include "aircraft_names.h"
#include "lcars_theme.h"  // isFontSafeText()

namespace {

// ---- airline from callsign ---------------------------------------------------

static void test_airline_from_callsign_prefix(void) {
  TEST_ASSERT_EQUAL_STRING("SunExpress", airlineForCallsign("SXS6JZ"));
  TEST_ASSERT_EQUAL_STRING("LOT", airlineForCallsign("LOT6669"));
  TEST_ASSERT_EQUAL_STRING("Wizz Air", airlineForCallsign("WZZ620U"));
  TEST_ASSERT_EQUAL_STRING("Ryanair", airlineForCallsign("RYR12AB"));
  TEST_ASSERT_EQUAL_STRING("Enter Air", airlineForCallsign("ENT1986"));
}

static void test_airline_needs_a_digit_after_the_prefix(void) {
  // Registration-style callsigns ("SPKAX", "N123AB", "DLHAB" without a
  // flight number) are not airline flights — don't guess.
  TEST_ASSERT_NULL(airlineForCallsign("SPKAX"));
  TEST_ASSERT_NULL(airlineForCallsign("N123AB"));
  TEST_ASSERT_NULL(airlineForCallsign("LOTX"));
  TEST_ASSERT_NULL(airlineForCallsign("LO"));
  TEST_ASSERT_NULL(airlineForCallsign(""));
}

static void test_unknown_airline_prefix_is_null(void) {
  TEST_ASSERT_NULL(airlineForCallsign("ZZZ123"));
}

// ---- friendly aircraft type ----------------------------------------------------

static void test_friendly_type_names(void) {
  TEST_ASSERT_EQUAL_STRING("737 MAX 8", friendlyAircraftType("B38M"));
  TEST_ASSERT_EQUAL_STRING("737-800", friendlyAircraftType("B738"));
  TEST_ASSERT_EQUAL_STRING("A321neo", friendlyAircraftType("A21N"));
  TEST_ASSERT_EQUAL_STRING("E175", friendlyAircraftType("E75L"));
  TEST_ASSERT_EQUAL_STRING("787-9", friendlyAircraftType("B789"));
  TEST_ASSERT_EQUAL_STRING("Dash 8-400", friendlyAircraftType("DH8D"));
}

static void test_unknown_type_is_null(void) {
  TEST_ASSERT_NULL(friendlyAircraftType("ZZZZ"));
  TEST_ASSERT_NULL(friendlyAircraftType(""));
}

// ---- table hygiene --------------------------------------------------------------

static void test_tables_unique_font_safe_and_short(void) {
  for (int i = 0; i < airlineTableSize(); ++i) {
    const NameEntry &e = airlineTableEntry(i);
    TEST_ASSERT_EQUAL_size_t(3, std::strlen(e.code));
    TEST_ASSERT_TRUE(isFontSafeText(e.name));
    for (int j = 0; j < i; ++j) TEST_ASSERT_TRUE(std::strcmp(airlineTableEntry(j).code, e.code) != 0);
  }
  for (int i = 0; i < typeTableSize(); ++i) {
    const NameEntry &e = typeTableEntry(i);
    TEST_ASSERT_TRUE(std::strlen(e.code) >= 2 && std::strlen(e.code) <= 4);
    TEST_ASSERT_TRUE(isFontSafeText(e.name));
    // Must fit the table's TYP column (12 characters of the body font).
    TEST_ASSERT_TRUE_MESSAGE(utf8Length(e.name) <= 12, e.name);
    for (int j = 0; j < i; ++j) TEST_ASSERT_TRUE(std::strcmp(typeTableEntry(j).code, e.code) != 0);
  }
  TEST_ASSERT_TRUE(airlineTableSize() > 60);
  TEST_ASSERT_TRUE(typeTableSize() > 60);
}

}  // namespace

void run_aircraft_names_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_airline_from_callsign_prefix);
  RUN_TEST(test_airline_needs_a_digit_after_the_prefix);
  RUN_TEST(test_unknown_airline_prefix_is_null);
  RUN_TEST(test_friendly_type_names);
  RUN_TEST(test_unknown_type_is_null);
  RUN_TEST(test_tables_unique_font_safe_and_short);

}
