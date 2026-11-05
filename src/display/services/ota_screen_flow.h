// ota_screen_flow — the firmware-update screen, drawn from NetState::ota
// (spec/done/01-ota-updates.md): the first progress of an update draws the
// screen at full brightness, later ones only the bar; a success stays until
// the restart, a failure for kOtaFailedShowMs, then the normal screens come
// back. While it owns the display nothing else is drawn and the alert message
// screen closes.
//
// Pure — tested under `pio test -e native`; the device half (backlight,
// drawing) is display/adapters/service_flows.
#pragma once

#include <cstdint>

#include "platform/core_link.h"  // OtaProgress

constexpr uint32_t kOtaFailedShowMs = 5000;  // long enough to read on the board

enum class OtaScreenResult : uint8_t { None, Ok, Failed };

struct OtaScreenStep {
  bool fullBrightnessAndFrame = false;  // backlightFullNow() + drawOtaScreen()
  bool drawProgress = false;            // drawOtaProgress(written, total)
  OtaScreenResult result = OtaScreenResult::None;  // drawOtaResult()
  bool ownsDisplay = false;    // nothing else is drawn this pass
  bool backToScreens = false;  // the failure was shown long enough: firstDraw
};

class OtaScreenFlow {
 public:
  OtaScreenStep step(const OtaProgress &o, bool restartPending, uint32_t nowMs);
  // The update screens need full brightness: running, done, restarting or a
  // failure on show.
  bool holdsBacklight(const OtaProgress &o, bool restartPending) const;

 private:
  uint32_t seenSeq_ = 0;
  bool drawn_ = false;
  uint32_t failedAtMs_ = 0;  // != 0: "update failed" on screen since then (millis() | 1)
};
