// ui_flow: taps, table pages, screen changes, clock minute, countdown, auto-cycle, shown health. docs/screens.md "Screen navigation", "Auto screen cycling".
#include <unity.h>

#include "display/services/ui_flow.h"

#include <cstdint>
#include <string>
#include <vector>

#include "display/core/i18n.h"
#include "display/core/screens/table_view.h"
#include "display/core/theme/lcars_theme.h"
#include "fixtures.h"
#include "platform/core_link.h"  // kNetStallLimitMs
#include "test_support.h"

namespace {

constexpr int16_t kW = 320;
constexpr int16_t kH = 240;

// The header as drawn with (or without) the "!" badge.
static HeaderZones headerWithBadge(bool badge) {
  return statusBarZones(kW, kStatusBarAppName, statusBarBlockLabel(7, true, Language::PL),
                        statusBarDate(0, false), statusBarClock(0, false), badge);
}

static TapContext context(int screen, int page = 0, int pageCount = 0) {
  TapContext c;
  c.currentScreen = screen;
  c.page = page;
  c.pageCount = pageCount;
  c.homeConfigured = true;
  c.header = headerWithBadge(false);
  c.screenW = kW;
  c.screenH = kH;
  return c;
}

// A y inside the table's first-row strip and inside its last-row strip.
static int16_t topStripY() { return static_cast<int16_t>(tableFirstRowY() + 2); }
static int16_t bottomStripY() {
  return static_cast<int16_t>(kH - LCARS_BOTTOM_NAV_HEIGHT - tableRowHeightPx() + 2);
}

// Dotknięcie zakładki na dole przełącza na jej ekran z każdego ekranu
static void test_route_tap_bottom_nav_jumps_to_its_screen(void) {
  struct Example {
    int screen;
    int16_t x;
    int target;
  };
  const Example examples[] = {{0, 50, 0}, {0, 160, 1}, {1, 300, 2}, {2, 10, 0}};
  for (const Example &e : examples) {
    const TapAction a = routeTap(e.x, 225, context(e.screen));
    const std::string what = "screen " + std::to_string(e.screen) + ", x " + std::to_string(e.x);
    ASSERT_ENUM_EQ(TapKind::JumpTo, a.kind);
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.target, a.target, what.c_str());
  }
}

// Dotknięcie „!” w nagłówku otwiera ekran ostrzeżeń, zanim zadziała cokolwiek innego
static void test_route_tap_alert_badge_comes_first(void) {
  TapContext c = context(kScreenFlights, /*page=*/1, /*pageCount=*/2);
  c.header = headerWithBadge(true);
  TEST_ASSERT_TRUE(c.header.alert_x >= 0);  // the badge is drawn
  const int16_t badgeCentre = static_cast<int16_t>(c.header.alert_x + kAlertBadgeDiameter / 2);
  const int16_t slackEdge =
      static_cast<int16_t>(c.header.alert_x + kAlertBadgeDiameter + kAlertBadgeTouchSlackX);
  ASSERT_ENUM_EQ(TapKind::OpenAlerts, routeTap(badgeCentre, 7, c).kind);
  ASSERT_ENUM_EQ(TapKind::OpenAlerts, routeTap(slackEdge, 7, c).kind);  // 8 px past it
  ASSERT_ENUM_EQ(TapKind::OpenAlerts, routeTap(badgeCentre, kAlertBadgeTouchBottom, c).kind);
}

