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
  // Slim header (same height the bottom nav used to have).
  TEST_ASSERT_EQUAL_INT(15, LCARS_HEADER_HEIGHT);
  TEST_ASSERT_EQUAL_INT(18, LCARS_CONTENT_TOP);  // 3px gap below the header
}

static void test_lcars_bottom_nav_height(void) {
  TEST_ASSERT_EQUAL_INT(25, LCARS_BOTTOM_NAV_HEIGHT);  // big, tappable buttons
  // Content area is 18..(240 - bottom nav) = 18..215 — still 197px tall.
  TEST_ASSERT_EQUAL_INT(215, LCARS_CONTENT_BOTTOM);
  TEST_ASSERT_EQUAL_INT(197, LCARS_CONTENT_BOTTOM - LCARS_CONTENT_TOP);
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

// ---- colour scheme "variant A" (warm chrome, cool content) -----------------

static void test_variant_a_palette_values(void) {
  // rgb565() of the hex colours from the agreed scheme.
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0x99, 0x66), LCARS_PEACH);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0xCC, 0x99), LCARS_PEACH_LIGHT);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x99, 0x77, 0xAA), LCARS_LAVENDER);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xCC, 0x99, 0xCC), LCARS_LILAC);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x99, 0xCC, 0xFF), LCARS_SKY);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x10, 0x20, 0x40), LCARS_NAVY);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x66, 0xCC, 0x66), LCARS_GREEN);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0x33, 0x33), LCARS_RED);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, LCARS_WHITE);
}

static void test_chrome_and_content_use_different_colour_families(void) {
  // The whole point of the scheme: navigation (header + buttons) never
  // shares a colour with the data it frames.
  const uint16_t chrome[] = {THEME_CHROME_ACCENT, THEME_CHROME_TEXT, THEME_CHROME_INACTIVE};
  const uint16_t content[] = {THEME_CONTENT_PRIMARY, THEME_CONTENT_SECONDARY, THEME_TABLE_HEADER,
                              THEME_PILL_FILL};
  for (uint16_t c : chrome) {
    for (uint16_t d : content) TEST_ASSERT_NOT_EQUAL(c, d);
  }
  // Table header must not look like the header-bar block.
  TEST_ASSERT_NOT_EQUAL(THEME_CHROME_ACCENT, THEME_TABLE_HEADER);
}

static void test_phase_colours(void) {
  TEST_ASSERT_EQUAL_HEX16(LCARS_GREEN, phaseColor(Phase::TAKEOFF));
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, phaseColor(Phase::LANDING));
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, phaseColor(Phase::OVERFLIGHT));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_MUTED, phaseColor(Phase::NONE));
  // Takeoff and landing — the thing people want to see from across the
  // garden — must differ from each other and from plain text.
  TEST_ASSERT_NOT_EQUAL(phaseColor(Phase::TAKEOFF), phaseColor(Phase::LANDING));
  TEST_ASSERT_NOT_EQUAL(THEME_CONTENT_PRIMARY, phaseColor(Phase::TAKEOFF));
  TEST_ASSERT_NOT_EQUAL(THEME_CONTENT_PRIMARY, phaseColor(Phase::LANDING));
}

static void test_table_rows_alternate_text_colour(void) {
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_PRIMARY, tableRowTextColor(0));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_SECONDARY, tableRowTextColor(1));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_PRIMARY, tableRowTextColor(2));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_SECONDARY, tableRowTextColor(3));
}

// ---- phaseTriangle: drawn ▲ / ▼ / ▶ icons ----------------------------------

static void assertInsideBox(const PhaseTriangle &t, int16_t cx, int16_t cy, int16_t size) {
  const int16_t half = size / 2;
  const int16_t xs[] = {t.x0, t.x1, t.x2};
  const int16_t ys[] = {t.y0, t.y1, t.y2};
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(xs[i] >= cx - half && xs[i] <= cx + half);
    TEST_ASSERT_TRUE(ys[i] >= cy - half && ys[i] <= cy + half);
  }
}

static void test_takeoff_triangle_points_up(void) {
  PhaseTriangle t = phaseTriangle(Phase::TAKEOFF, 100, 50, 10);
  TEST_ASSERT_TRUE(t.visible);
  assertInsideBox(t, 100, 50, 10);
  // Apex (x0,y0) above the flat base (y1 == y2).
  TEST_ASSERT_EQUAL_INT(t.y1, t.y2);
  TEST_ASSERT_TRUE(t.y0 < t.y1);
  TEST_ASSERT_EQUAL_INT(100, t.x0);
}

static void test_landing_triangle_points_down(void) {
  PhaseTriangle t = phaseTriangle(Phase::LANDING, 100, 50, 10);
  TEST_ASSERT_TRUE(t.visible);
  assertInsideBox(t, 100, 50, 10);
  TEST_ASSERT_EQUAL_INT(t.y1, t.y2);
  TEST_ASSERT_TRUE(t.y0 > t.y1);
}

static void test_overflight_triangle_points_right(void) {
  PhaseTriangle t = phaseTriangle(Phase::OVERFLIGHT, 100, 50, 10);
  TEST_ASSERT_TRUE(t.visible);
  assertInsideBox(t, 100, 50, 10);
  TEST_ASSERT_EQUAL_INT(t.x1, t.x2);  // flat left side
  TEST_ASSERT_TRUE(t.x0 > t.x1);      // tip on the right
  TEST_ASSERT_EQUAL_INT(50, t.y0);
}

