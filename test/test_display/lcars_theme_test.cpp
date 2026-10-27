// lcars_theme: palette, fitToWidth, phase icons, font set. docs/screens.md "Design language".
#include <unity.h>

#include "display/domain/theme/lcars_theme.h"
#include "test_support.h"

namespace {

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

static void test_canonical_palette_values(void) {
  // The exact RGB565 values mandated by docs/screens.md "Design language" —
  // every screen spec refers to these by name, so pin them precisely.
  TEST_ASSERT_EQUAL_HEX16(0x0000, LCARS_BLACK);
  TEST_ASSERT_EQUAL_HEX16(0xFC00, LCARS_ORANGE);
  TEST_ASSERT_EQUAL_HEX16(0xFFE0, LCARS_YELLOW);
}

// Scenario: Pomarańczowy LCARS zostaje jako kolor ostrzeżenia „!”
static void test_lcars_orange_stays_as_warning_colour(void) {
  // Spec 11 removes the unused palette colours; LCARS_ORANGE survives as the
  // warning "!" colour.
  TEST_ASSERT_EQUAL_HEX16(LCARS_ORANGE, THEME_WARNING);
  TEST_ASSERT_EQUAL_HEX16(0xFC00, THEME_WARNING);
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

// ---- phase icons: aircraft pixel art -------------------------------------

static const PhaseIconKind kDrawnIcons[] = {PhaseIconKind::TAKEOFF, PhaseIconKind::LANDING,
                                            PhaseIconKind::OVERFLIGHT, PhaseIconKind::GROUND};

static void test_phase_icon_kind_mapping(void) {
  ASSERT_ENUM_EQ(PhaseIconKind::TAKEOFF, phaseIconKind(Phase::TAKEOFF, false));
  ASSERT_ENUM_EQ(PhaseIconKind::LANDING, phaseIconKind(Phase::LANDING, false));
  ASSERT_ENUM_EQ(PhaseIconKind::OVERFLIGHT, phaseIconKind(Phase::OVERFLIGHT, false));
  ASSERT_ENUM_EQ(PhaseIconKind::NONE, phaseIconKind(Phase::NONE, false));
  // On the ground always gets the top-view icon, whatever the phase says.
  ASSERT_ENUM_EQ(PhaseIconKind::GROUND, phaseIconKind(Phase::NONE, true));
  ASSERT_ENUM_EQ(PhaseIconKind::GROUND, phaseIconKind(Phase::OVERFLIGHT, true));
}

static void test_phase_icons_are_well_formed(void) {
  for (PhaseIconKind k : kDrawnIcons) {
    const char *const *rows = phaseIconRows(k);
    TEST_ASSERT_NOT_NULL(rows);
    int bodyPixels = 0;
    for (int y = 0; y < kPhaseIconH; ++y) {
      TEST_ASSERT_EQUAL_size_t(static_cast<size_t>(kPhaseIconW), std::string(rows[y]).size());
      for (int x = 0; x < kPhaseIconW; ++x) {
        const char c = rows[y][x];
        ASSERT_CONTAINS(std::string(".#dkg"), c);
        if (c == '#') ++bodyPixels;
      }
    }
    TEST_ASSERT_TRUE(bodyPixels > 20);  // an aircraft, not a speck
  }
  TEST_ASSERT_NULL(phaseIconRows(PhaseIconKind::NONE));
}

static void test_phase_icons_are_all_different(void) {
  for (int a = 0; a < 4; ++a) {
    for (int b = a + 1; b < 4; ++b) {
      const char *const *ra = phaseIconRows(kDrawnIcons[a]);
      const char *const *rb = phaseIconRows(kDrawnIcons[b]);
      bool differ = false;
      for (int y = 0; y < kPhaseIconH && !differ; ++y) differ = std::string(ra[y]) != rb[y];
      TEST_ASSERT_TRUE(differ);
    }
  }
}

static bool rowHas(const char *row, char c, int from, int to) {
  for (int x = from; x < to; ++x) {
    if (row[x] == c) return true;
  }
  return false;
}

static void test_runway_under_tail_on_takeoff_and_under_nose_on_landing(void) {
  const char *takeoffBottom = phaseIconRows(PhaseIconKind::TAKEOFF)[kPhaseIconH - 1];
  const char *landingBottom = phaseIconRows(PhaseIconKind::LANDING)[kPhaseIconH - 1];
  // Nose points right: takeoff leaves the runway behind (left), landing
  // heads for it (right).
  TEST_ASSERT_TRUE(rowHas(takeoffBottom, 'g', 0, 4));
  TEST_ASSERT_FALSE(rowHas(takeoffBottom, 'g', 12, kPhaseIconW));
  TEST_ASSERT_TRUE(rowHas(landingBottom, 'g', 12, kPhaseIconW));
  TEST_ASSERT_FALSE(rowHas(landingBottom, 'g', 0, 4));
  // Airborne-level and ground icons have no runway.
  for (PhaseIconKind k : {PhaseIconKind::OVERFLIGHT, PhaseIconKind::GROUND}) {
    for (int y = 0; y < kPhaseIconH; ++y) TEST_ASSERT_FALSE(rowHas(phaseIconRows(k)[y], 'g', 0, kPhaseIconW));
  }
}

static int topBodyRow(PhaseIconKind k, int x) {
  for (int y = 0; y < kPhaseIconH; ++y) {
    if (phaseIconRows(k)[y][x] == '#') return y;
  }
  return -1;
}

static void test_takeoff_climbs_and_landing_descends(void) {
  // Fuselage top towards the nose (x:10) vs. behind the wing (x:4).
  TEST_ASSERT_TRUE(topBodyRow(PhaseIconKind::TAKEOFF, 10) < topBodyRow(PhaseIconKind::TAKEOFF, 4));
  TEST_ASSERT_TRUE(topBodyRow(PhaseIconKind::LANDING, 10) > topBodyRow(PhaseIconKind::LANDING, 4));
  TEST_ASSERT_EQUAL_INT(topBodyRow(PhaseIconKind::OVERFLIGHT, 10),
                        topBodyRow(PhaseIconKind::OVERFLIGHT, 4));  // level
}

static void test_phase_icon_colours(void) {
  TEST_ASSERT_EQUAL_HEX16(LCARS_GREEN, phaseIconColor(PhaseIconKind::TAKEOFF));
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, phaseIconColor(PhaseIconKind::LANDING));
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, phaseIconColor(PhaseIconKind::OVERFLIGHT));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_MUTED, phaseIconColor(PhaseIconKind::GROUND));

  uint16_t c = 0;
  TEST_ASSERT_TRUE(phaseIconPixelColor(PhaseIconKind::TAKEOFF, '#', &c));
  TEST_ASSERT_EQUAL_HEX16(LCARS_GREEN, c);
  TEST_ASSERT_TRUE(phaseIconPixelColor(PhaseIconKind::TAKEOFF, 'd', &c));
  TEST_ASSERT_NOT_EQUAL(LCARS_GREEN, c);  // engines darker than the body
  TEST_ASSERT_NOT_EQUAL(LCARS_BLACK, c);
  TEST_ASSERT_TRUE(phaseIconPixelColor(PhaseIconKind::LANDING, 'g', &c));
  TEST_ASSERT_EQUAL_HEX16(LCARS_RUNWAY_GREY, c);
  TEST_ASSERT_TRUE(phaseIconPixelColor(PhaseIconKind::LANDING, 'k', &c));
  TEST_ASSERT_EQUAL_HEX16(LCARS_BLACK, c);
  TEST_ASSERT_FALSE(phaseIconPixelColor(PhaseIconKind::LANDING, '.', &c));
}

