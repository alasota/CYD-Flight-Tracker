#include "display/services/password_reset_flow.h"

#include "display/core/screens/service_screen.h"  // confirmHit()

bool shouldOfferPasswordReset(bool passwordSet, bool pressed) { return passwordSet && pressed; }

PasswordResetFlow::PasswordResetFlow(uint32_t nowMs)
    : holdStartMs_(nowMs), lastPressedMs_(nowMs) {}

ResetStep PasswordResetFlow::step(const ResetInput &in) {
  ResetStep r;
  switch (phase_) {
    case Phase::Hold: {
      if (in.pressed) lastPressedMs_ = in.nowMs;
      const uint32_t held = in.nowMs - holdStartMs_;
      const ResetHoldState st = resetHoldState(in.nowMs - lastPressedMs_, held);
      if (st == ResetHoldState::Released) break;  // lifted for good: no reset
      r.draw = ResetDraw::HoldProgress;
      r.heldMs = held;
      if (st == ResetHoldState::Done) {
        phase_ = Phase::WaitRelease;  // wait for the finger to come off, then ask
        releasedSinceMs_ = in.nowMs;
      } else {
        r.sleepMs = 30;
      }
      return r;
    }
    case Phase::WaitRelease:
      // The time first, then the reading — a reading after the grace window
      // no longer counts.
      if (in.nowMs - releasedSinceMs_ >= kResetReleaseGraceMs) {
        phase_ = Phase::Confirm;
        askedAtMs_ = in.nowMs;
        wasPressed_ = false;
        r.draw = ResetDraw::ConfirmScreen;
        return r;
      }
      if (in.pressed) releasedSinceMs_ = in.nowMs;
      r.sleepMs = 20;
      return r;
    case Phase::Confirm:
      if (cleared_ || in.nowMs - askedAtMs_ >= kResetConfirmTimeoutMs) break;  // done / no answer
      if (in.pressed && !wasPressed_) {
        const ResetChoice c = confirmHit(in.x, in.y, in.screenW, in.screenH);
        if (c == ResetChoice::Yes) {
          cleared_ = true;
          r.draw = ResetDraw::DoneScreen;
          r.clearPassword = true;
          r.sleepMs = 1500;  // "password removed" stays readable
          return r;
        }
        if (c == ResetChoice::No) break;
      }
      wasPressed_ = in.pressed;
      r.sleepMs = 20;
      return r;
    case Phase::Finished:
    default:
      break;
  }
  // Over: a black screen, boot goes on.
  phase_ = Phase::Finished;
  r.draw = ResetDraw::Clear;
  r.finished = true;
  return r;
}

PasswordResetFlow::Phase PasswordResetFlow::phase() const { return phase_; }