static void test_no_phase_draws_nothing(void) {
  TEST_ASSERT_FALSE(phaseTriangle(Phase::NONE, 100, 50, 10).visible);
}

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

// ---- fitToWidth -------------------------------------------------------------
//
// Measured with fake fixed-pitch fonts so expectations are hand-checkable:
// kOnePx (1px per char — width == character count) and kSixPx (Font0's
// 6px advance).

static int onePx(const std::string &s) { return static_cast<int>(s.size()); }
static int sixPx(const std::string &s) { return static_cast<int>(s.size()) * 6; }
static const TextMeasure kOnePx = onePx;
static const TextMeasure kSixPx = sixPx;

static void test_fit_short_string_unchanged(void) {
  TEST_ASSERT_EQUAL_STRING("Ryanair", fitToWidth("Ryanair", 15, kOnePx).c_str());
}

static void test_fit_exact_width_unchanged(void) {
  // width == maxPx is a fit, not an overflow.
  TEST_ASSERT_EQUAL_STRING("123456789012345", fitToWidth("123456789012345", 15, kOnePx).c_str());
}

static void test_fit_long_string_gets_ellipsis(void) {
  std::string out = fitToWidth("LOT Polish Airlines", 13, kOnePx);
  TEST_ASSERT_EQUAL_STRING("LOT Polish...", out.c_str());
}

static void test_fit_trims_trailing_space_before_ellipsis(void) {
  // Head would end "LOT " — the space is dropped so it's not "LOT ...".
  TEST_ASSERT_EQUAL_STRING("LOT...", fitToWidth("LOT Airlines", 7, kOnePx).c_str());
}

static void test_fit_result_never_exceeds_width(void) {
  const std::string name = "Scandinavian Airlines System Denmark Norway Sweden";
  for (int px = 0; px <= 130; ++px) {
    TEST_ASSERT_TRUE(sixPx(fitToWidth(name, px, kSixPx)) <= px);
  }
}

static void test_fit_too_tight_for_ellipsis_hard_cuts(void) {
  // < 4 chars of room can't fit "X..." — plain cut, no ellipsis.
  TEST_ASSERT_EQUAL_STRING("Lu", fitToWidth("Lufthansa", 2, kOnePx).c_str());
  TEST_ASSERT_EQUAL_STRING("Luf", fitToWidth("Lufthansa", 3, kOnePx).c_str());
}

static void test_fit_nonpositive_width_is_empty(void) {
  TEST_ASSERT_EQUAL_STRING("", fitToWidth("Lufthansa", 0, kOnePx).c_str());
  TEST_ASSERT_EQUAL_STRING("", fitToWidth("Lufthansa", -5, kOnePx).c_str());
}

static void test_fit_uses_pixel_width_not_char_count(void) {
  // 106px at 6px/char = 17 chars of room: "Wizz Air Hungary Ltd" (20) -> 14 + "...".
  std::string out = fitToWidth("Wizz Air Hungary Ltd", 106, kSixPx);
  TEST_ASSERT_EQUAL_STRING("Wizz Air Hunga...", out.c_str());
  TEST_ASSERT_TRUE(sixPx(out) <= 106);
}

static void test_fit_ascii_ellipsis_only(void) {
  // Must stay within the bitmap fonts' 0x20-0x7E range (no "…").
  std::string out = fitToWidth("Some Very Long Airline Name Ltd", 12, kOnePx);
  for (char c : out) {
    TEST_ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 &&
                     static_cast<unsigned char>(c) <= 0x7E);
  }
}

// ---- small font vs. thin strips --------------------------------------------

static void test_fonts_fit_header_and_nav(void) {
  // Header text (Font0, drawn with a background colour) must fit the 15px
  // header; nav button labels (Font2) must fit the 25px buttons.
  TEST_ASSERT_TRUE(LCARS_FONT_SMALL_HEIGHT_PX + 4 <= LCARS_HEADER_HEIGHT);
  TEST_ASSERT_TRUE(LCARS_FONT_BODY_HEIGHT_PX + 4 <= LCARS_BOTTOM_NAV_HEIGHT);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_fit_short_string_unchanged);
  RUN_TEST(test_fit_exact_width_unchanged);
  RUN_TEST(test_fit_long_string_gets_ellipsis);
  RUN_TEST(test_fit_trims_trailing_space_before_ellipsis);
  RUN_TEST(test_fit_result_never_exceeds_width);
  RUN_TEST(test_fit_too_tight_for_ellipsis_hard_cuts);
  RUN_TEST(test_fit_nonpositive_width_is_empty);
  RUN_TEST(test_fit_uses_pixel_width_not_char_count);
  RUN_TEST(test_fit_ascii_ellipsis_only);
  RUN_TEST(test_fonts_fit_header_and_nav);

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
  RUN_TEST(test_takeoff_triangle_points_up);
  RUN_TEST(test_landing_triangle_points_down);
  RUN_TEST(test_overflight_triangle_points_right);
  RUN_TEST(test_no_phase_draws_nothing);

  RUN_TEST(test_variant_a_palette_values);
  RUN_TEST(test_chrome_and_content_use_different_colour_families);
  RUN_TEST(test_phase_colours);
  RUN_TEST(test_table_rows_alternate_text_colour);

  return UNITY_END();
}