static void test_shade_rgb565(void) {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, shadeRgb565(0xFFFF, 1, 1));
  TEST_ASSERT_EQUAL_HEX16(0x0000, shadeRgb565(0xFFFF, 0, 1));
  TEST_ASSERT_EQUAL_HEX16(0x7BEF, shadeRgb565(0xFFFF, 1, 2));  // each channel halved, rounded down
}

static void test_phase_icon_fits_text_line(void) {
  // Identity line: 16px text; table rows are checked in test_table_view.
  TEST_ASSERT_TRUE(kPhaseIconH <= LCARS_FONT_BODY_HEIGHT_PX);
}

// ---- directionArrow: "where to look" (bearing from home, north up) ----------

static void test_arrow_points_along_the_bearing(void) {
  const int16_t cx = 50, cy = 50, size = 12;
  ArrowShape n = directionArrow(0.0f, cx, cy, size);
  TEST_ASSERT_TRUE(n.head.visible);
  TEST_ASSERT_EQUAL_INT(cx, n.head.x0);  // tip straight up
  TEST_ASSERT_TRUE(n.head.y0 < cy);
  ArrowShape e = directionArrow(90.0f, cx, cy, size);
  TEST_ASSERT_TRUE(e.head.x0 > cx);      // tip to the right
  TEST_ASSERT_EQUAL_INT(cy, e.head.y0);
  ArrowShape s = directionArrow(180.0f, cx, cy, size);
  TEST_ASSERT_TRUE(s.head.y0 > cy);
  ArrowShape w = directionArrow(270.0f, cx, cy, size);
  TEST_ASSERT_TRUE(w.head.x0 < cx);
}

