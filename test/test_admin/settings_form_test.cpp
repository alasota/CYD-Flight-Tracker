// settings_form: credentials JSON, form number checks. CLAUDE.md "Code conventions" (settings_form).
#include <unity.h>

#include "admin/core/settings_form.h"

namespace {

static void test_parse_credentials_json_valid(void) {
  OpenSkyCredentials creds =
      parseOpenSkyCredentialsJson(R"({"clientId":"przykladowy id","clientSecret":"tajne haslo"})");

  TEST_ASSERT_TRUE(creds.ok);
  TEST_ASSERT_EQUAL_STRING("przykladowy id", creds.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("tajne haslo", creds.client_secret.c_str());
}

static void test_parse_credentials_json_ignores_extra_fields_and_whitespace(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(
      "{\n  \"clientId\": \"abc123\",\n  \"clientSecret\": \"s3cr3t\",\n  \"issuedAt\": "
      "\"2026-01-01\"\n}");

  TEST_ASSERT_TRUE(creds.ok);
  TEST_ASSERT_EQUAL_STRING("abc123", creds.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr3t", creds.client_secret.c_str());
}

static void test_parse_credentials_json_rejects_malformed_json(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson("not json at all");

  TEST_ASSERT_FALSE(creds.ok);
  TEST_ASSERT_TRUE(creds.client_id.empty());
  TEST_ASSERT_TRUE(creds.client_secret.empty());
}

static void test_parse_credentials_json_rejects_missing_field(void) {
  OpenSkyCredentials missingSecret = parseOpenSkyCredentialsJson(R"({"clientId":"abc123"})");
  TEST_ASSERT_FALSE(missingSecret.ok);

  OpenSkyCredentials missingId = parseOpenSkyCredentialsJson(R"({"clientSecret":"s3cr3t"})");
  TEST_ASSERT_FALSE(missingId.ok);
}

static void test_parse_credentials_json_rejects_empty_values(void) {
  OpenSkyCredentials creds =
      parseOpenSkyCredentialsJson(R"({"clientId":"","clientSecret":""})");

  TEST_ASSERT_FALSE(creds.ok);
}

static void test_parse_credentials_json_rejects_empty_string(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson("");

  TEST_ASSERT_FALSE(creds.ok);
}

static void test_parse_credentials_json_rejects_oversized_body(void) {
  // Padding well past kMaxCredentialsJsonBytes — the size check must
  // reject it before any parsing is attempted (docs/review-notes-2026.md 4.3).
  std::string oversized(kMaxCredentialsJsonBytes + 1, 'x');
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(oversized);
  TEST_ASSERT_FALSE(creds.ok);
}

// ---- isValidFloatString (docs/review-notes-2026.md 1.6) --------------------

static void test_valid_float_string_accepts_typical_values(void) {
  TEST_ASSERT_TRUE(isValidFloatString("51.5074"));
  TEST_ASSERT_TRUE(isValidFloatString("-0.1278"));
  TEST_ASSERT_TRUE(isValidFloatString("10"));
  TEST_ASSERT_TRUE(isValidFloatString("0.1"));
  TEST_ASSERT_TRUE(isValidFloatString("  12.5"));  // leading whitespace tolerated, like strtof()
}

static void test_valid_float_string_rejects_garbage(void) {
  TEST_ASSERT_FALSE(isValidFloatString(""));
  TEST_ASSERT_FALSE(isValidFloatString("abc"));
  TEST_ASSERT_FALSE(isValidFloatString("12.3.4"));
  TEST_ASSERT_FALSE(isValidFloatString("12abc"));
  TEST_ASSERT_FALSE(isValidFloatString("12 34"));  // trailing garbage after the number
  TEST_ASSERT_FALSE(isValidFloatString(" "));
}

// ---- isValidUnsignedIntString (docs/review-notes-2026.md 1.6) --------------

static void test_valid_unsigned_int_string_accepts_digits(void) {
  TEST_ASSERT_TRUE(isValidUnsignedIntString("15"));
  TEST_ASSERT_TRUE(isValidUnsignedIntString("0"));
  TEST_ASSERT_TRUE(isValidUnsignedIntString("3600"));
}

static void test_valid_unsigned_int_string_rejects_non_digits(void) {
  TEST_ASSERT_FALSE(isValidUnsignedIntString(""));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("-5"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("12.5"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("abc"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("15s"));
}

}  // namespace

void run_settings_form_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_parse_credentials_json_valid);
  RUN_TEST(test_parse_credentials_json_ignores_extra_fields_and_whitespace);
  RUN_TEST(test_parse_credentials_json_rejects_malformed_json);
  RUN_TEST(test_parse_credentials_json_rejects_missing_field);
  RUN_TEST(test_parse_credentials_json_rejects_empty_values);
  RUN_TEST(test_parse_credentials_json_rejects_empty_string);
  RUN_TEST(test_parse_credentials_json_rejects_oversized_body);

  RUN_TEST(test_valid_float_string_accepts_typical_values);
  RUN_TEST(test_valid_float_string_rejects_garbage);
  RUN_TEST(test_valid_unsigned_int_string_accepts_digits);
  RUN_TEST(test_valid_unsigned_int_string_rejects_non_digits);
}
