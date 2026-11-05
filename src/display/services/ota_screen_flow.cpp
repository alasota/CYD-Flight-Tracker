#include "display/services/ota_screen_flow.h"

OtaScreenStep OtaScreenFlow::step(const OtaProgress &o, bool restartPending, uint32_t nowMs) {
  using Phase = OtaProgress::Phase;
  OtaScreenStep r;
  if (o.seq != seenSeq_) {
    seenSeq_ = o.seq;
    if (o.phase == Phase::Running && o.written == 0) drawn_ = false;  // a new update
    if (!drawn_) {
      r.fullBrightnessAndFrame = true;
      drawn_ = true;
    }
    switch (o.phase) {
      case Phase::Running:
        r.drawProgress = true;
        break;
      case Phase::Done:
        r.result = OtaScreenResult::Ok;  // net_task restarts the board in a moment
        break;
      case Phase::Failed:
        r.result = OtaScreenResult::Failed;
        failedAtMs_ = nowMs | 1;  // never 0, which means "none"
        break;
      default:
        break;
    }
  }
  if (o.phase == Phase::Running || o.phase == Phase::Done || restartPending) {
    r.ownsDisplay = true;
    return r;
  }
  if (failedAtMs_ != 0) {
    if (nowMs - failedAtMs_ < kOtaFailedShowMs) {
      r.ownsDisplay = true;
      return r;
    }
    failedAtMs_ = 0;  // back to the normal screens
    drawn_ = false;
    r.backToScreens = true;
  }
  return r;
}

bool OtaScreenFlow::holdsBacklight(const OtaProgress &o, bool restartPending) const {
  return o.phase == OtaProgress::Phase::Running || o.phase == OtaProgress::Phase::Done ||
         failedAtMs_ != 0 || restartPending;
}
