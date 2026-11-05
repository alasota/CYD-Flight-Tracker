// password_reset_flow: removing a forgotten panel password from the touchscreen at power-up. docs/architecture.md "Firmware updates (OTA) & panel password".
#include <unity.h>

#include "display/services/password_reset_flow.h"

#include <cstdint>
#include <string>

#include "display/core/screens/service_screen.h"  // confirmButtonRect()
#include "test_support.h"

namespace {

using Phase = PasswordResetFlow::Phase;

static ResetInput at(uint32_t nowMs, bool pressed = false, int16_t x = 0, int16_t y = 0) {
  ResetInput in;
  in.nowMs = nowMs;
  in.pressed = pressed;
  in.x = x;
  in.y = y;
  return in;
}

// A press in the middle of YES (left) / NO (right).
static ResetInput tapOn(bool yes, uint32_t nowMs) {
  const Rect b = confirmButtonRect(yes, 320, 240);
  return at(nowMs, true, static_cast<int16_t>(b.x + b.w / 2), static_cast<int16_t>(b.y + b.h / 2));
}

// Steps on from `t` (the time of the last step): the finger held until the
// 5 s are up, then lifted until the question is up. Returns the time of the
// step that drew it.
static uint32_t toConfirm(PasswordResetFlow &flow, uint32_t t) {
  const uint32_t limit = t + kResetHoldMs + 5000;
  while (flow.phase() == Phase::Hold && t < limit) {
    t += 30;
    flow.step(at(t, true));
  }
  while (flow.phase() == Phase::WaitRelease && t < limit) {
    t += 20;
    flow.step(at(t, false));
  }
  ASSERT_ENUM_EQ(Phase::Confirm, flow.phase());
  return t;
}

// Reset hasła jest proponowany tylko z ustawionym hasłem i palcem na ekranie przy starcie
static void test_reset_offered_only_with_password_and_finger(void) {
  TEST_ASSERT_TRUE(shouldOfferPasswordReset(/*passwordSet=*/true, /*pressed=*/true));
  TEST_ASSERT_FALSE(shouldOfferPasswordReset(true, false));
  TEST_ASSERT_FALSE(shouldOfferPasswordReset(false, true));
}

// Przytrzymanie palca 5 s prowadzi do pytania TAK / NIE dopiero po puszczeniu palca
static void test_reset_hold_5_s_then_confirm_after_release(void) {
  PasswordResetFlow flow(1000);
  for (uint32_t t = 1030; t < 6000; t += 30) {
    const ResetStep r = flow.step(at(t, true));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetDraw::HoldProgress), static_cast<int>(r.draw));
    TEST_ASSERT_EQUAL_UINT32(t - 1000, r.heldMs);
  }
  ASSERT_ENUM_EQ(ResetDraw::HoldProgress, flow.step(at(5999, true)).draw);
  ASSERT_ENUM_EQ(Phase::Hold, flow.phase());
  flow.step(at(6000, true));  // 5 s held
  ASSERT_ENUM_EQ(Phase::WaitRelease, flow.phase());

  // Still holding: no question.
  for (uint32_t t = 6020; t <= 7000; t += 20) {
    ASSERT_ENUM_NE(ResetDraw::ConfirmScreen, flow.step(at(t, true)).draw);
  }
  // Lifted after the reading at 7000: the question 300 ms after it, not before.
  ASSERT_ENUM_EQ(ResetDraw::None, flow.step(at(7299, false)).draw);
  ASSERT_ENUM_EQ(Phase::WaitRelease, flow.phase());
  ASSERT_ENUM_EQ(ResetDraw::ConfirmScreen, flow.step(at(7300, false)).draw);
  ASSERT_ENUM_EQ(Phase::Confirm, flow.phase());
}

// Puszczenie palca na dłużej niż 300 ms przerywa reset, krótszy zanik odczytu nie
static void test_reset_release_over_300_ms_cancels_glitch_does_not(void) {
  PasswordResetFlow flow(1000);
  flow.step(at(1100, true));
  const ResetStep glitch = flow.step(at(1400, false));  // 300 ms without a reading
  ASSERT_ENUM_EQ(ResetDraw::HoldProgress, glitch.draw);
  TEST_ASSERT_FALSE(glitch.finished);
  ASSERT_ENUM_EQ(Phase::Hold, flow.phase());
  flow.step(at(1450, true));  // back
  const ResetStep gone = flow.step(at(1751, false));  // 301 ms
  ASSERT_ENUM_EQ(ResetDraw::Clear, gone.draw);
  TEST_ASSERT_TRUE(gone.finished);
  TEST_ASSERT_FALSE(gone.clearPassword);
  ASSERT_ENUM_EQ(Phase::Finished, flow.phase());
}

