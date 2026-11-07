// update_prompt_flow: the display's "new version — install it?" question. docs/architecture.md "New versions from GitHub (release_check)".
#include <unity.h>

#include "display/services/update_prompt_flow.h"

#include <cstdint>
#include <memory>
#include <string>

#include "display/core/screen_nav.h"               // kScreen*
#include "display/core/screens/service_screen.h"  // confirmButtonRect()
#include "fixtures.h"
#include "test_support.h"

namespace {

constexpr const char *kTag = "v1.3.0";

// A gate that lets the question through: a newer release, not skipped, home
// set, the screen on, no finger, no alert screen.
static PromptGate openGate() {
  PromptGate g;
  g.status = UpdateStatus::Available;
  g.latestTag = kTag;
  g.installedIsDev = false;
  g.homeConfigured = true;
  g.currentScreen = 0;
  return g;
}

static PromptInput at(uint32_t nowMs, bool pressed = false, int16_t x = 0, int16_t y = 0) {
  PromptInput in;
  in.nowMs = nowMs;
  in.pressed = pressed;
  in.x = x;
  in.y = y;
  return in;
}

// A press in the middle of YES (left) / NO (right).
static PromptInput tapOn(bool yes, uint32_t nowMs) {
  const Rect b = confirmButtonRect(yes, 320, 240);
  return at(nowMs, true, static_cast<int16_t>(b.x + b.w / 2), static_cast<int16_t>(b.y + b.h / 2));
}

// Opened at `nowMs`, the finger lifted right after.
static void openReleased(UpdatePromptFlow &flow, uint32_t nowMs) {
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, nowMs));
  flow.step(at(nowMs + 10));
}

// Pytanie o nową wersję pojawia się najwyżej raz na uruchomienie
static void test_prompt_opens_once_per_boot(void) {
  UpdatePromptFlow flow;
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 1000));
  TEST_ASSERT_TRUE(flow.active());
  ASSERT_STR_EQ(kTag, flow.tag());
  const PromptStep closed = flow.step(at(1000 + kUpdatePromptTimeoutMs));
  ASSERT_ENUM_EQ(PromptAnswer::TimedOut, closed.answer);
  TEST_ASSERT_FALSE(flow.active());
  // The gate lets it through again (alreadyPromptedThisBoot is the flow's own).
  TEST_ASSERT_FALSE(flow.tryOpen(openGate(), kTag, 40000));
  TEST_ASSERT_FALSE(flow.active());
}

// Palec, który był na ekranie przy pojawieniu się pytania, albo dotknięcie przed upływem 300 ms nie odpowiada
static void test_prompt_ignores_tap_before_release_or_arm_time(void) {
  {
    UpdatePromptFlow flow;
    TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 0));
    // The finger comes down as it appears and slides onto YES — never lifted.
    ASSERT_ENUM_EQ(PromptAnswer::None, flow.step(at(10, true, 5, 5)).answer);
    ASSERT_ENUM_EQ(PromptAnswer::None, flow.step(tapOn(true, 400)).answer);
    TEST_ASSERT_TRUE(flow.active());
  }
  {
    // Lifted at 200 ms, a fresh tap at 299 ms: still too early.
    UpdatePromptFlow flow;
    TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 0));
    flow.step(at(200));
    const PromptStep early = flow.step(tapOn(true, 299));
    ASSERT_ENUM_EQ(PromptAnswer::None, early.answer);
    ASSERT_ENUM_EQ(NetCommandType::None, early.command.type);
    TEST_ASSERT_TRUE(flow.active());
  }
  {
    UpdatePromptFlow flow;
    TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 0));
    flow.step(at(200));
    ASSERT_ENUM_EQ(PromptAnswer::Install, flow.step(tapOn(true, 300)).answer);  // armed
  }
}

// TAK wysyła InstallTag, NIE wysyła SkipTag z tagiem pytania, a ekrany wracają w całości
static void test_prompt_yes_installs_no_skips_with_full_redraw(void) {
  struct Example {
    bool yes;
    PromptAnswer answer;
    NetCommandType command;
  };
  const Example examples[] = {{true, PromptAnswer::Install, NetCommandType::InstallTag},
                              {false, PromptAnswer::Skip, NetCommandType::SkipTag}};
  for (const Example &e : examples) {
    UpdatePromptFlow flow;
    openReleased(flow, 0);
    const PromptStep r = flow.step(tapOn(e.yes, 300));
    ASSERT_ENUM_EQ(e.answer, r.answer);
    ASSERT_ENUM_EQ(e.command, r.command.type);
    ASSERT_STR_EQ(kTag, netCommandTag(r.command));
    TEST_ASSERT_TRUE(r.firstDraw);
    TEST_ASSERT_FALSE(flow.active());
  }
}

// Bez odpowiedzi pytanie znika dokładnie po 30 s, bez komendy
static void test_prompt_times_out_at_exactly_30_s(void) {
  UpdatePromptFlow flow;
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 5000));
  const PromptStep before = flow.step(at(5000 + 29999));
  ASSERT_ENUM_EQ(PromptAnswer::None, before.answer);
  TEST_ASSERT_FALSE(before.firstDraw);
  TEST_ASSERT_TRUE(flow.active());
  const PromptStep after = flow.step(at(5000 + 30000));
  ASSERT_ENUM_EQ(PromptAnswer::TimedOut, after.answer);
  TEST_ASSERT_TRUE(after.firstDraw);
  ASSERT_ENUM_EQ(NetCommandType::None, after.command.type);
  TEST_ASSERT_FALSE(flow.active());
}

