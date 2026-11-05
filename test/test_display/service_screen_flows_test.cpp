// service_screen_flows: the OTA screen, the update question and the alert message together, as the UI loop runs them. docs/architecture.md "Concurrency — two cores".
#include <unity.h>

#include "display/services/service_screen_flows.h"

#include <cstdint>
#include <string>
#include <vector>

#include "display/core/screens/service_screen.h"  // confirmButtonRect()
#include "test_support.h"

namespace {

constexpr const char *kTag = "v1.3.0";

// A command queue that takes `room` commands, then refuses.
struct FakeQueue {
  int room = 100;
  std::vector<NetCommand> sent;
  CommandSender sender() {
    return [this](const NetCommand &c) {
      if (room <= 0) return false;
      --room;
      sent.push_back(c);
      return true;
    };
  }
};

static PromptGate openGate() {
  PromptGate g;
  g.status = UpdateStatus::Available;
  g.latestTag = kTag;
  g.homeConfigured = true;
  return g;
}

static PromptInput promptAt(uint32_t nowMs, bool pressed = false, int16_t x = 0, int16_t y = 0) {
  PromptInput in;
  in.nowMs = nowMs;
  in.pressed = pressed;
  in.x = x;
  in.y = y;
  return in;
}

// The question opened at 0, the finger lifted, then a tap on YES at 400 ms.
static PromptStep answerYes(UpdatePromptFlow &prompt) {
  TEST_ASSERT_TRUE(prompt.tryOpen(openGate(), kTag, 0));
  prompt.step(promptAt(10));
  const Rect b = confirmButtonRect(true, 320, 240);
  return prompt.step(promptAt(400, true, static_cast<int16_t>(b.x + b.w / 2),
                              static_cast<int16_t>(b.y + b.h / 2)));
}

static AlertSet oneAlert() {
  AlertSet a;
  a.set(AlertId::OpenSkyLoginFailed);
  return a;
}

static AlertScreenInput alertAt(uint32_t nowMs) {
  AlertScreenInput in;
  in.nowMs = nowMs;
  return in;
}

// -- the OTA screen owns the display: the alert message closes, nothing pending is drawn
static void test_service_ota_owning_display_closes_alert_and_drops_pending(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  f.alertFlow().open(0, oneAlert());
  OtaScreenStep step;
  step.ownsDisplay = true;
  const ServiceOutcome o = f.afterOtaStep(step);
  TEST_ASSERT_TRUE(o.ownsDisplay);
  TEST_ASSERT_TRUE(o.clearPending);
  TEST_ASSERT_FALSE(o.firstDraw);
  TEST_ASSERT_FALSE(f.alertFlow().isOpen());
}

// -- a failure shown long enough: the normal screens come back in full, the alert message stays
static void test_service_ota_back_to_screens_redraws_in_full(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  OtaScreenStep step;
  step.backToScreens = true;
  const ServiceOutcome o = f.afterOtaStep(step);
  TEST_ASSERT_FALSE(o.ownsDisplay);
  TEST_ASSERT_FALSE(o.clearPending);
  TEST_ASSERT_TRUE(o.firstDraw);
  const ServiceOutcome idle = f.afterOtaStep(OtaScreenStep{});
  TEST_ASSERT_FALSE(idle.ownsDisplay);
  TEST_ASSERT_FALSE(idle.firstDraw);
}

// -- YES goes to net_task at once; the screens come back in full, the question no longer owns them
static void test_service_prompt_answer_sends_command_and_redraws(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  const PromptStep r = answerYes(f.promptFlow());
  const ServiceOutcome o = f.afterPromptStep(r);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(q.sent.size()));
  ASSERT_ENUM_EQ(NetCommandType::InstallTag, q.sent[0].type);
  ASSERT_STR_EQ(kTag, netCommandTag(q.sent[0]));
  TEST_ASSERT_TRUE(o.firstDraw);
  TEST_ASSERT_FALSE(o.ownsDisplay);
  TEST_ASSERT_FALSE(o.clearPending);
  TEST_ASSERT_FALSE(o.logQueueFull);
}