static void test_arrow_has_a_stem_so_it_is_not_a_phase_triangle(void) {
  // Review: a bare ▼ "where to look" read exactly like the ▼ landing icon.
  // The arrow now has a stem from the tail (opposite the tip) to the head.
  ArrowShape s = directionArrow(180.0f, 50, 50, 12);  // pointing down / south
  TEST_ASSERT_TRUE(s.stem_y0 < 50);                     // tail is up
  TEST_ASSERT_TRUE(s.stem_y1 > s.stem_y0);              // stem runs towards the tip
  TEST_ASSERT_TRUE(s.stem_y1 <= s.head.y0);
  const int stemLen = s.stem_y1 - s.stem_y0;
  TEST_ASSERT_TRUE(stemLen >= 4);
}

static void test_arrow_stays_inside_its_box(void) {
  for (float b = 0.0f; b < 360.0f; b += 15.0f) {
    ArrowShape a = directionArrow(b, 50, 50, 12);
    const int16_t xs[] = {a.head.x0, a.head.x1, a.head.x2, a.stem_x0, a.stem_x1};
    const int16_t ys[] = {a.head.y0, a.head.y1, a.head.y2, a.stem_y0, a.stem_y1};
    for (int i = 0; i < 5; ++i) {
      TEST_ASSERT_TRUE(xs[i] >= 44 && xs[i] <= 56);
      TEST_ASSERT_TRUE(ys[i] >= 44 && ys[i] <= 56);
    }
  }
}

static void test_compass_points(void) {
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(0.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(22.0f));
  TEST_ASSERT_EQUAL_STRING("NE", compassPoint(23.0f));
  TEST_ASSERT_EQUAL_STRING("E", compassPoint(90.0f));
  TEST_ASSERT_EQUAL_STRING("SE", compassPoint(135.0f));
  TEST_ASSERT_EQUAL_STRING("S", compassPoint(180.0f));
  TEST_ASSERT_EQUAL_STRING("SW", compassPoint(225.0f));
  TEST_ASSERT_EQUAL_STRING("W", compassPoint(270.0f));
  TEST_ASSERT_EQUAL_STRING("NW", compassPoint(315.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(350.0f));
  TEST_ASSERT_EQUAL_STRING("N", compassPoint(360.0f));
  TEST_ASSERT_EQUAL_STRING("W", compassPoint(-90.0f));
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

// ---- UTF-8 aware text helpers (efont renders Polish letters) ----------------

static void test_utf8_length_counts_characters_not_bytes(void) {
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("SKAD"));
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("SK\xC4\x84" "D"));        // SKĄD: 5 bytes, 4 chars
  TEST_ASSERT_EQUAL_size_t(4, utf8Length("\xC5\x81\xC3\xB3" "d\xC5\xBA"));  // Łódź
  TEST_ASSERT_EQUAL_size_t(0, utf8Length(""));
}