// TAK czyści hasło dokładnie raz, NIE i brak odpowiedzi przez 15 s je zostawiają
static void test_reset_confirm_yes_clears_once_no_and_timeout_keep(void) {
  enum class What { Yes, No, Nothing, OutsideThenNothing };
  struct Example {
    What what;
    int clears;
    bool doneScreen;
  };
  const Example examples[] = {{What::Yes, 1, true},
                              {What::No, 0, false},
                              {What::Nothing, 0, false},
                              {What::OutsideThenNothing, 0, false}};
  for (const Example &e : examples) {
    const std::string what = "example " + std::to_string(static_cast<int>(e.what));
    PasswordResetFlow flow(1000);
    const uint32_t asked = toConfirm(flow, 1000);
    int clears = 0;
    bool doneScreen = false;
    ResetDraw last = ResetDraw::None;
    uint32_t t = asked;
    bool finished = false;
    for (int guard = 0; guard < 2000 && !finished; ++guard) {
      t += 20;
      ResetInput in = at(t);
      if (e.what == What::Yes && t == asked + 100) in = tapOn(true, t);
      if (e.what == What::No && t == asked + 100) in = tapOn(false, t);
      if (e.what == What::OutsideThenNothing && t == asked + 100) in = at(t, true, 160, 20);
      const ResetStep r = flow.step(in);
      if (r.clearPassword) ++clears;
      if (r.draw == ResetDraw::DoneScreen) doneScreen = true;
      if (r.draw != ResetDraw::None) last = r.draw;
      finished = r.finished;
      // No answer: still asking 20 ms before the 15 s are up.
      if ((e.what == What::Nothing || e.what == What::OutsideThenNothing) &&
          t == asked + kResetConfirmTimeoutMs - 20) {
        TEST_ASSERT_FALSE_MESSAGE(r.finished, what.c_str());
      }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.clears, clears, what.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(e.doneScreen, doneScreen, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ResetDraw::Clear), static_cast<int>(last),
                                  what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(Phase::Finished),
                                  static_cast<int>(flow.phase()), what.c_str());
  }
}

// -- edge case: a tap outside YES / NO is no answer; the question stays
static void test_reset_tap_outside_buttons_is_no_answer(void) {
  PasswordResetFlow flow(0);
  const uint32_t asked = toConfirm(flow, 0);
  const ResetStep r = flow.step(at(asked + 100, true, 160, 20));
  ASSERT_ENUM_EQ(ResetDraw::None, r.draw);
  TEST_ASSERT_FALSE(r.finished);
  TEST_ASSERT_FALSE(r.clearPassword);
  flow.step(at(asked + 120));  // lifted
  const ResetStep yes = flow.step(tapOn(true, asked + 140));
  TEST_ASSERT_TRUE(yes.clearPassword);  // still answerable
}

// -- edge case: the waits between steps are today's delay() values
static void test_reset_sleep_steps_match_today(void) {
  PasswordResetFlow flow(0);
  TEST_ASSERT_EQUAL_UINT32(30, flow.step(at(100, true)).sleepMs);  // holding
  for (uint32_t t = 130; flow.phase() == Phase::Hold && t < 7000; t += 30) flow.step(at(t, true));
  ASSERT_ENUM_EQ(Phase::WaitRelease, flow.phase());
  TEST_ASSERT_EQUAL_UINT32(20, flow.step(at(5100, true)).sleepMs);  // waiting for the lift
  const uint32_t asked = toConfirm(flow, 5100);
  TEST_ASSERT_EQUAL_UINT32(20, flow.step(at(asked + 20)).sleepMs);  // asking
  const ResetStep done = flow.step(tapOn(true, asked + 40));
  ASSERT_ENUM_EQ(ResetDraw::DoneScreen, done.draw);
  TEST_ASSERT_EQUAL_UINT32(1500, done.sleepMs);  // "password removed" stays readable
  const ResetStep end = flow.step(at(asked + 1540));
  ASSERT_ENUM_EQ(ResetDraw::Clear, end.draw);
  TEST_ASSERT_TRUE(end.finished);
}

}  // namespace

void run_password_reset_flow_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_reset_offered_only_with_password_and_finger);
  RUN_TEST(test_reset_hold_5_s_then_confirm_after_release);
  RUN_TEST(test_reset_release_over_300_ms_cancels_glitch_does_not);
  RUN_TEST(test_reset_confirm_yes_clears_once_no_and_timeout_keep);
  RUN_TEST(test_reset_tap_outside_buttons_is_no_answer);
  RUN_TEST(test_reset_sleep_steps_match_today);
}
