// alert_screen_flow: the device-alert message screen behind the header's "!" as the UI loop runs it. docs/architecture.md "Device alerts", spec 08.
#include <unity.h>

#include "display/services/alert_screen_flow.h"

#include <cstdint>

#include "display/core/screen_nav.h"                // kScreenRadar
#include "display/services/service_screen_flows.h"  // the wiring the UI loop runs
#include "display/services/ui_flow.h"
#include "test_support.h"

namespace {

static AlertSet oneAlert() {
  AlertSet a;
  a.set(AlertId::OpenSkyLoginFailed);
  return a;
}

static AlertSet twoAlerts() {
  AlertSet a = oneAlert();
  a.set(AlertId::OpenSkyBudgetOverLimit);
  return a;
}

static AlertScreenInput at(uint32_t nowMs, bool pressed = false, int16_t x = 0, int16_t y = 0) {
  AlertScreenInput in;
  in.nowMs = nowMs;
  in.pressed = pressed;
  in.x = x;
  in.y = y;
  return in;
}

// A press in the middle of CLOSE with `count` alerts listed.
static AlertScreenInput tapClose(uint32_t nowMs, int count) {
  const Rect b = alertScreenLayout(320, 240, count).button;
  return at(nowMs, true, static_cast<int16_t>(b.x + b.w / 2), static_cast<int16_t>(b.y + b.h / 2));
}

// Zmiana listy ostrzeżeń przy otwartym ekranie przerysowuje listę, a odliczanie biegnie dalej
static void test_alert_flow_redraws_list_when_alerts_change(void) {
  AlertScreenFlow flow;
  TEST_ASSERT_EQUAL_UINT32(15, flow.open(0, oneAlert()));
  TEST_ASSERT_FALSE(flow.step(at(1500), oneAlert()).redrawList);  // the same alerts
  const AlertFlowStep r = flow.step(at(3000), twoAlerts());
  TEST_ASSERT_TRUE(r.redrawList);
  TEST_ASSERT_EQUAL_UINT32(12, r.secondsLeft);  // not 15 again
  TEST_ASSERT_FALSE(r.close);
  TEST_ASSERT_TRUE(flow.isOpen());
  TEST_ASSERT_FALSE(flow.step(at(3100), twoAlerts()).redrawList);  // drawn once
}

// The UI loop's side of the alert screen, wired as main.cpp wires it: the
// Radar screen on table page 1 (any page other than the first), the message
// opened at `openMs`, every pass's step through ServiceScreenFlows into UiFlow.
struct AlertLoop {
  ServiceScreenFlows services{[](const NetCommand &) { return true; }};
  UiFlow ui;
  uint32_t openMs;