// Strefa odznaki bez odznaki albo bez ustawionego domu nie otwiera ostrzeżeń
static void test_route_tap_badge_zone_without_badge_or_home_falls_through(void) {
  TapContext withBadge = context(kScreenFlights);
  withBadge.header = headerWithBadge(true);
  const int16_t x = static_cast<int16_t>(withBadge.header.alert_x + kAlertBadgeDiameter / 2);
  const int16_t y = 7;
  // The same place with a badge and home set does open it.
  ASSERT_ENUM_EQ(TapKind::OpenAlerts, routeTap(x, y, withBadge).kind);

  TapContext noBadge = withBadge;
  noBadge.header.alert_x = -1;
  ASSERT_ENUM_NE(TapKind::OpenAlerts, routeTap(x, y, noBadge).kind);

  TapContext noHome = withBadge;
  noHome.homeConfigured = false;
  ASSERT_ENUM_NE(TapKind::OpenAlerts, routeTap(x, y, noHome).kind);
}

// Pas pierwszego i ostatniego wiersza tabeli zmienia stronę, ale nie poza pierwszą i ostatnią
static void test_route_tap_pages_table_within_bounds(void) {
  struct Example {
    int pages;
    int page;
    bool top;
    TapKind want;
  };
  const Example examples[] = {
      {2, 0, true, TapKind::None},      {2, 1, true, TapKind::PagePrev},
      {2, 0, false, TapKind::PageNext}, {2, 1, false, TapKind::None},
      {0, 0, false, TapKind::None},     {1, 0, false, TapKind::None},
  };
  for (const Example &e : examples) {
    const int16_t y = e.top ? topStripY() : bottomStripY();
    const TapAction a = routeTap(160, y, context(kScreenFlights, e.page, e.pages));
    const std::string what = std::to_string(e.pages) + " pages, page " + std::to_string(e.page) +
                             (e.top ? ", top strip" : ", bottom strip");
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.want), static_cast<int>(a.kind),
                                  what.c_str());
  }
}

// Na ekranach TERAZ i RADAR dotknięcie treści nic nie robi
static void test_route_tap_content_of_flight_and_radar_does_nothing(void) {
  // The same tap on LOTY pages the table.
  ASSERT_ENUM_EQ(TapKind::PageNext,
                 routeTap(160, bottomStripY(), context(kScreenFlights, 0, 2)).kind);
  for (int screen : {kScreenFlight, kScreenRadar}) {
    const TapContext c = context(screen, 0, 2);
    ASSERT_ENUM_EQ(TapKind::None, routeTap(160, bottomStripY(), c).kind);
    ASSERT_ENUM_EQ(TapKind::None, routeTap(160, topStripY(), c).kind);
    ASSERT_ENUM_EQ(TapKind::None, routeTap(160, 120, c).kind);  // the middle
  }
}

// -- edge case: the nav touch band starts at y 210, not at the drawn 215
static void test_route_tap_nav_band_starts_at_210(void) {
  const TapContext c = context(kScreenRadar);
  ASSERT_ENUM_NE(TapKind::JumpTo, routeTap(50, 209, c).kind);
  const TapAction a = routeTap(50, 210, c);
  ASSERT_ENUM_EQ(TapKind::JumpTo, a.kind);
  TEST_ASSERT_EQUAL_INT(kScreenFlights, a.target);
}

// Nowy poll z krótszą listą cofa stronę tabeli na ostatnią istniejącą
static void test_table_pager_clamps_after_shorter_list(void) {
  TablePager pager;
  for (int i = 0; i < 3; ++i) pager.next(5);
  TEST_ASSERT_EQUAL_INT(3, pager.page());
  pager.clamp(2);
  TEST_ASSERT_EQUAL_INT(1, pager.page());
  pager.clamp(tablePageCount({}));  // an empty list
  TEST_ASSERT_EQUAL_INT(0, pager.page());
}

