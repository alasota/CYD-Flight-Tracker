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
  // Locks the legacy palette values in place — a change here is a visible
  // palette change, not just an internal refactor.
  TEST_ASSERT_EQUAL_HEX16(0x0000, LCARS_BLACK);
  TEST_ASSERT_EQUAL_HEX16(0xFCC0, LCARS_AMBER);
  TEST_ASSERT_EQUAL_HEX16(0x9B3F, LCARS_BLUE_VIOLET);
  TEST_ASSERT_EQUAL_HEX16(0xFB2C, LCARS_ROSE);
  TEST_ASSERT_EQUAL_HEX16(0x9E7F, LCARS_PALE_BLUE);
}

static void test_canonical_palette_values(void) {
  // The exact RGB565 values mandated by CLAUDE.md "Design language" —
  // every screen spec refers to these by name, so pin them precisely.
  TEST_ASSERT_EQUAL_HEX16(0x0000, LCARS_BLACK);
  TEST_ASSERT_EQUAL_HEX16(0xFC00, LCARS_ORANGE);
  TEST_ASSERT_EQUAL_HEX16(0xF81F, LCARS_MAGENTA);
  TEST_ASSERT_EQUAL_HEX16(0x07FF, LCARS_CYAN);
  TEST_ASSERT_EQUAL_HEX16(0xFFE0, LCARS_YELLOW);
}

static void test_lcars_header_height(void) {
  TEST_ASSERT_EQUAL_INT(25, LCARS_HEADER_HEIGHT);
}

static void test_lcars_bottom_nav_height(void) {
  TEST_ASSERT_EQUAL_INT(15, LCARS_BOTTOM_NAV_HEIGHT);
  // Content area is header..(240 - bottom nav) = 28..225.
  TEST_ASSERT_EQUAL_INT(225, 240 - LCARS_BOTTOM_NAV_HEIGHT);
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

static void test_elbow_arc_point_matches_elbow_frame_seam(void) {
  // drawElbowFrame() sweeps its top-left corner arc from 180 deg (where it
  // must meet the top of the left edge) to 270 deg (where it must meet the
  // left end of the top edge), at both cornerRadius (outer) and
  // cornerRadius - thickness (inner). With the arc centered at
  // (x + r, y + r), those four seam points must land exactly where the
  // straight edges begin:
  //   outer @ 180 -> (x,       y + r)   top of the left edge
  //   inner @ 180 -> (x + t,   y + r)   inner top of the left edge
  //   outer @ 270 -> (x + r,   y)       left end of the top edge
  //   inner @ 270 -> (x + r,   y + t)   inner left end of the top edge
  int16_t r = 16;  // cornerRadius
  int16_t t = 5;   // thickness

  ElbowArcPoint outerWest = elbowArcPoint(r, 180.0f);
  TEST_ASSERT_EQUAL_INT(-r, outerWest.x);  // relative to center (x + r, y + r)
  TEST_ASSERT_EQUAL_INT(0, outerWest.y);

  ElbowArcPoint innerWest = elbowArcPoint(static_cast<int16_t>(r - t), 180.0f);
  TEST_ASSERT_EQUAL_INT(-(r - t), innerWest.x);
  TEST_ASSERT_EQUAL_INT(0, innerWest.y);

  ElbowArcPoint outerNorth = elbowArcPoint(r, 270.0f);
  TEST_ASSERT_EQUAL_INT(0, outerNorth.x);
  TEST_ASSERT_EQUAL_INT(-r, outerNorth.y);

  ElbowArcPoint innerNorth = elbowArcPoint(static_cast<int16_t>(r - t), 270.0f);
  TEST_ASSERT_EQUAL_INT(0, innerNorth.x);
  TEST_ASSERT_EQUAL_INT(-(r - t), innerNorth.y);
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

// ---- phaseIcon --------------------------------------------------------------

static void test_phase_icon_distinct_ascii_per_phase(void) {
  char none = phaseIcon(Phase::NONE);
  char takeoff = phaseIcon(Phase::TAKEOFF);
  char landing = phaseIcon(Phase::LANDING);
  char overflight = phaseIcon(Phase::OVERFLIGHT);

  // All ASCII-printable (bitmap fonts generally only cover 0x20-0x7E).
  TEST_ASSERT_TRUE(none >= 0x20 && none <= 0x7E);
  TEST_ASSERT_TRUE(takeoff >= 0x20 && takeoff <= 0x7E);
  TEST_ASSERT_TRUE(landing >= 0x20 && landing <= 0x7E);
  TEST_ASSERT_TRUE(overflight >= 0x20 && overflight <= 0x7E);

  // All four distinct from one another.
  TEST_ASSERT_TRUE(none != takeoff);
  TEST_ASSERT_TRUE(none != landing);
  TEST_ASSERT_TRUE(none != overflight);
  TEST_ASSERT_TRUE(takeoff != landing);
  TEST_ASSERT_TRUE(takeoff != overflight);
  TEST_ASSERT_TRUE(landing != overflight);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_rgb565_primary_colors);
  RUN_TEST(test_rgb565_black_and_white);
  RUN_TEST(test_rgb565_palette_constants);
  RUN_TEST(test_canonical_palette_values);
  RUN_TEST(test_lcars_header_height);
  RUN_TEST(test_lcars_bottom_nav_height);

  RUN_TEST(test_elbow_arc_point_cardinal_angles);
  RUN_TEST(test_elbow_arc_point_zero_radius);
  RUN_TEST(test_elbow_arc_point_matches_drawElbow_seam);
  RUN_TEST(test_elbow_arc_point_matches_elbow_frame_seam);

  RUN_TEST(test_view_toggle_button_bounds_stays_within_screen);

  RUN_TEST(test_phase_icon_distinct_ascii_per_phase);

  return UNITY_END();
}