// -- a full queue keeps the answer, logs once and sends it on a later pass
static void test_service_prompt_command_waits_for_a_full_queue(void) {
  FakeQueue q;
  q.room = 0;
  ServiceScreenFlows f(q.sender());
  const ServiceOutcome o = f.afterPromptStep(answerYes(f.promptFlow()));
  TEST_ASSERT_TRUE(o.logQueueFull);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(q.sent.size()));
  f.retryCommand();  // still full
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(q.sent.size()));
  q.room = 1;
  f.retryCommand();
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(q.sent.size()));
  ASSERT_ENUM_EQ(NetCommandType::InstallTag, q.sent[0].type);
  f.retryCommand();  // sent once, not again
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(q.sent.size()));
}

// -- while open the question owns the display but the pending redraw stays
static void test_service_prompt_open_owns_display_keeps_pending(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  TEST_ASSERT_FALSE(f.promptRuns(false));  // nothing open
  TEST_ASSERT_TRUE(f.promptFlow().tryOpen(openGate(), kTag, 0));
  TEST_ASSERT_TRUE(f.promptRuns(false));
  const ServiceOutcome o = f.afterPromptStep(f.promptFlow().step(promptAt(100)));
  TEST_ASSERT_TRUE(o.ownsDisplay);
  TEST_ASSERT_FALSE(o.clearPending);
  TEST_ASSERT_FALSE(o.firstDraw);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(q.sent.size()));
}

// -- an install started from the settings page closes the question before it runs, without a redraw
static void test_service_prompt_superseded_by_install_does_not_run(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  TEST_ASSERT_TRUE(f.promptFlow().tryOpen(openGate(), kTag, 0));
  TEST_ASSERT_FALSE(f.promptRuns(true));
  TEST_ASSERT_FALSE(f.promptFlow().active());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(q.sent.size()));
}

// -- the alert message: open = owns, nothing pending, swallow; closed = new auto-cycle, full redraw
static void test_service_alert_open_owns_closed_restarts_auto_cycle(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  f.alertFlow().open(1000, oneAlert());
  const ServiceOutcome open = f.afterAlertStep(f.alertFlow().step(alertAt(1500), oneAlert()));
  TEST_ASSERT_TRUE(open.ownsDisplay);
  TEST_ASSERT_TRUE(open.clearPending);
  TEST_ASSERT_TRUE(open.swallowTouch);
  TEST_ASSERT_FALSE(open.firstDraw);
  TEST_ASSERT_FALSE(open.restartAutoCycle);

  const ServiceOutcome closed = f.afterAlertStep(
      f.alertFlow().step(alertAt(1000 + kAlertScreenTimeoutMs), oneAlert()));
  TEST_ASSERT_FALSE(closed.ownsDisplay);
  TEST_ASSERT_FALSE(closed.clearPending);
  TEST_ASSERT_TRUE(closed.swallowTouch);  // the closing pass's finger isn't a tap either
  TEST_ASSERT_TRUE(closed.firstDraw);
  TEST_ASSERT_TRUE(closed.restartAutoCycle);

  const ServiceOutcome none = f.afterAlertStep(f.alertFlow().step(alertAt(20000), oneAlert()));
  TEST_ASSERT_FALSE(none.ownsDisplay);
  TEST_ASSERT_FALSE(none.swallowTouch);
}

// -- the question is asked only with no message or question up and no install running
static void test_service_may_ask_only_without_dialog_or_install(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  TEST_ASSERT_TRUE(f.mayAskAboutUpdate(false));
  TEST_ASSERT_FALSE(f.mayAskAboutUpdate(true));
  f.alertFlow().open(0, oneAlert());
  TEST_ASSERT_FALSE(f.mayAskAboutUpdate(false));
  f.alertFlow().closeQuietly();
  TEST_ASSERT_TRUE(f.promptFlow().tryOpen(openGate(), kTag, 0));
  TEST_ASSERT_FALSE(f.mayAskAboutUpdate(false));
}

// -- a screenshot of a normal screen closes the message like CLOSE; nothing when it isn't open
static void test_service_close_alert_screen_for_a_capture(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  const ServiceOutcome none = f.closeAlertScreen();
  TEST_ASSERT_FALSE(none.firstDraw);
  TEST_ASSERT_FALSE(none.restartAutoCycle);
  f.alertFlow().open(0, oneAlert());
  const ServiceOutcome o = f.closeAlertScreen();
  TEST_ASSERT_FALSE(f.alertFlow().isOpen());
  TEST_ASSERT_TRUE(o.firstDraw);
  TEST_ASSERT_TRUE(o.restartAutoCycle);
  TEST_ASSERT_FALSE(o.ownsDisplay);
  TEST_ASSERT_FALSE(o.clearPending);
}