// -- edge case: prev / next stop at the first and the last page; tablePageCount() skips the featured row
static void test_table_pager_prev_next_stop_at_ends(void) {
  TablePager pager;
  TEST_ASSERT_FALSE(pager.prev());
  TEST_ASSERT_EQUAL_INT(0, pager.page());
  TEST_ASSERT_TRUE(pager.next(2));
  TEST_ASSERT_EQUAL_INT(1, pager.page());
  TEST_ASSERT_FALSE(pager.next(2));
  TEST_ASSERT_EQUAL_INT(1, pager.page());
  TEST_ASSERT_TRUE(pager.prev());
  TEST_ASSERT_EQUAL_INT(0, pager.page());
  TEST_ASSERT_FALSE(pager.next(0));
  TEST_ASSERT_EQUAL_INT(0, pager.page());

  std::vector<AircraftRow> rows;
  for (int i = 0; i < 1 + tableRowsPerPage(); ++i) rows.push_back(makeRow("LOT1", 5.0f + i));
  TEST_ASSERT_EQUAL_INT(1, tablePageCount(rows));  // featured + one full page
  rows.push_back(makeRow("LOT9", 50.0f));
  TEST_ASSERT_EQUAL_INT(2, tablePageCount(rows));
}

// Każda zmiana ekranu zeruje stronę tabeli i timer auto-cycle i zleca przerysowanie
static void test_screen_change_resets_page_and_auto_cycle_timer(void) {
  const ScreenChangeCause causes[] = {ScreenChangeCause::Touch, ScreenChangeCause::AutoCycle,
                                      ScreenChangeCause::Screenshot};
  for (ScreenChangeCause cause : causes) {
    const std::string what = "cause " + std::to_string(static_cast<int>(cause));
    ScreenChangeTracker tracker;
    tracker.begin(kScreenFlights, 1000);
    TablePager pager;
    pager.next(3);
    pager.next(3);
    const uint32_t now = 41001;  // 40 s after the last change
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(40001, tracker.autoCycleElapsedMs(now), what.c_str());
    RedrawEvents pending;
    tracker.onChange(kScreenRadar, cause, now, pager, pending);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pager.page(), what.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, tracker.autoCycleElapsedMs(now), what.c_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, tracker.autoCycleElapsedMs(now + 500), what.c_str());
    TEST_ASSERT_TRUE_MESSAGE(pending.screenChanged, what.c_str());
  }
}

// Zapis ostatniego ekranu: dotyk od razu, auto-cycle najwyżej co 10 min, zrzut nigdy, ten sam ekran wcale
static void test_screen_change_persists_only_a_new_screen(void) {
  ScreenChangeTracker tracker;
  TablePager pager;
  RedrawEvents pending;
  const uint32_t saved = 1001;
  tracker.begin(kScreenFlights, saved);  // screen 0 saved at boot

  const uint32_t touchAt = saved + 60000;  // a minute later
  TEST_ASSERT_TRUE(
      tracker.onChange(1, ScreenChangeCause::Touch, touchAt, pager, pending).persist);
  TEST_ASSERT_FALSE(  // a minute after that write
      tracker.onChange(2, ScreenChangeCause::AutoCycle, touchAt + 60000, pager, pending).persist);
  TEST_ASSERT_FALSE(
      tracker.onChange(0, ScreenChangeCause::Screenshot, touchAt + 61000, pager, pending).persist);
  TEST_ASSERT_TRUE(  // 10 min after the last write
      tracker
          .onChange(2, ScreenChangeCause::AutoCycle, touchAt + kScreenPersistMinIntervalMs, pager,
                    pending)
          .persist);
  // Screen 2 is saved now: going back to it writes nothing, not even by touch.
  TEST_ASSERT_FALSE(tracker
                        .onChange(1, ScreenChangeCause::Screenshot,
                                  touchAt + kScreenPersistMinIntervalMs + 1000, pager, pending)
                        .persist);
  TEST_ASSERT_FALSE(tracker
                        .onChange(2, ScreenChangeCause::Touch,
                                  touchAt + kScreenPersistMinIntervalMs + 2000, pager, pending)
                        .persist);
}

