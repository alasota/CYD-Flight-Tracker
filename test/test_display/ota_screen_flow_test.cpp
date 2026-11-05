// ota_screen_flow: the firmware-update screen drawn from NetState::ota. docs/architecture.md "Firmware updates (OTA) & panel password".
#include <unity.h>

#include "display/services/ota_screen_flow.h"

#include <cstdint>
#include <string>

#include "display/services/alert_screen_flow.h"
#include "test_support.h"

namespace {

using Phase = OtaProgress::Phase;

static OtaProgress progress(uint32_t seq, Phase phase, uint32_t written = 0,
                            uint32_t total = 1300000) {
  OtaProgress o;
  o.seq = seq;
  o.phase = phase;
  o.written = written;
  o.total = total;
  o.newVersion = "v1.3.0";
  return o;
}

// The update failed at `t`: its result drawn, the screen held.
static void failAt(OtaScreenFlow &flow, uint32_t seq, uint32_t t) {
  flow.step(progress(seq, Phase::Running), false, t - 2000);
  const OtaScreenStep r = flow.step(progress(seq + 1, Phase::Failed), false, t);
  ASSERT_ENUM_EQ(OtaScreenResult::Failed, r.result);
  TEST_ASSERT_TRUE(r.ownsDisplay);
}

// Pierwszy postęp aktualizacji rysuje ekran OTA w pełnej jasności, kolejne tylko pasek postępu
static void test_ota_first_progress_draws_screen_then_only_bar(void) {
  OtaScreenFlow flow;
  const OtaScreenStep first = flow.step(progress(1, Phase::Running, 0), false, 1000);
  TEST_ASSERT_TRUE(first.fullBrightnessAndFrame);
  TEST_ASSERT_TRUE(first.drawProgress);
  TEST_ASSERT_TRUE(first.ownsDisplay);
  const OtaScreenStep second = flow.step(progress(2, Phase::Running, 400000), false, 1500);
  TEST_ASSERT_FALSE(second.fullBrightnessAndFrame);
  TEST_ASSERT_TRUE(second.drawProgress);
  ASSERT_ENUM_EQ(OtaScreenResult::None, second.result);
  TEST_ASSERT_TRUE(second.ownsDisplay);
}

// Ekran OTA trzyma wyświetlacz w trakcie, po sukcesie i przed restartem, a zamyka ekran ostrzeżeń
static void test_ota_owns_display_while_running_done_or_restarting(void) {
  struct Example {
    Phase phase;
    bool restart;
    bool owns;
  };
  const Example examples[] = {{Phase::Running, false, true},
                              {Phase::Done, false, true},
                              {Phase::Idle, true, true},
                              {Phase::Idle, false, false}};
  for (const Example &e : examples) {
    const std::string what = "phase " + std::to_string(static_cast<int>(e.phase)) +
                             ", restartPending " + (e.restart ? "true" : "false");
    OtaScreenFlow flow;
    AlertSet alerts;
    alerts.set(AlertId::OpenSkyLoginFailed);
    AlertScreenFlow alertScreen;
    alertScreen.open(500, alerts);
    TEST_ASSERT_TRUE_MESSAGE(alertScreen.isOpen(), what.c_str());

    const uint32_t seq = e.phase == Phase::Idle ? 0 : 1;  // Idle: no progress reported yet
    const OtaScreenStep r = flow.step(progress(seq, e.phase, 1000), e.restart, 1000);
    TEST_ASSERT_EQUAL_MESSAGE(e.owns, r.ownsDisplay, what.c_str());
    // main.cpp: while the OTA screen owns the display the alert screen closes.
    if (r.ownsDisplay) alertScreen.closeQuietly();
    TEST_ASSERT_EQUAL_MESSAGE(!e.owns, alertScreen.isOpen(), what.c_str());
  }
}

// Nieudana aktualizacja zostaje na ekranie dokładnie 5 s, potem wracają zwykłe ekrany
static void test_ota_failure_shown_5_s_then_back_to_screens(void) {
  OtaScreenFlow flow;
  const uint32_t t = 10001;
  failAt(flow, 3, t);
  const OtaProgress failed = progress(4, Phase::Failed);
  const OtaScreenStep held = flow.step(failed, false, t + 4999);
  TEST_ASSERT_TRUE(held.ownsDisplay);
  TEST_ASSERT_FALSE(held.backToScreens);
  TEST_ASSERT_TRUE(flow.holdsBacklight(failed, false));
  const OtaScreenStep back = flow.step(failed, false, t + kOtaFailedShowMs);
  TEST_ASSERT_FALSE(back.ownsDisplay);
  TEST_ASSERT_TRUE(back.backToScreens);
  TEST_ASSERT_FALSE(flow.holdsBacklight(failed, false));
  // Only once: the next pass is a normal one.
  const OtaScreenStep after = flow.step(failed, false, t + kOtaFailedShowMs + 20);
  TEST_ASSERT_FALSE(after.ownsDisplay);
  TEST_ASSERT_FALSE(after.backToScreens);
}

// 5 s po błędzie liczy się poprawnie także przez przekręcenie millis()
static void test_ota_failure_timer_survives_millis_wrap(void) {
  OtaScreenFlow flow;
  const uint32_t t = 0xFFFFF001u;
  failAt(flow, 3, t);
  const OtaProgress failed = progress(4, Phase::Failed);
  TEST_ASSERT_TRUE(flow.step(failed, false, t + 4999).ownsDisplay);  // wrapped past 0
  const OtaScreenStep back = flow.step(failed, false, t + 5000);
  TEST_ASSERT_FALSE(back.ownsDisplay);
  TEST_ASSERT_TRUE(back.backToScreens);
}

// Ponowna próba po błędzie rysuje ekran OTA od nowa
static void test_ota_retry_after_failure_redraws_screen(void) {
  OtaScreenFlow flow;
  const uint32_t t = 20001;
  failAt(flow, 3, t);
  TEST_ASSERT_TRUE(flow.step(progress(4, Phase::Failed), false, t + 5000).backToScreens);
  const OtaScreenStep retry = flow.step(progress(5, Phase::Running, 0), false, t + 9000);
  TEST_ASSERT_TRUE(retry.fullBrightnessAndFrame);
  TEST_ASSERT_TRUE(retry.drawProgress);
  TEST_ASSERT_TRUE(retry.ownsDisplay);
}

// Bez nowej sekwencji postępu ekran OTA niczego nie rysuje
static void test_ota_same_seq_draws_nothing(void) {
  OtaScreenFlow flow;
  const OtaProgress o = progress(7, Phase::Running, 1000);
  const OtaScreenStep first = flow.step(o, false, 1000);
  TEST_ASSERT_TRUE(first.fullBrightnessAndFrame);
  for (uint32_t now : {1020u, 1040u, 2000u}) {
    const OtaScreenStep r = flow.step(o, false, now);
    TEST_ASSERT_FALSE(r.fullBrightnessAndFrame);
    TEST_ASSERT_FALSE(r.drawProgress);
    ASSERT_ENUM_EQ(OtaScreenResult::None, r.result);
    TEST_ASSERT_TRUE(r.ownsDisplay);
  }
}

// -- edge case: full brightness while running, done, restarting or a failure is on show
static void test_ota_holds_backlight_while_failure_shown(void) {
  OtaScreenFlow flow;
  TEST_ASSERT_FALSE(flow.holdsBacklight(progress(0, Phase::Idle), false));
  TEST_ASSERT_TRUE(flow.holdsBacklight(progress(1, Phase::Running), false));
  TEST_ASSERT_TRUE(flow.holdsBacklight(progress(1, Phase::Done), false));
  TEST_ASSERT_TRUE(flow.holdsBacklight(progress(0, Phase::Idle), true));
  failAt(flow, 3, 30001);
  TEST_ASSERT_TRUE(flow.holdsBacklight(progress(4, Phase::Failed), false));
}

// -- edge case: a finished update draws the "OK" result and keeps the screen until the restart
static void test_ota_done_draws_ok_result(void) {
  OtaScreenFlow flow;
  flow.step(progress(1, Phase::Running, 0), false, 1000);
  const OtaScreenStep done = flow.step(progress(2, Phase::Done, 1300000), false, 40000);
  ASSERT_ENUM_EQ(OtaScreenResult::Ok, done.result);
  TEST_ASSERT_FALSE(done.fullBrightnessAndFrame);  // already on screen
  TEST_ASSERT_TRUE(done.ownsDisplay);
  TEST_ASSERT_TRUE(flow.step(progress(2, Phase::Done, 1300000), false, 90000).ownsDisplay);
}

}  // namespace

void run_ota_screen_flow_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_ota_first_progress_draws_screen_then_only_bar);
  RUN_TEST(test_ota_owns_display_while_running_done_or_restarting);
  RUN_TEST(test_ota_failure_shown_5_s_then_back_to_screens);
  RUN_TEST(test_ota_failure_timer_survives_millis_wrap);
  RUN_TEST(test_ota_retry_after_failure_redraws_screen);
  RUN_TEST(test_ota_same_seq_draws_nothing);
  RUN_TEST(test_ota_holds_backlight_while_failure_shown);
  RUN_TEST(test_ota_done_draws_ok_result);
}
