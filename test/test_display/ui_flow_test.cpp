// ui_flow: taps, table pages, screen changes, clock minute, countdown, auto-cycle, shown health. docs/screens.md "Screen navigation", "Auto screen cycling".
#include <unity.h>

#include "display/services/ui_flow.h"

#include <cstdint>
#include <memory>
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

  ShownHealthStep empty = shownHealthStep(FeedHealth::Ok, FeedHealth::Ok, now, beat, true);
  ASSERT_ENUM_EQ(FeedHealth::NetworkError, empty.shown);
  TEST_ASSERT_TRUE(empty.healthChanged);
  TEST_ASSERT_TRUE(empty.newPoll);
  TEST_ASSERT_TRUE(empty.stalled);

  ShownHealthStep full = shownHealthStep(FeedHealth::Ok, FeedHealth::Ok, now, beat, false);
  ASSERT_ENUM_EQ(FeedHealth::NetworkError, full.shown);
  TEST_ASSERT_TRUE(full.healthChanged);
  TEST_ASSERT_FALSE(full.newPoll);

  // Exactly 60 s is not silence yet; the same health again changes nothing.
  ShownHealthStep fresh =
      shownHealthStep(FeedHealth::Ok, FeedHealth::Ok, beat + kNetStallLimitMs, beat, true);
  ASSERT_ENUM_EQ(FeedHealth::Ok, fresh.shown);
  TEST_ASSERT_FALSE(fresh.healthChanged);
  TEST_ASSERT_FALSE(fresh.newPoll);
}

// ---- What the screens show (helpers main.cpp reads NetState with) ------------------

// A snapshot after a poll: three rows, the second one NOW, its CPA 40 s ahead at `polledAt`.
static NetState polledState(uint32_t polledAt) {
  auto data = std::make_shared<AirspaceSnapshot>();
  data->rows = {makeRow("LOT1", 3.0f), makeRow("LOT2", 5.0f), makeRow("LOT3", 9.0f)};
  data->nowRowIndex = 1;
  data->cpa.found = true;
  data->cpa.t_cpa_seconds = 40.0f;
  data->polledAtMs = polledAt;
  Config cfg = makeConfig();
  cfg.home.radius_deg = 1.5f;
  NetState s;
  s.data = data;
  s.cfg = std::make_shared<const Config>(cfg);
  return s;
}

// -- edge case: before the first poll there are no rows and no CPA; after it, the CPA ages
static void test_net_rows_and_cpa_before_and_after_first_poll(void) {
  NetState empty;
  empty.cfg = std::make_shared<const Config>(makeConfig());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(netRows(empty).size()));
  TEST_ASSERT_FALSE(netCpaAt(empty, 5000).found);

  const NetState s = polledState(0xFFFFF000u);  // the extrapolation wraps with millis()
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(netRows(s).size()));
  const CpaPrediction cpa = netCpaAt(s, 0xFFFFF000u + 10000);
  TEST_ASSERT_TRUE(cpa.found);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, cpa.t_cpa_seconds);
}

// -- edge case: the view carries the snapshot, the UI's screen / page / shown health and the clock
static void test_make_ui_view_reads_the_snapshot(void) {
  NetState s = polledState(1000);
  s.alerts.set(AlertId::OpenSkyLoginFailed);
  const UiView v =
      makeUiView(s, kScreenRadar, 2, FeedHealth::NetworkError, 21000, 1760770000, true);
  TEST_ASSERT_EQUAL_INT(kScreenRadar, v.screen);
  TEST_ASSERT_EQUAL_INT(2, v.page);
  TEST_ASSERT_EQUAL_PTR(&s.data->rows, v.rows);
  TEST_ASSERT_TRUE(v.hasData);
  TEST_ASSERT_EQUAL_INT(1, v.nowRowIndex);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, v.cpa.t_cpa_seconds);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, v.radiusDeg);
  ASSERT_ENUM_EQ(FeedHealth::NetworkError, v.health);
  ASSERT_ENUM_EQ(s.alerts.displaySeverity(), v.alertBadge);
  ASSERT_ENUM_NE(AlertSeverity::None, v.alertBadge);
  TEST_ASSERT_EQUAL_INT64(1760770000, static_cast<int64_t>(v.local));
  TEST_ASSERT_TRUE(v.synced);
  TEST_ASSERT_TRUE(v.homeConfigured);

  NetState first;  // setup()'s first snapshot: the settings only
  first.cfg = std::make_shared<const Config>(defaultConfig());
  const UiView w = makeUiView(first, kScreenFlights, 0, FeedHealth::Ok, 100, 0, false);
  TEST_ASSERT_FALSE(w.hasData);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(w.rows->size()));
  TEST_ASSERT_EQUAL_INT(-1, w.nowRowIndex);
  TEST_ASSERT_FALSE(w.cpa.found);
  TEST_ASSERT_FALSE(w.homeConfigured);
  ASSERT_ENUM_EQ(AlertSeverity::None, w.alertBadge);
}