// -- edge case: the 10-min auto-cycle rule and the auto-cycle timer across a millis() rollover
static void test_screen_change_auto_cycle_persist_across_millis_wrap(void) {
  ScreenChangeTracker tracker;
  TablePager pager;
  RedrawEvents pending;
  const uint32_t start = 0xFFFFF001u;
  tracker.begin(kScreenFlights, start);
  TEST_ASSERT_EQUAL_UINT32(10000, tracker.autoCycleElapsedMs(start + 10000));  // wrapped past 0
  TEST_ASSERT_FALSE(tracker
                        .onChange(1, ScreenChangeCause::AutoCycle,
                                  start + kScreenPersistMinIntervalMs - 1, pager, pending)
                        .persist);
  TEST_ASSERT_TRUE(tracker
                       .onChange(2, ScreenChangeCause::AutoCycle,
                                 start + kScreenPersistMinIntervalMs, pager, pending)
                       .persist);
}

// Zegar w nagłówku odświeża się przy pierwszej synchronizacji NTP i przy zmianie minuty, nie częściej
static void test_minute_watch_first_sync_and_rollover(void) {
  MinuteWatch watch;
  TEST_ASSERT_TRUE(watch.changed(-1));   // the first pass draws the "--:--"
  TEST_ASSERT_FALSE(watch.changed(-1));  // still unsynced
  TEST_ASSERT_TRUE(watch.changed(29123456));  // the first NTP sync
  TEST_ASSERT_FALSE(watch.changed(29123456));
  TEST_ASSERT_FALSE(watch.changed(29123456));
  TEST_ASSERT_TRUE(watch.changed(29123457));  // the next minute
  TEST_ASSERT_FALSE(watch.changed(29123457));
}

// -- edge case: the NOW countdown ticks once a second, only on the NOW screen
static void test_countdown_due_once_per_second_only_on_flight_screen(void) {
  CountdownTick tick;
  TEST_ASSERT_TRUE(tick.due(kScreenFlight, 5000));
  TEST_ASSERT_FALSE(tick.due(kScreenFlight, 5999));
  TEST_ASSERT_EQUAL_UINT32(1, tick.msUntilNext(5999));
  TEST_ASSERT_TRUE(tick.due(kScreenFlight, 6000));
  TEST_ASSERT_FALSE(tick.due(kScreenFlights, 9000));  // other screens never tick
  TEST_ASSERT_FALSE(tick.due(kScreenRadar, 9000));
  TEST_ASSERT_TRUE(tick.due(kScreenFlight, 9000));
}

// Auto-cycle nie przełącza wygaszonego ekranu, ekranu bez ustawionego domu ani gdy jest wyłączony
static void test_auto_cycle_never_on_dark_screen_or_without_home(void) {
  AutoCycleInput on;
  on.enabled = true;
  on.homeConfigured = true;
  on.screenOff = false;
  on.currentScreen = kScreenRadar;
  on.intervalS = 15;
  on.elapsedMs = 15000;  // the interval has passed, nothing holds it
  TEST_ASSERT_TRUE(shouldAutoCycleNow(on));

  AutoCycleInput off = on;
  off.enabled = false;
  TEST_ASSERT_FALSE(shouldAutoCycleNow(off));
  AutoCycleInput noHome = on;
  noHome.homeConfigured = false;
  TEST_ASSERT_FALSE(shouldAutoCycleNow(noHome));
  AutoCycleInput dark = on;
  dark.screenOff = true;
  TEST_ASSERT_FALSE(shouldAutoCycleNow(dark));

  // ...and the existing rules still apply: not before the interval, not
  // while the NOW screen holds an imminent pass.
  AutoCycleInput early = on;
  early.elapsedMs = 14999;
  TEST_ASSERT_FALSE(shouldAutoCycleNow(early));
  AutoCycleInput held = on;
  held.currentScreen = kScreenFlight;
  held.cpaFound = true;
  held.tCpaSeconds = 3.0f;
  TEST_ASSERT_FALSE(shouldAutoCycleNow(held));
}

