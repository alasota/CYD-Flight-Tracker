// password_reset_flow — removing a forgotten web panel password from the
// touchscreen at power-up (spec/done/01-ota-updates.md §5.7): with a password
// set and a finger on the screen, hold it kResetHoldMs (a gap in the readings
// up to kResetReleaseGraceMs is a glitch, a longer one cancels), lift it,
// then a fresh tap on YES / NO within kResetConfirmTimeoutMs (none = NO).
//
// Pure — tested under `pio test -e native`. The device half
// (display/adapters/service_flows) runs it in setup(), before net_task, and
// sleeps `sleepMs` between steps as before; main.cpp clears the password.
#pragma once

#include <cstdint>

#include "admin/core/web_auth.h"  // kReset*, resetHoldState(), ResetChoice

// Offered only with a password set and a finger on the screen at power-up.
bool shouldOfferPasswordReset(bool passwordSet, bool pressed);

struct ResetInput {
  uint32_t nowMs = 0;
  bool pressed = false;
  int16_t x = 0, y = 0;
  int16_t screenW = 320, screenH = 240;
};

enum class ResetDraw : uint8_t {
  None,
  HoldProgress,   // drawResetHoldProgress(heldMs)
  ConfirmScreen,  // drawResetConfirmScreen()
  DoneScreen,     // drawResetDoneScreen() — the password is gone
  Clear,          // a black screen: the reset is over
};

struct ResetStep {
  ResetDraw draw = ResetDraw::None;
  uint32_t heldMs = 0;         // for HoldProgress
  bool clearPassword = false;  // YES: clear and save it (once)
  bool finished = false;       // the reset is over, boot goes on
  uint32_t sleepMs = 0;        // before the next step: 30 hold, 20 release / confirm, 1500 done
};

class PasswordResetFlow {
 public:
  enum class Phase : uint8_t { Hold, WaitRelease, Confirm, Finished };

  // Started with the finger down at `nowMs` (the hold screen is drawn by the caller).
  explicit PasswordResetFlow(uint32_t nowMs);
  // One step with a fresh touch reading.
  ResetStep step(const ResetInput &in);
  Phase phase() const;

 private:
  Phase phase_ = Phase::Hold;
  uint32_t holdStartMs_ = 0;
  uint32_t lastPressedMs_ = 0;
  uint32_t releasedSinceMs_ = 0;
  uint32_t askedAtMs_ = 0;
  bool wasPressed_ = false;
  bool cleared_ = false;  // YES answered: the "done" screen is up
};
