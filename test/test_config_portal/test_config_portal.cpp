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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_bbox_area_matches_radius);
  RUN_TEST(test_credit_cost_boundary_exactly_25_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_100_sq_deg);
  RUN_TEST(test_credit_cost_boundary_exactly_400_sq_deg);
  RUN_TEST(test_credit_cost_just_below_and_above_each_boundary);
  RUN_TEST(test_credit_cost_far_over_400_sq_deg);
  RUN_TEST(test_credit_cost_at_zero_radius);
  return UNITY_END();
}