// Po 60 s ciszy rdzenia 0 nagłówek pokazuje błąd sieci, a pusty ekran mówi dlaczego
static void test_shown_health_network_error_after_silence_marks_empty_screen(void) {
  const uint32_t beat = 1000;
  const uint32_t now = beat + kNetStallLimitMs + 1;  // 60 001 ms of silence

  ShownHealthStep empty = shownHealthStep(OpenSkyHealth::Ok, OpenSkyHealth::Ok, now, beat, true);
  ASSERT_ENUM_EQ(OpenSkyHealth::NetworkError, empty.shown);
  TEST_ASSERT_TRUE(empty.healthChanged);
  TEST_ASSERT_TRUE(empty.newPoll);
  TEST_ASSERT_TRUE(empty.stalled);

  ShownHealthStep full = shownHealthStep(OpenSkyHealth::Ok, OpenSkyHealth::Ok, now, beat, false);
  ASSERT_ENUM_EQ(OpenSkyHealth::NetworkError, full.shown);
  TEST_ASSERT_TRUE(full.healthChanged);
  TEST_ASSERT_FALSE(full.newPoll);

  // Exactly 60 s is not silence yet; the same health again changes nothing.
  ShownHealthStep fresh =
      shownHealthStep(OpenSkyHealth::Ok, OpenSkyHealth::Ok, beat + kNetStallLimitMs, beat, true);
  ASSERT_ENUM_EQ(OpenSkyHealth::Ok, fresh.shown);
  TEST_ASSERT_FALSE(fresh.healthChanged);
  TEST_ASSERT_FALSE(fresh.newPoll);
}

// -- edge case: one tap per press; a swallowed press needs a new one
static void test_touch_edge_one_tap_per_press_and_swallow(void) {
  TouchEdge edge;
  TEST_ASSERT_FALSE(edge.tap(false));
  TEST_ASSERT_TRUE(edge.tap(true));
  TEST_ASSERT_FALSE(edge.tap(true));  // still the same press
  TEST_ASSERT_FALSE(edge.tap(false));
  TEST_ASSERT_TRUE(edge.tap(true));
  edge.swallow();
  TEST_ASSERT_FALSE(edge.tap(true));  // the swallowed finger is still down
  TEST_ASSERT_FALSE(edge.tap(false));
  TEST_ASSERT_TRUE(edge.tap(true));
  edge.tap(false);
  edge.swallow();  // swallowed with no finger down: the next press still counts
  TEST_ASSERT_FALSE(edge.tap(false));
  TEST_ASSERT_TRUE(edge.tap(true));
}

}  // namespace

void run_ui_flow_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_route_tap_bottom_nav_jumps_to_its_screen);
  RUN_TEST(test_route_tap_alert_badge_comes_first);
  RUN_TEST(test_route_tap_badge_zone_without_badge_or_home_falls_through);
  RUN_TEST(test_route_tap_pages_table_within_bounds);
  RUN_TEST(test_route_tap_content_of_flight_and_radar_does_nothing);
  RUN_TEST(test_route_tap_nav_band_starts_at_210);
  RUN_TEST(test_table_pager_clamps_after_shorter_list);
  RUN_TEST(test_table_pager_prev_next_stop_at_ends);
  RUN_TEST(test_screen_change_resets_page_and_auto_cycle_timer);
  RUN_TEST(test_screen_change_persists_only_a_new_screen);
  RUN_TEST(test_screen_change_auto_cycle_persist_across_millis_wrap);
  RUN_TEST(test_minute_watch_first_sync_and_rollover);
  RUN_TEST(test_countdown_due_once_per_second_only_on_flight_screen);
  RUN_TEST(test_auto_cycle_never_on_dark_screen_or_without_home);
  RUN_TEST(test_shown_health_network_error_after_silence_marks_empty_screen);
  RUN_TEST(test_touch_edge_one_tap_per_press_and_swallow);
}