// Pasek czasu pytania odświeża się co 250 ms i UI budzi się na ten moment
static void test_prompt_timer_redraws_every_250_ms(void) {
  UpdatePromptFlow flow;
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 0));
  TEST_ASSERT_FALSE(flow.step(at(249)).redrawTimer);
  const PromptStep due = flow.step(at(250));
  TEST_ASSERT_TRUE(due.redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(29750, due.remainingMs);
  TEST_ASSERT_EQUAL_UINT32(250, flow.msUntilTimer(250));
  TEST_ASSERT_FALSE(flow.step(at(499)).redrawTimer);
  TEST_ASSERT_EQUAL_UINT32(1, flow.msUntilTimer(499));
}

// Instalacja zlecona z panelu w trakcie pytania zamyka je bez przerysowania ekranów (ekran OTA przejmuje)
static void test_prompt_closed_by_panel_install_without_redraw(void) {
  UpdatePromptFlow flow;
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 0));
  TEST_ASSERT_TRUE(flow.active());
  const PromptStep r = flow.supersededByInstall();
  TEST_ASSERT_FALSE(flow.active());
  TEST_ASSERT_FALSE(r.firstDraw);
  ASSERT_ENUM_EQ(NetCommandType::None, r.command.type);
}

// -- edge case: a fresh tap outside both buttons is no answer
static void test_prompt_no_answer_tap_outside_buttons_keeps_waiting(void) {
  UpdatePromptFlow flow;
  openReleased(flow, 0);
  const PromptStep r = flow.step(at(1000, true, 160, 20));  // the title
  ASSERT_ENUM_EQ(PromptAnswer::None, r.answer);
  TEST_ASSERT_FALSE(r.firstDraw);
  TEST_ASSERT_TRUE(flow.active());
  flow.step(at(1100));
  ASSERT_ENUM_EQ(PromptAnswer::Skip, flow.step(tapOn(false, 1200)).answer);  // still answerable
}

// -- edge case: the gate from net_task's state: release answer, settings, the CPA at that moment
static void test_update_prompt_gate_from_net_state(void) {
  auto data = std::make_shared<AirspaceSnapshot>();
  data->cpa.found = true;
  data->cpa.t_cpa_seconds = 50.0f;
  data->polledAtMs = 1000;
  Settings cfg = makeSettings();
  cfg.update.skipped_tag = "v1.2.9";
  NetState s;
  s.data = data;
  s.cfg = std::make_shared<const Settings>(cfg);
  s.update.status = UpdateStatus::Available;
  s.update.latestTag = kTag;
  s.update.installed = "v1.2.0";
  const PromptGate g = updatePromptGate(s, kScreenFlight, 21000, true, true, true);
  ASSERT_ENUM_EQ(UpdateStatus::Available, g.status);
  ASSERT_STR_EQ(kTag, g.latestTag);
  ASSERT_STR_EQ("v1.2.9", g.skippedTag);
  TEST_ASSERT_FALSE(g.installedIsDev);
  TEST_ASSERT_TRUE(g.homeConfigured);
  TEST_ASSERT_EQUAL_INT(kScreenFlight, g.currentScreen);
  TEST_ASSERT_TRUE(g.cpaFound);
  TEST_ASSERT_EQUAL_INT(30, g.tCpaSeconds);  // 20 s after the poll
  TEST_ASSERT_TRUE(g.touchActive);
  TEST_ASSERT_TRUE(g.screenOff);
  TEST_ASSERT_TRUE(g.alertScreenOpen);
  TEST_ASSERT_FALSE(g.alreadyPromptedThisBoot);

  s.update.installed = "v1.2.0-3-g1a2b3c4";  // a development build
  s.data = nullptr;                          // before the first poll
  const PromptGate dev = updatePromptGate(s, kScreenFlights, 21000, false, false, false);
  TEST_ASSERT_TRUE(dev.installedIsDev);
  TEST_ASSERT_FALSE(dev.cpaFound);
}

// -- edge case: no wake-up for the timer strip while the question isn't open
static void test_prompt_ms_until_timer_is_no_deadline_when_closed(void) {
  UpdatePromptFlow flow;
  TEST_ASSERT_EQUAL_UINT32(kNoDeadline, flow.msUntilTimer(0));
  TEST_ASSERT_TRUE(flow.tryOpen(openGate(), kTag, 1000));
  TEST_ASSERT_EQUAL_UINT32(kPromptTimerStepMs, flow.msUntilTimer(1000));
  flow.step(at(1000 + kUpdatePromptTimeoutMs));  // timed out
  TEST_ASSERT_EQUAL_UINT32(kNoDeadline, flow.msUntilTimer(1000 + kUpdatePromptTimeoutMs));
}

}  // namespace

void run_update_prompt_flow_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_prompt_opens_once_per_boot);
  RUN_TEST(test_prompt_ignores_tap_before_release_or_arm_time);
  RUN_TEST(test_prompt_yes_installs_no_skips_with_full_redraw);
  RUN_TEST(test_prompt_times_out_at_exactly_30_s);
  RUN_TEST(test_prompt_timer_redraws_every_250_ms);
  RUN_TEST(test_prompt_closed_by_panel_install_without_redraw);
  RUN_TEST(test_prompt_no_answer_tap_outside_buttons_keeps_waiting);
  RUN_TEST(test_prompt_ms_until_timer_is_no_deadline_when_closed);
  RUN_TEST(test_update_prompt_gate_from_net_state);
}
