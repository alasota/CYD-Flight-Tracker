// scan_area: bbox area and the OpenSky credit cost per call. docs/data-sources.md "Rate limits / credits".
#include <unity.h>

#include "opensky/core/scan_area.h"

namespace {

// ---- computeScanArea -------------------------------------------------

static void test_bbox_default_radius_matches_config_portal_1_credit_tier(void) {
  // radius 2.5 deg is defaultSettings()'s and web_server's default, sitting
  // exactly at the 1-credit tier boundary (bbox area = 25 sq deg there).
  ScanArea bbox = computeScanArea(51.5f, -0.1f, 2.5f);

  TEST_ASSERT_EQUAL_FLOAT(49.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(54.0f, bbox.lamax);
  TEST_ASSERT_EQUAL_FLOAT(-2.6f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(2.4f, bbox.lomax);
}

static void test_bbox_radius_past_1_credit_tier(void) {
  // radius 5.0 deg crosses web_server's 1-credit threshold (bbox area
  // there is 100 sq deg -> its 2-credit tier) — bbox math itself doesn't
  // care about credits, but the box should scale accordingly.
  ScanArea bbox = computeScanArea(0.0f, 0.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(-5.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, bbox.lamax);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, bbox.lomax);
}

static void test_bbox_clamps_near_north_pole(void) {
  ScanArea bbox = computeScanArea(89.0f, 10.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(84.0f, bbox.lamin);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, bbox.lamax);  // clamped, not 94
}

static void test_bbox_clamps_near_antimeridian(void) {
  ScanArea bbox = computeScanArea(0.0f, 179.0f, 5.0f);

  TEST_ASSERT_EQUAL_FLOAT(174.0f, bbox.lomin);
  TEST_ASSERT_EQUAL_FLOAT(180.0f, bbox.lomax);  // clamped, not 184
}

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

}  // namespace

void run_scan_area_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_bbox_default_radius_matches_config_portal_1_credit_tier);
  RUN_TEST(test_bbox_radius_past_1_credit_tier);
  RUN_TEST(test_bbox_clamps_near_north_pole);
  RUN_TEST(test_bbox_clamps_near_antimeridian);
  RUN_TEST(test_bbox_area_matches_radius);
  RUN_TEST(test_credit_cost_boundary_exactly_25_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_100_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_400_sq_deg);
  RUN_TEST(test_credit_cost_just_below_and_above_each_boundary);
  RUN_TEST(test_credit_cost_far_over_400_sq_deg);
  RUN_TEST(test_credit_cost_at_zero_radius);
}
