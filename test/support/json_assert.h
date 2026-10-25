// JSON checks by path instead of by exact text, so whitespace and key order
// don't matter (spec/done/07-test-reorganization.md §4). Paths are dot-separated
// object keys: "general.update_check".
#pragma once

#include <ArduinoJson.h>
#include <unity.h>

#include <string>

#include "test_support.h"

inline JsonDocument parseJson(const std::string &text, int line = __builtin_LINE()) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, text);
  if (err) {
    UNITY_TEST_FAIL(line, test_support::failMessage(std::string("not JSON (") + err.c_str() + "): " +
                                                    test_support::head(text, 200)));
  }
  return doc;
}

namespace test_support {

inline JsonVariantConst jsonAt(const JsonDocument &doc, const std::string &path) {
  JsonVariantConst v = doc.as<JsonVariantConst>();
  size_t from = 0;
  while (from <= path.size()) {
    const size_t dot = path.find('.', from);
    const std::string key = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
    v = v[key];
    if (dot == std::string::npos) break;
    from = dot + 1;
  }
  return v;
}

inline std::string jsonText(JsonVariantConst v) {
  if (v.isNull()) return "missing";
  std::string out;
  serializeJson(v, out);
  return out;
}

inline void failJson(const std::string &path, const std::string &expected, JsonVariantConst actual, int line) {
  UNITY_TEST_FAIL(line, failMessage(path + ": expected " + expected + ", was " + jsonText(actual)));
}

}  // namespace test_support

inline void assertJsonBool(const JsonDocument &doc, const std::string &path, bool expected,
                           int line = __builtin_LINE()) {
  const JsonVariantConst v = test_support::jsonAt(doc, path);
  if (!v.is<bool>() || v.as<bool>() != expected) test_support::failJson(path, expected ? "true" : "false", v, line);
}

inline void assertJsonInt(const JsonDocument &doc, const std::string &path, long expected,
                          int line = __builtin_LINE()) {
  const JsonVariantConst v = test_support::jsonAt(doc, path);
  if (!v.is<long>() || v.as<long>() != expected) test_support::failJson(path, std::to_string(expected), v, line);
}

inline void assertJsonString(const JsonDocument &doc, const std::string &path, const std::string &expected,
                             int line = __builtin_LINE()) {
  const JsonVariantConst v = test_support::jsonAt(doc, path);
  if (!v.is<const char *>() || expected != v.as<const char *>()) {
    test_support::failJson(path, "\"" + expected + "\"", v, line);
  }
}

// The key is absent (e.g. secrets left out of a backup).
inline void assertJsonMissing(const JsonDocument &doc, const std::string &path, int line = __builtin_LINE()) {
  const JsonVariantConst v = test_support::jsonAt(doc, path);
  if (!v.isNull()) test_support::failJson(path, "missing", v, line);
}