  explicit AlertLoop(uint32_t openAt) : openMs(openAt) {
    ui.screenChanges.begin(kScreenRadar, 0);
    ui.pager.next(3);
    services.alertFlow().open(openMs, oneAlert());
    ui.pending = RedrawEvents{};
  }
  // One pass; true = the alert screen owns the display. The touch edge last
  // saw no finger, so only a swallow makes the next press no tap.
  bool pass(const AlertScreenInput &in) {
    ui.touch.tap(false);
    return ui.apply(services.afterAlertStep(services.alertFlow().step(in, oneAlert())), in.nowMs);
  }
};

// The Then once the message has closed at `closedMs`: full redraw, a fresh
// auto-cycle interval, the same screen and page, the closing finger no tap.
static void assertBackInFull(AlertLoop &l, uint32_t closedMs) {
  TEST_ASSERT_FALSE(l.services.alertFlow().isOpen());
  TEST_ASSERT_TRUE(l.ui.pending.firstDraw);
  TEST_ASSERT_EQUAL_UINT32(0, l.ui.screenChanges.autoCycleElapsedMs(closedMs));
  TEST_ASSERT_FALSE(l.ui.pending.screenChanged);  // the screen below is the one shown before
  TEST_ASSERT_FALSE(l.ui.pending.pageChanged);
  TEST_ASSERT_EQUAL_INT(1, l.ui.pager.page());
  TEST_ASSERT_FALSE(l.ui.touch.tap(true));  // the closing pass's finger is no tap below
}

// Zamknięcie ekranu ostrzeżeń wraca do tego samego ekranu i strony z pełnym przerysowaniem i świeżym auto-cycle
static void test_alert_flow_close_returns_with_full_redraw(void) {
  {  // 15 s run out
    AlertLoop l(1000);
    TEST_ASSERT_TRUE(l.pass(at(1000 + 14999, true, 10, 10)));
    TEST_ASSERT_FALSE(l.ui.pending.firstDraw);  // still open: nothing drawn below
    TEST_ASSERT_FALSE(l.ui.touch.tap(true));    // a finger on the open screen is no tap below
    const uint32_t closedMs = 1000 + kAlertScreenTimeoutMs;
    TEST_ASSERT_FALSE(l.pass(at(closedMs)));
    assertBackInFull(l, closedMs);
  }
  {  // a fresh tap on CLOSE
    AlertLoop l(1000);
    TEST_ASSERT_TRUE(l.pass(at(1100)));  // the opening finger lifted
    TEST_ASSERT_FALSE(l.pass(tapClose(1500, 1)));
    assertBackInFull(l, 1500);
  }
}

// Ekran aktualizacji zamyka ekran ostrzeżeń bez przerysowania (ekran OTA ma pierwszeństwo)
static void test_alert_flow_closed_by_ota_screen(void) {
  AlertScreenFlow flow;
  flow.open(0, oneAlert());
  TEST_ASSERT_TRUE(flow.isOpen());
  flow.closeQuietly();
  TEST_ASSERT_FALSE(flow.isOpen());
  // Nothing left to draw: a later pass with other alerts asks for nothing.
  const AlertFlowStep r = flow.step(at(2000), twoAlerts());
  TEST_ASSERT_FALSE(r.redrawList);
  TEST_ASSERT_FALSE(r.redrawTimer);
  TEST_ASSERT_FALSE(r.close);
}

// -- edge case: the CLOSE countdown is redrawn only when its second changes
static void test_alert_flow_timer_redraw_only_when_second_changes(void) {
  AlertScreenFlow flow;
  flow.open(0, oneAlert());
  TEST_ASSERT_EQUAL_UINT32(1000, flow.msToNextSecond(0));
  TEST_ASSERT_FALSE(flow.step(at(500), oneAlert()).redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(500, flow.msToNextSecond(500));
  const AlertFlowStep tick = flow.step(at(1000), oneAlert());
  TEST_ASSERT_TRUE(tick.redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(14, tick.secondsLeft);
  TEST_ASSERT_FALSE(flow.step(at(1500), oneAlert()).redrawTimer);
}

// -- edge case: the screen closes as soon as no display alert is left
static void test_alert_flow_closes_when_last_display_alert_goes(void) {
  AlertScreenFlow flow;
  flow.open(0, oneAlert());
  AlertScreenInput in = at(3000);
  in.anyDisplayAlerts = false;
  const AlertFlowStep r = flow.step(in, AlertSet{});
  TEST_ASSERT_TRUE(r.close);
  TEST_ASSERT_FALSE(r.redrawList);  // closing, not redrawing an empty list
  TEST_ASSERT_FALSE(flow.isOpen());
}

}  // namespace

void run_alert_screen_flow_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_alert_flow_redraws_list_when_alerts_change);
  RUN_TEST(test_alert_flow_close_returns_with_full_redraw);
  RUN_TEST(test_alert_flow_closed_by_ota_screen);
  RUN_TEST(test_alert_flow_timer_redraw_only_when_second_changes);
  RUN_TEST(test_alert_flow_closes_when_last_display_alert_goes);
}
