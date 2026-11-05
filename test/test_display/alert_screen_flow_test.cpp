// alert_screen_flow: the device-alert message screen behind the header's "!" as the UI loop runs it. docs/architecture.md "Device alerts", spec 08.
#include <unity.h>

#include "display/services/alert_screen_flow.h"

#include <cstdint>

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

// Zamknięcie ekranu ostrzeżeń wraca do tego samego ekranu i strony z pełnym przerysowaniem i świeżym auto-cycle
static void test_alert_flow_close_returns_with_full_redraw(void) {
  {  // 15 s run out
    AlertScreenFlow flow;
    flow.open(1000, oneAlert());
    const AlertFlowStep held = flow.step(at(1000 + 14999, true, 10, 10), oneAlert());
    TEST_ASSERT_FALSE(held.close);
    TEST_ASSERT_TRUE(held.swallowTouch);  // a finger on the open screen is no tap below
    const AlertFlowStep r = flow.step(at(1000 + kAlertScreenTimeoutMs), oneAlert());
    TEST_ASSERT_TRUE(r.close);
    TEST_ASSERT_TRUE(r.swallowTouch);
    TEST_ASSERT_FALSE(flow.isOpen());
  }
  {  // a fresh tap on CLOSE
    AlertScreenFlow flow;
    flow.open(1000, oneAlert());
    flow.step(at(1100), oneAlert());  // the opening finger lifted
    const AlertFlowStep r = flow.step(tapClose(1500, 1), oneAlert());
    TEST_ASSERT_TRUE(r.close);
    TEST_ASSERT_TRUE(r.swallowTouch);  // the closing tap isn't one for the screen below
    TEST_ASSERT_FALSE(flow.isOpen());
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