// -- a screenshot demo over everything: the message closes, the real screen comes back in full
static void test_service_screenshot_demo_closes_alert_and_redraws_after(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  f.alertFlow().open(0, oneAlert());
  const ServiceOutcome o = f.screenshotDemoShown();
  TEST_ASSERT_FALSE(f.alertFlow().isOpen());
  TEST_ASSERT_TRUE(o.clearPending);
  TEST_ASSERT_TRUE(o.firstDraw);  // applied after the clear
  TEST_ASSERT_FALSE(o.ownsDisplay);
  TEST_ASSERT_FALSE(o.restartAutoCycle);
}

// -- after a normal pass's tap: the message or question it opened owns the display
static void test_service_after_normal_tap_dialog_owns_display(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  const ServiceOutcome none = f.afterNormalTap();
  TEST_ASSERT_FALSE(none.ownsDisplay);
  TEST_ASSERT_FALSE(none.clearPending);
  f.alertFlow().open(0, oneAlert());
  const ServiceOutcome alert = f.afterNormalTap();
  TEST_ASSERT_TRUE(alert.ownsDisplay);
  TEST_ASSERT_TRUE(alert.clearPending);
  TEST_ASSERT_FALSE(alert.firstDraw);
  f.alertFlow().closeQuietly();
  TEST_ASSERT_TRUE(f.promptFlow().tryOpen(openGate(), kTag, 0));
  const ServiceOutcome prompt = f.afterNormalTap();
  TEST_ASSERT_TRUE(prompt.ownsDisplay);
  TEST_ASSERT_TRUE(prompt.clearPending);
}

// -- full brightness for the question or the OTA screen; wake-ups for their timers only while up
static void test_service_backlight_and_wake_times(void) {
  FakeQueue q;
  ServiceScreenFlows f(q.sender());
  OtaProgress idle;
  TEST_ASSERT_FALSE(f.holdsBacklight(idle, false));
  TEST_ASSERT_TRUE(f.holdsBacklight(idle, true));  // restarting
  OtaProgress running;
  running.phase = OtaProgress::Phase::Running;
  TEST_ASSERT_TRUE(f.holdsBacklight(running, false));

  UiWake w;
  f.addWakeTimes(w, 0);
  TEST_ASSERT_EQUAL_UINT32(kNoDeadline, w.untilPromptTimerMs);
  TEST_ASSERT_EQUAL_UINT32(kNoDeadline, w.untilAlertTimerMs);

  TEST_ASSERT_TRUE(f.promptFlow().tryOpen(openGate(), kTag, 1000));
  TEST_ASSERT_TRUE(f.holdsBacklight(idle, false));
  f.alertFlow().open(1000, oneAlert());
  UiWake both;
  f.addWakeTimes(both, 1100);
  TEST_ASSERT_EQUAL_UINT32(kPromptTimerStepMs - 100, both.untilPromptTimerMs);
  TEST_ASSERT_EQUAL_UINT32(900, both.untilAlertTimerMs);
}

}  // namespace

void run_service_screen_flows_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_service_ota_owning_display_closes_alert_and_drops_pending);
  RUN_TEST(test_service_ota_back_to_screens_redraws_in_full);
  RUN_TEST(test_service_prompt_answer_sends_command_and_redraws);
  RUN_TEST(test_service_prompt_command_waits_for_a_full_queue);
  RUN_TEST(test_service_prompt_open_owns_display_keeps_pending);
  RUN_TEST(test_service_prompt_superseded_by_install_does_not_run);
  RUN_TEST(test_service_alert_open_owns_closed_restarts_auto_cycle);
  RUN_TEST(test_service_may_ask_only_without_dialog_or_install);
  RUN_TEST(test_service_close_alert_screen_for_a_capture);
  RUN_TEST(test_service_backlight_and_wake_times);
  RUN_TEST(test_service_screenshot_demo_closes_alert_and_redraws_after);
  RUN_TEST(test_service_after_normal_tap_dialog_owns_display);
}
