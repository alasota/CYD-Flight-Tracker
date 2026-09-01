#include <unity.h>

#include "config_portal.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bbox_area_matches_radius(void) {
  TEST_ASSERT_EQUAL_FLOAT(25.0f, bboxAreaSqDeg(2.5f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, bboxAreaSqDeg(5.0f));
  TEST_ASSERT_EQUAL_FLOAT(400.0f, bboxAreaSqDeg(10.0f));
}

static void test_credit_cost_boundary_exactly_25_sq_deg(void) {
  // radius 2.5 -> bbox 5x5 = 25 sq deg -> still the "<=25" tier.
  TEST_ASSERT_EQUAL_INT(1, openSkyCreditCost(2.5f));
}

static void test_credit_cost_boundary_exactly_100_sq_deg(void) {
  // radius 5.0 -> bbox 10x10 = 100 sq deg -> the "25-100" tier.
  TEST_ASSERT_EQUAL_INT(2, openSkyCreditCost(5.0f));
}

static void test_credit_cost_boundary_exactly_400_sq_deg(void) {
  // radius 10.0 -> bbox 20x20 = 400 sq deg -> the "100-400" tier.
  TEST_ASSERT_EQUAL_INT(3, openSkyCreditCost(10.0f));
}

static void test_credit_cost_just_below_and_above_each_boundary(void) {
  TEST_ASSERT_EQUAL_INT(1, openSkyCreditCost(2.4f));   // area 23.04, < 25
  TEST_ASSERT_EQUAL_INT(2, openSkyCreditCost(2.6f));   // area 27.04, > 25
  TEST_ASSERT_EQUAL_INT(2, openSkyCreditCost(4.9f));   // area 96.04, < 100
  TEST_ASSERT_EQUAL_INT(3, openSkyCreditCost(5.1f));   // area 104.04, > 100
  TEST_ASSERT_EQUAL_INT(3, openSkyCreditCost(9.9f));   // area 392.04, < 400
  TEST_ASSERT_EQUAL_INT(4, openSkyCreditCost(10.1f));  // area 408.04, > 400
}

static void test_credit_cost_far_over_400_sq_deg(void) {
  TEST_ASSERT_EQUAL_INT(4, openSkyCreditCost(50.0f));
}

static void test_credit_cost_at_zero_radius(void) {
  TEST_ASSERT_EQUAL_INT(1, openSkyCreditCost(0.0f));
}

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
  // reject it before any parsing is attempted (review notes 4.3).
  std::string oversized(kMaxCredentialsJsonBytes + 1, 'x');
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(oversized);
  TEST_ASSERT_FALSE(creds.ok);
}

// ---- isValidFloatString (review notes 1.6) ---------------------------------

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

// ---- isValidUnsignedIntString (review notes 1.6) ---------------------------

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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_bbox_area_matches_radius);
  RUN_TEST(test_credit_cost_boundary_exactly_25_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_100_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_400_sq_deg);
  RUN_TEST(test_credit_cost_just_below_and_above_each_boundary);
  RUN_TEST(test_credit_cost_far_over_400_sq_deg);
  RUN_TEST(test_credit_cost_at_zero_radius);
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
  return UNITY_END();
}