// -- edge case: a tap's context comes from the view: the pages of its rows, the header with its "!"
static void test_tap_context_of_view_has_pages_and_header(void) {
  NetState s = polledState(1000);
  auto data = std::make_shared<AirspaceSnapshot>(*s.data);
  for (int i = 0; i < tableRowsPerPage(); ++i) data->rows.push_back(makeRow("LOT9", 20.0f + i));
  s.data = data;
  s.alerts.set(AlertId::OpenSkyLoginFailed);
  const UiView v = makeUiView(s, kScreenFlights, 1, FeedHealth::Ok, 1000, 0, false);
  const TapContext c = tapContextOf(v, kW, kH, Language::EN);
  TEST_ASSERT_EQUAL_INT(kScreenFlights, c.currentScreen);
  TEST_ASSERT_EQUAL_INT(1, c.page);
  TEST_ASSERT_EQUAL_INT(2, c.pageCount);
  TEST_ASSERT_EQUAL_INT(tablePageCount(data->rows), c.pageCount);
  TEST_ASSERT_TRUE(c.homeConfigured);
  const HeaderZones want = statusBarZonesNow(kW, static_cast<int>(data->rows.size()), true,
                                             Language::EN, 0, false, true);
  TEST_ASSERT_EQUAL_INT(want.alert_x, c.header.alert_x);
  TEST_ASSERT_TRUE(c.header.alert_x >= 0);
  TEST_ASSERT_EQUAL_INT(kW, c.screenW);
  TEST_ASSERT_EQUAL_INT(kH, c.screenH);
}

// -- edge case: the auto-cycle input takes the settings and the CPA at that moment
static void test_auto_cycle_input_from_settings_and_cpa(void) {
  NetState s = polledState(1000);
  Config cfg = *s.cfg;
  cfg.auto_cycle.enabled = true;
  cfg.auto_cycle.interval_s = 25;
  s.cfg = std::make_shared<const Config>(cfg);
  const AutoCycleInput in = autoCycleInputAt(s, kScreenFlight, true, 7000, 11000);
  TEST_ASSERT_TRUE(in.enabled);
  TEST_ASSERT_TRUE(in.homeConfigured);
  TEST_ASSERT_TRUE(in.screenOff);
  TEST_ASSERT_EQUAL_INT(kScreenFlight, in.currentScreen);
  TEST_ASSERT_TRUE(in.cpaFound);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, in.tCpaSeconds);
  TEST_ASSERT_EQUAL_UINT32(7000, in.elapsedMs);
  TEST_ASSERT_EQUAL_UINT32(25, in.intervalS);
}

// ---- UiFlow: the bookkeeping of one UI pass together ----------------------------------

