#include <unity.h>

#include "opensky_client.h"

void setUp(void) {}
void tearDown(void) {}

// ---- computeBoundingBox ----------------------------------------------

static void test_bbox_default_radius_matches_config_portal_1_credit_tier(void) {
  // radius 2.5 deg is config_store's/config_portal's default, sitting
  // exactly at the 1-credit tier boundary (bbox area = 25 sq deg there).
  BoundingBox bbox = computeBoundingBox(51.5f, -0.1f, 2.5f);

  TEST_ASSERT_EQUAL_FLOAT(49.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(54.0f, bbox.lamax);
  TEST_ASSERT_EQUAL_FLOAT(-2.6f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(2.4f, bbox.lomax);
}

static void test_bbox_radius_past_1_credit_tier(void) {
  // radius 5.0 deg crosses config_portal's 1-credit threshold (bbox area
  // there is 100 sq deg -> its 2-credit tier) — bbox math itself doesn't
  // care about credits, but the box should scale accordingly.
  BoundingBox bbox = computeBoundingBox(0.0f, 0.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(-5.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, bbox.lamax);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, bbox.lomax);
}

static void test_bbox_clamps_near_north_pole(void) {
  BoundingBox bbox = computeBoundingBox(89.0f, 10.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(84.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, bbox.lamax);  // clamped, not 94
}

static void test_bbox_clamps_near_antimeridian(void) {
  BoundingBox bbox = computeBoundingBox(0.0f, 179.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(174.0f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(180.0f, bbox.lomax);  // clamped, not 184
}

// ---- tokenNeedsRefresh -------------------------------------------------

static void test_token_never_fetched_needs_refresh(void) {
  TokenState token;  // valid == false by default
  TEST_ASSERT_TRUE(tokenNeedsRefresh(token, 0, 60));
}

static void test_token_fresh_does_not_need_refresh(void) {
  TokenState token;
  token.valid = true;
  token.obtained_at_ms = 0;
  token.expires_in_s = 1800;

  TEST_ASSERT_FALSE(tokenNeedsRefresh(token, 0, 60));
  TEST_ASSERT_FALSE(tokenNeedsRefresh(token, 1700UL * 1000UL, 60));  // 100s left, > 60s margin
}

static void test_token_needs_refresh_inside_margin(void) {
  TokenState token;
  token.valid = true;
  token.obtained_at_ms = 0;
  token.expires_in_s = 1800;

  // 50s left (< 60s margin) -> refresh now.
  TEST_ASSERT_TRUE(tokenNeedsRefresh(token, 1750UL * 1000UL, 60));
}

static void test_token_needs_refresh_after_expiry(void) {
  TokenState token;
  token.valid = true;
  token.obtained_at_ms = 0;
  token.expires_in_s = 1800;

  TEST_ASSERT_TRUE(tokenNeedsRefresh(token, 1800UL * 1000UL, 60));
  TEST_ASSERT_TRUE(tokenNeedsRefresh(token, 5000UL * 1000UL, 60));
}

static void test_token_refresh_survives_millis_rollover(void) {
  TokenState token;
  token.valid = true;
  token.obtained_at_ms = 4294967200UL;  // near UINT32_MAX
  token.expires_in_s = 1800;

  // now_ms has wrapped past 0 to 100 — true elapsed is only ~196ms.
  TEST_ASSERT_FALSE(tokenNeedsRefresh(token, 100UL, 60));
}

static void test_should_use_oauth(void) {
  TEST_ASSERT_TRUE(shouldUseOAuth("id", "secret"));
  TEST_ASSERT_FALSE(shouldUseOAuth("", "secret"));
  TEST_ASSERT_FALSE(shouldUseOAuth("id", ""));
  TEST_ASSERT_FALSE(shouldUseOAuth("", ""));
}

// ---- parseStatesResponse ------------------------------------------------

static const char *kSampleStatesResponse = R"({
  "time": 1706550000,
  "states": [
    ["3c6444","DLH9LH  ","Germany",1706549990,1706549990,6.1546,50.1210,9639.3,false,232.88,98.24,4.55,null,9852.3,"1000",false,0],
    ["a1b2c3","UAL123  ","United States",1706549990,1706549990,null,null,null,true,0.0,null,null,null,null,"1200",false,0],
    ["deadbe","","Poland",1706549990,1706549990,19.9449,50.0647,1200.0,false,55.5,270.0,-1.2,null,1300.0,"2000",false,0]
  ]
})";

static void test_parse_states_response_valid(void) {
  std::vector<Aircraft> aircraft = parseStatesResponse(kSampleStatesResponse);

  TEST_ASSERT_EQUAL_size_t(3, aircraft.size());

  const Aircraft &a0 = aircraft[0];
  TEST_ASSERT_EQUAL_STRING("3c6444", a0.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("DLH9LH", a0.callsign.c_str());  // trailing spaces trimmed
  TEST_ASSERT_TRUE(a0.has_position);
  TEST_ASSERT_EQUAL_FLOAT(50.1210f, a0.lat);
  TEST_ASSERT_EQUAL_FLOAT(6.1546f, a0.lon);
  TEST_ASSERT_EQUAL_FLOAT(9639.3f, a0.baro_altitude);
  TEST_ASSERT_EQUAL_FLOAT(232.88f, a0.velocity);
  TEST_ASSERT_EQUAL_FLOAT(98.24f, a0.true_track);

  // Null lat/lon (e.g. grounded, no position report yet) -> has_position
  // false, numeric fields left at their defaults where the source was null.
  const Aircraft &a1 = aircraft[1];
  TEST_ASSERT_EQUAL_STRING("a1b2c3", a1.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("UAL123", a1.callsign.c_str());
  TEST_ASSERT_FALSE(a1.has_position);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, a1.baro_altitude);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, a1.true_track);

  const Aircraft &a2 = aircraft[2];
  TEST_ASSERT_EQUAL_STRING("deadbe", a2.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("", a2.callsign.c_str());
  TEST_ASSERT_TRUE(a2.has_position);
  TEST_ASSERT_EQUAL_FLOAT(50.0647f, a2.lat);
  TEST_ASSERT_EQUAL_FLOAT(19.9449f, a2.lon);
}

static void test_parse_states_response_missing_states_key(void) {
  std::vector<Aircraft> aircraft = parseStatesResponse(R"({"time": 1706550000})");
  TEST_ASSERT_TRUE(aircraft.empty());
}

static void test_parse_states_response_null_states(void) {
  std::vector<Aircraft> aircraft =
      parseStatesResponse(R"({"time": 1706550000, "states": null})");
  TEST_ASSERT_TRUE(aircraft.empty());
}

static void test_parse_states_response_empty_states(void) {
  std::vector<Aircraft> aircraft =
      parseStatesResponse(R"({"time": 1706550000, "states": []})");
  TEST_ASSERT_TRUE(aircraft.empty());
}

static void test_parse_states_response_malformed_json(void) {
  std::vector<Aircraft> aircraft = parseStatesResponse("not json at all");
  TEST_ASSERT_TRUE(aircraft.empty());
}

static void test_parse_states_response_skips_short_state_vectors(void) {
  std::vector<Aircraft> aircraft =
      parseStatesResponse(R"({"states": [["3c6444","DLH9LH  "]]})");
  TEST_ASSERT_TRUE(aircraft.empty());
}

// ---- parseRetryAfterSeconds ---------------------------------------------

static void test_parse_retry_after_valid(void) {
  TEST_ASSERT_EQUAL_UINT32(30, parseRetryAfterSeconds("30", 60));
  TEST_ASSERT_EQUAL_UINT32(0, parseRetryAfterSeconds("0", 60));
}

static void test_parse_retry_after_missing_or_invalid_uses_default(void) {
  TEST_ASSERT_EQUAL_UINT32(60, parseRetryAfterSeconds("", 60));
  TEST_ASSERT_EQUAL_UINT32(60, parseRetryAfterSeconds("soon", 60));
  TEST_ASSERT_EQUAL_UINT32(60, parseRetryAfterSeconds("-5", 60));
}

// ---- urlEncode ------------------------------------------------------------

static void test_url_encode_leaves_unreserved_chars(void) {
  TEST_ASSERT_EQUAL_STRING("abcXYZ019-_.~", urlEncode("abcXYZ019-_.~").c_str());
}

static void test_url_encode_escapes_special_chars(void) {
  TEST_ASSERT_EQUAL_STRING("a%26b%3Dc", urlEncode("a&b=c").c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_bbox_default_radius_matches_config_portal_1_credit_tier);
  RUN_TEST(test_bbox_radius_past_1_credit_tier);
  RUN_TEST(test_bbox_clamps_near_north_pole);
  RUN_TEST(test_bbox_clamps_near_antimeridian);

  RUN_TEST(test_token_never_fetched_needs_refresh);
  RUN_TEST(test_token_fresh_does_not_need_refresh);
  RUN_TEST(test_token_needs_refresh_inside_margin);
  RUN_TEST(test_token_needs_refresh_after_expiry);
  RUN_TEST(test_token_refresh_survives_millis_rollover);
  RUN_TEST(test_should_use_oauth);

  RUN_TEST(test_parse_states_response_valid);
  RUN_TEST(test_parse_states_response_missing_states_key);
  RUN_TEST(test_parse_states_response_null_states);
  RUN_TEST(test_parse_states_response_empty_states);
  RUN_TEST(test_parse_states_response_malformed_json);
  RUN_TEST(test_parse_states_response_skips_short_state_vectors);

  RUN_TEST(test_parse_retry_after_valid);
  RUN_TEST(test_parse_retry_after_missing_or_invalid_uses_default);

  RUN_TEST(test_url_encode_leaves_unreserved_chars);
  RUN_TEST(test_url_encode_escapes_special_chars);

  return UNITY_END();
}