static void test_font_safe_accepts_ascii_and_polish_only(void) {
  TEST_ASSERT_TRUE(isFontSafeText("ZBLI\xC5\xBB" "A SI\xC4\x98"));  // ZBLIŻA SIĘ
  TEST_ASSERT_TRUE(isFontSafeText("Krak\xC3\xB3w"));                // Kraków
  TEST_ASSERT_TRUE(isFontSafeText("plain ASCII 123"));
  TEST_ASSERT_FALSE(isFontSafeText("Z\xC3\xBCrich"));               // ü — not guaranteed
  TEST_ASSERT_FALSE(isFontSafeText("\xE2\x82\xAC"));                // € — not in the fonts
  TEST_ASSERT_FALSE(isFontSafeText("tab\there"));                   // control char
  TEST_ASSERT_FALSE(isFontSafeText("\xC5"));                        // truncated sequence
}

static void test_fit_never_splits_a_utf8_character(void) {
  // "Łódź Łódź" with a 1px-per-byte measure: every cut must land on a
  // character boundary, i.e. the result is still font-safe UTF-8.
  const std::string s = "\xC5\x81\xC3\xB3" "d\xC5\xBA \xC5\x81\xC3\xB3" "d\xC5\xBA";
  for (int px = 0; px <= static_cast<int>(s.size()); ++px) {
    TEST_ASSERT_TRUE(isFontSafeText(fitToWidth(s, px, kOnePx)));
  }
}

// ---- small font vs. thin strips --------------------------------------------

static void test_fonts_fit_header_and_nav(void) {
  // Header text (efont 10px, drawn with a background colour) must fit the 15px
  // header; nav button labels (efont 16px) must fit the 25px buttons.
  TEST_ASSERT_TRUE(LCARS_FONT_SMALL_HEIGHT_PX + 4 <= LCARS_HEADER_HEIGHT);
  TEST_ASSERT_TRUE(LCARS_FONT_BODY_HEIGHT_PX + 4 <= LCARS_BOTTOM_NAV_HEIGHT);
}

}  // namespace

void run_lcars_theme_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_fit_short_string_unchanged);
  RUN_TEST(test_fit_exact_width_unchanged);
  RUN_TEST(test_fit_long_string_gets_ellipsis);
  RUN_TEST(test_fit_trims_trailing_space_before_ellipsis);
  RUN_TEST(test_fit_result_never_exceeds_width);
  RUN_TEST(test_fit_too_tight_for_ellipsis_hard_cuts);
  RUN_TEST(test_fit_nonpositive_width_is_empty);
  RUN_TEST(test_fit_uses_pixel_width_not_char_count);
  RUN_TEST(test_fit_ascii_ellipsis_only);
  RUN_TEST(test_utf8_length_counts_characters_not_bytes);
  RUN_TEST(test_font_safe_accepts_ascii_and_polish_only);
  RUN_TEST(test_fit_never_splits_a_utf8_character);
  RUN_TEST(test_fonts_fit_header_and_nav);

  RUN_TEST(test_rgb565_primary_colors);
  RUN_TEST(test_rgb565_black_and_white);
  RUN_TEST(test_canonical_palette_values);
  RUN_TEST(test_lcars_orange_stays_as_warning_colour);
  RUN_TEST(test_lcars_header_height);
  RUN_TEST(test_lcars_bottom_nav_height);



  RUN_TEST(test_phase_icon_kind_mapping);
  RUN_TEST(test_phase_icons_are_well_formed);
  RUN_TEST(test_phase_icons_are_all_different);
  RUN_TEST(test_runway_under_tail_on_takeoff_and_under_nose_on_landing);
  RUN_TEST(test_takeoff_climbs_and_landing_descends);
  RUN_TEST(test_phase_icon_colours);
  RUN_TEST(test_shade_rgb565);
  RUN_TEST(test_phase_icon_fits_text_line);
  RUN_TEST(test_arrow_points_along_the_bearing);
  RUN_TEST(test_arrow_stays_inside_its_box);
  RUN_TEST(test_arrow_has_a_stem_so_it_is_not_a_phase_triangle);
  RUN_TEST(test_compass_points);

  RUN_TEST(test_variant_a_palette_values);
  RUN_TEST(test_chrome_and_content_use_different_colour_families);
  RUN_TEST(test_phase_colours);
  RUN_TEST(test_table_rows_alternate_text_colour);

}