// -- a snapshot's events go into pending, except its health; a new poll clamps the page
static void test_ui_flow_takes_net_events_but_not_their_health(void) {
  UiFlow ui;
  for (int i = 0; i < 3; ++i) ui.pager.next(5);
  RedrawEvents e;
  e.firstDraw = e.languageChanged = e.minuteChanged = e.alertsChanged = true;
  e.healthChanged = true;  // the header's health is the shown one, not net_task's
  ui.takeNetEvents(e, polledState(1000));
  TEST_ASSERT_TRUE(ui.pending.firstDraw);
  TEST_ASSERT_TRUE(ui.pending.languageChanged);
  TEST_ASSERT_TRUE(ui.pending.minuteChanged);
  TEST_ASSERT_TRUE(ui.pending.alertsChanged);
  TEST_ASSERT_FALSE(ui.pending.healthChanged);
  TEST_ASSERT_FALSE(ui.pending.newPoll);
  TEST_ASSERT_EQUAL_INT(3, ui.pager.page());  // no new poll: the page stays

  RedrawEvents poll;
  poll.newPoll = true;
  ui.takeNetEvents(poll, polledState(2000));  // 3 rows: one page at most
  TEST_ASSERT_TRUE(ui.pending.newPoll);
  TEST_ASSERT_EQUAL_INT(0, ui.pager.page());
}

// -- 60 s of silence: NetworkError in the header once, the empty screen says why
static void test_ui_flow_health_silence_logged_once(void) {
  UiFlow ui;
  NetState empty;
  empty.cfg = std::make_shared<const Config>(makeConfig());
  const uint32_t now = 1000 + kNetStallLimitMs + 1;
  TEST_ASSERT_TRUE(ui.updateHealth(empty, now, 1000));
  ASSERT_ENUM_EQ(FeedHealth::NetworkError, ui.shownHealth);
  TEST_ASSERT_TRUE(ui.pending.healthChanged);
  TEST_ASSERT_TRUE(ui.pending.newPoll);
  ui.pending = RedrawEvents{};
  TEST_ASSERT_FALSE(ui.updateHealth(empty, now + 20, 1000));  // still silent: no change
  TEST_ASSERT_FALSE(ui.pending.healthChanged);
  TEST_ASSERT_FALSE(ui.updateHealth(empty, now + 40, now + 30));  // back: Ok, not "silent"
  ASSERT_ENUM_EQ(FeedHealth::Ok, ui.shownHealth);
  TEST_ASSERT_TRUE(ui.pending.healthChanged);
}

// -- a screen change: page 0, the redraw, a fresh auto-cycle, last_screen saved by touch
static void test_ui_flow_screen_changed_resets_and_persists(void) {
  UiFlow ui;
  ui.screenChanges.begin(kScreenFlights, 1000);
  ui.pager.next(3);
  TEST_ASSERT_TRUE(ui.screenChanged(kScreenRadar, ScreenChangeCause::Touch, 9000));
  TEST_ASSERT_EQUAL_INT(0, ui.pager.page());
  TEST_ASSERT_TRUE(ui.pending.screenChanged);
  TEST_ASSERT_EQUAL_UINT32(0, ui.screenChanges.autoCycleElapsedMs(9000));
  TEST_ASSERT_FALSE(ui.screenChanged(kScreenFlight, ScreenChangeCause::Screenshot, 9500));
}

// -- a service outcome: pending cleared before firstDraw, auto-cycle restarted, touch swallowed
static void test_ui_flow_applies_service_outcome_in_order(void) {
  UiFlow ui;
  ui.screenChanges.begin(kScreenFlights, 0);
  ui.pending.newPoll = true;
  ServiceOutcome o;
  o.clearPending = true;
  o.firstDraw = true;
  o.restartAutoCycle = true;
  o.swallowTouch = true;
  TEST_ASSERT_FALSE(ui.apply(o, 5000));
  TEST_ASSERT_FALSE(ui.pending.newPoll);
  TEST_ASSERT_TRUE(ui.pending.firstDraw);
  TEST_ASSERT_EQUAL_UINT32(100, ui.screenChanges.autoCycleElapsedMs(5100));
  TEST_ASSERT_FALSE(ui.touch.tap(true));  // the swallowed finger
  ServiceOutcome owns;
  owns.ownsDisplay = true;
  TEST_ASSERT_TRUE(ui.apply(owns, 6000));
  TEST_ASSERT_TRUE(ui.pending.firstDraw);  // nothing cleared without clearPending
}

