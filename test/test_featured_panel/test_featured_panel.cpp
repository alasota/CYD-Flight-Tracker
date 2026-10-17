#include <unity.h>

#include "featured_panel.h"

void setUp(void) {}
void tearDown(void) {}

// featured_panel's text formatting now lives in aircraft_summary
// (formatSummaryIdentity / formatSummaryRoute — covered by
// test_aircraft_summary). What remains pure here is the panel's fixed
// on-frame geometry from CLAUDE.md "Screen 1" ("y: 18 to 95px").

static void test_featured_panel_top_is_18(void) {
  TEST_ASSERT_EQUAL_INT(18, featuredPanelTopPx());
}

static void test_featured_panel_height_is_77(void) {
  TEST_ASSERT_EQUAL_INT(77, featuredPanelHeightPx());
}

static void test_featured_panel_bottom_is_95_and_on_screen(void) {
  int16_t bottom = static_cast<int16_t>(featuredPanelTopPx() + featuredPanelHeightPx());
  TEST_ASSERT_EQUAL_INT(95, bottom);
  TEST_ASSERT_TRUE(bottom < 240);  // leaves room for the table section below
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_featured_panel_top_is_18);
  RUN_TEST(test_featured_panel_height_is_77);
  RUN_TEST(test_featured_panel_bottom_is_95_and_on_screen);

  return UNITY_END();
}
