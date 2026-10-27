// Shared assertions and the per-test reset for the native suites
// (spec/done/07-test-reorganization.md §3.1, §4). Header-only: every suite's
// main.cpp and *_test.cpp include it via `-I test/support`.
#pragma once

#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "enrichment/domain/aircraft_lookup.h"
#include "fixtures.h"
#include "display/domain/i18n.h"
#include "enrichment/domain/route_lookup.h"

// Clears all global state the modules (and fixtures.h's stubs) keep between
// tests. Called from every suite's setUp().
inline void resetAllForTesting() {
  aircraftLookupClearCacheForTesting();
  routeLookupClearCacheForTesting();
  airportLookupClearCacheForTesting();
  resetStubPorts();
  i18nSetLanguage(kDefaultLanguage);
}

// main.cpp's -DTEST_MODULE=<module> filter.
#define TEST_SUPPORT_STR2(x) #x
#define TEST_SUPPORT_STR(x) TEST_SUPPORT_STR2(x)
inline bool testModuleIs(const char *only, const char *module) { return std::strcmp(only, module) == 0; }

namespace test_support {

// Unity's failure longjmps out of the test, so the message must outlive the
// caller's locals — one static buffer, rebuilt per failure.
inline const char *failMessage(const std::string &text) {
  static std::string buffer;
  buffer = text;
  return buffer.c_str();
}

inline std::string around(const std::string &s, size_t at, size_t len, size_t margin) {
  const size_t from = at > margin ? at - margin : 0;
  const size_t to = std::min(s.size(), at + len + margin);
  return (from > 0 ? "..." : "") + s.substr(from, to - from) + (to < s.size() ? "..." : "");
}

inline std::string head(const std::string &s, size_t n) { return s.size() > n ? s.substr(0, n) + "..." : s; }

inline std::string asString(const std::string &s) { return s; }
inline std::string asString(const char *s) { return s ? s : "(null)"; }

inline void assertContains(const std::string &haystack, const std::string &needle, const char *expr, int line) {
  if (haystack.find(needle) != std::string::npos) return;
  UNITY_TEST_FAIL(line, failMessage("\"" + needle + "\" not found in " + expr + " = \"" + head(haystack, 200) + "\""));
}

inline void assertNotContains(const std::string &haystack, const std::string &needle, const char *expr, int line) {
  const size_t at = haystack.find(needle);
  if (at == std::string::npos) return;
  UNITY_TEST_FAIL(line, failMessage("\"" + needle + "\" unexpectedly found in " + expr + ": \"" +
                                    around(haystack, at, needle.size(), 60) + "\""));
}

inline void assertContains(const std::string &haystack, char needle, const char *expr, int line) {
  assertContains(haystack, std::string(1, needle), expr, line);
}

inline void assertNotContains(const std::string &haystack, char needle, const char *expr, int line) {
  assertNotContains(haystack, std::string(1, needle), expr, line);
}

inline std::string joined(const std::vector<std::string> &v) {
  std::string out = "[";
  for (size_t i = 0; i < v.size(); ++i) out += (i ? ", \"" : "\"") + v[i] + "\"";
  return out + "]";
}

inline bool has(const std::vector<std::string> &v, const std::string &s) {
  for (const auto &e : v) {
    if (e == s) return true;
  }
  return false;
}

inline void assertContains(const std::vector<std::string> &v, const std::string &item, const char *expr, int line) {
  if (has(v, item)) return;
  UNITY_TEST_FAIL(line, failMessage("\"" + item + "\" not in " + expr + " = " + joined(v)));
}

inline void assertNotContains(const std::vector<std::string> &v, const std::string &item, const char *expr, int line) {
  if (!has(v, item)) return;
  UNITY_TEST_FAIL(line, failMessage("\"" + item + "\" unexpectedly in " + expr + " = " + joined(v)));
}

}  // namespace test_support

// Enum equality with both values in the message ("Expected 2 Was 0. x.kind").
#define ASSERT_ENUM_EQ(expected, actual)                                                     \
  do {                                                                                       \
    static_assert(std::is_enum<std::decay_t<decltype(actual)>>::value, "ASSERT_ENUM_EQ: not an enum"); \
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), #actual); \
  } while (0)

#define ASSERT_ENUM_NE(unexpected, actual)                                                   \
  do {                                                                                       \
    static_assert(std::is_enum<std::decay_t<decltype(actual)>>::value, "ASSERT_ENUM_NE: not an enum"); \
    TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(static_cast<int>(unexpected), static_cast<int>(actual), #actual); \
  } while (0)

// std::string / const char * equality, both strings in the message.
#define ASSERT_STR_EQ(expected, actual)                                                      \
  TEST_ASSERT_EQUAL_STRING_MESSAGE(test_support::asString(expected).c_str(),                 \
                                   test_support::asString(actual).c_str(), #actual)

// Substring of a std::string, or an element of a std::vector<std::string>.
#define ASSERT_CONTAINS(haystack, needle) test_support::assertContains((haystack), (needle), #haystack, __LINE__)
#define ASSERT_NOT_CONTAINS(haystack, needle) \
  test_support::assertNotContains((haystack), (needle), #haystack, __LINE__)