// -- a page tap moves the page within its bounds and redraws the table only when it moved
static void test_ui_flow_page_tap_redraws_only_when_moved(void) {
  UiFlow ui;
  ui.pageTap(TapKind::PagePrev, 2);
  TEST_ASSERT_FALSE(ui.pending.pageChanged);
  ui.pageTap(TapKind::PageNext, 2);
  TEST_ASSERT_TRUE(ui.pending.pageChanged);
  TEST_ASSERT_EQUAL_INT(1, ui.pager.page());
  ui.pending = RedrawEvents{};
  ui.pageTap(TapKind::PageNext, 2);
  TEST_ASSERT_FALSE(ui.pending.pageChanged);
  ui.pageTap(TapKind::JumpTo, 2);  // not a page tap
  TEST_ASSERT_EQUAL_INT(1, ui.pager.page());
}

// -- a normal pass's clocks: the countdown second, the minute, auto-cycle when due
static void test_ui_flow_tick_countdown_minute_and_auto_cycle(void) {
  NetState s = polledState(1000);
  Config cfg = *s.cfg;
  cfg.auto_cycle.enabled = true;
  cfg.auto_cycle.interval_s = 15;
  s.cfg = std::make_shared<const Config>(cfg);
  UiFlow ui;
  ui.screenChanges.begin(kScreenRadar, 1000);
  TEST_ASSERT_FALSE(ui.tick(s, kScreenRadar, 2000, -1, false));
  TEST_ASSERT_TRUE(ui.pending.minuteChanged);  // the first pass draws the clock
  TEST_ASSERT_FALSE(ui.pending.countdownTick);  // not the NOW screen
  ui.pending = RedrawEvents{};
  TEST_ASSERT_FALSE(ui.tick(s, kScreenFlight, 3000, -1, false));
  TEST_ASSERT_TRUE(ui.pending.countdownTick);
  TEST_ASSERT_FALSE(ui.pending.minuteChanged);
  TEST_ASSERT_TRUE(ui.tick(s, kScreenRadar, 16000, -1, false));  // 15 s since the change
  TEST_ASSERT_FALSE(ui.tick(s, kScreenRadar, 16000, -1, true));  // dark screen: never
}

// -- the redraw mask is computeRedrawMask()'s, and nothing is pending afterwards
static void test_ui_flow_take_redraw_mask_clears_pending(void) {
  UiFlow ui;
  ui.pending.newPoll = true;
  RedrawEvents same;
  same.newPoll = true;
  const uint8_t want = computeRedrawMask(same, kScreenFlights);
  TEST_ASSERT_EQUAL_UINT8(want, ui.takeRedrawMask(kScreenFlights));
  TEST_ASSERT_EQUAL_UINT8(0, ui.takeRedrawMask(kScreenFlights));
  TEST_ASSERT_FALSE(ui.pending.newPoll);
}

// -- the countdown wakes the loop only on the NOW screen
static void test_ui_flow_wake_times_only_on_now_screen(void) {
  UiFlow ui;
  ui.countdown.due(kScreenFlight, 5000);
  UiWake other;
  ui.addWakeTimes(other, kScreenRadar, 5400);
  TEST_ASSERT_EQUAL_UINT32(kNoDeadline, other.untilCountdownMs);
  UiWake now;
  ui.addWakeTimes(now, kScreenFlight, 5400);
  TEST_ASSERT_EQUAL_UINT32(600, now.untilCountdownMs);
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
  RUN_TEST(test_net_rows_and_cpa_before_and_after_first_poll);
  RUN_TEST(test_make_ui_view_reads_the_snapshot);
  RUN_TEST(test_tap_context_of_view_has_pages_and_header);
  RUN_TEST(test_auto_cycle_input_from_settings_and_cpa);
  RUN_TEST(test_ui_flow_takes_net_events_but_not_their_health);
  RUN_TEST(test_ui_flow_health_silence_logged_once);
  RUN_TEST(test_ui_flow_screen_changed_resets_and_persists);
  RUN_TEST(test_ui_flow_applies_service_outcome_in_order);
  RUN_TEST(test_ui_flow_page_tap_redraws_only_when_moved);
  RUN_TEST(test_ui_flow_tick_countdown_minute_and_auto_cycle);
  RUN_TEST(test_ui_flow_take_redraw_mask_clears_pending);
  RUN_TEST(test_ui_flow_wake_times_only_on_now_screen);
}
