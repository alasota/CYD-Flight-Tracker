#include <unity.h>

#include "lcars_theme.h"

void setUp(void) {}
void tearDown(void) {}

// ---- rgb565 ---------------------------------------------------------------

static void test_rgb565_primary_colors(void) {
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(0xFF, 0x00, 0x00));  // pure red
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0x00, 0xFF, 0x00));  // pure green
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0x00, 0x00, 0xFF));  // pure blue
}

static void test_rgb565_black_and_white(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565(0x00, 0x00, 0x00));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(0xFF, 0xFF, 0xFF));
}

static void test_rgb565_palette_constants(void) {
  // Locks the actual palette values in place — a change here is a visible
  // palette change, not just an internal refactor.
  TEST_ASSERT_EQUAL_HEX16(0x0000, LCARS_BLACK);
  TEST_ASSERT_EQUAL_HEX16(0xFCC0, LCARS_AMBER);
  TEST_ASSERT_EQUAL_HEX16(0x9B3F, LCARS_BLUE_VIOLET);
  TEST_ASSERT_EQUAL_HEX16(0xFB2C, LCARS_ROSE);
  TEST_ASSERT_EQUAL_HEX16(0x9E7F, LCARS_PALE_BLUE);
}

// ---- elbowArcPoint ---------------------------------------------------------

static void test_elbow_arc_point_cardinal_angles(void) {
  ElbowArcPoint east = elbowArcPoint(10, 0.0f);
  TEST_ASSERT_EQUAL_INT(10, east.x);
  TEST_ASSERT_EQUAL_INT(0, east.y);

  // 90 deg = south (+y), since screen y grows downward — see header comment.
  ElbowArcPoint south = elbowArcPoint(10, 90.0f);
  TEST_ASSERT_EQUAL_INT(0, south.x);
  TEST_ASSERT_EQUAL_INT(10, south.y);

  ElbowArcPoint west = elbowArcPoint(10, 180.0f);
  TEST_ASSERT_EQUAL_INT(-10, west.x);
  TEST_ASSERT_EQUAL_INT(0, west.y);

  ElbowArcPoint north = elbowArcPoint(10, 270.0f);
  TEST_ASSERT_EQUAL_INT(0, north.x);
  TEST_ASSERT_EQUAL_INT(-10, north.y);
}

static void test_elbow_arc_point_zero_radius(void) {
  ElbowArcPoint p = elbowArcPoint(0, 42.0f);
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);
}

static void test_elbow_arc_point_matches_drawElbow_seam(void) {
  // drawElbow() sweeps its corner arc from 180 deg (where it must meet the
  // vertical bar) to 270 deg (where it must meet the horizontal header),
  // at both barWidth (inner) and 2*barWidth (outer) radius. Pin down that
  // those four seam points land exactly where the straight bars start.
  int16_t barWidth = 6;
  int16_t outerRadius = barWidth * 2;

  ElbowArcPoint outerAtVerticalBar = elbowArcPoint(outerRadius, 180.0f);
  TEST_ASSERT_EQUAL_INT(-outerRadius, outerAtVerticalBar.x);
  TEST_ASSERT_EQUAL_INT(0, outerAtVerticalBar.y);

  ElbowArcPoint innerAtVerticalBar = elbowArcPoint(barWidth, 180.0f);
  TEST_ASSERT_EQUAL_INT(-barWidth, innerAtVerticalBar.x);
  TEST_ASSERT_EQUAL_INT(0, innerAtVerticalBar.y);

  ElbowArcPoint outerAtHeader = elbowArcPoint(outerRadius, 270.0f);
  TEST_ASSERT_EQUAL_INT(0, outerAtHeader.x);
  TEST_ASSERT_EQUAL_INT(-outerRadius, outerAtHeader.y);

  ElbowArcPoint innerAtHeader = elbowArcPoint(barWidth, 270.0f);
  TEST_ASSERT_EQUAL_INT(0, innerAtHeader.x);
  TEST_ASSERT_EQUAL_INT(-barWidth, innerAtHeader.y);
}

// ---- viewToggleButtonBounds -------------------------------------------

static void test_view_toggle_button_bounds_stays_within_screen(void) {
  Rect b = viewToggleButtonBounds(320, 240);

  TEST_ASSERT_TRUE(b.x >= 0);
  TEST_ASSERT_TRUE(b.y >= 0);
  TEST_ASSERT_TRUE(b.x + b.w <= 320);
  TEST_ASSERT_TRUE(b.y + b.h <= 240);
  // Anchored to the top-right corner.
  TEST_ASSERT_TRUE(b.x > 320 / 2);
  TEST_ASSERT_TRUE(b.y < 240 / 2);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_rgb565_primary_colors);
  RUN_TEST(test_rgb565_black_and_white);
  RUN_TEST(test_rgb565_palette_constants);

  RUN_TEST(test_elbow_arc_point_cardinal_angles);
  RUN_TEST(test_elbow_arc_point_zero_radius);
  RUN_TEST(test_elbow_arc_point_matches_drawElbow_seam);

  RUN_TEST(test_view_toggle_button_bounds_stays_within_screen);

  return UNITY_END();
}
