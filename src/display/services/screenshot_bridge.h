// screenshot_bridge — the UI side of /screenshot.bmp (spec 09 §1, spec 10
// §2.2): what a ?screen=N capture prepares (screenshotPrepareTarget()) and
// one UI pass of a capture (screenshotUiPass()): answer the handler's
// Prepare / Band job through ScreenshotBridge, then put back the screen shown
// before the capture (ScreenshotReturn). The board runs it with a SpinLock
// bridge (display/adapters/lgfx/screenshot_panel), the tests with std::mutex.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/services/screenshot.h"  // ScreenshotBridge, ScreenshotReturn

// What a capture asks the UI to show before it is read off the panel.
enum class ScreenshotPrepareKind : uint8_t {
  Keep,              // no ?screen= (or out of range): whatever is showing
  Screen,            // switch to ScreenshotPrepare::screen
  UpdatePromptDemo,  // index screenCount: the update question, demo data
  AlertDemo,         // index screenCount + 1: the alert message screen, demo data
};

struct ScreenshotPrepare {
  ScreenshotPrepareKind kind = ScreenshotPrepareKind::Keep;
  int screen = -1;  // only for Screen
};

ScreenshotPrepare screenshotPrepareTarget(int index, int screenCount);

struct ScreenshotPassResult {
  bool capturing = false;  // a capture runs: draw nothing this pass
  int backTo = -1;         // >= 0: switch back to this screen now (ScreenChangeCause::Screenshot)
};

// One UI pass: takes the handler's job; Prepare -> ret.onPrepare(currentScreen,
// ...) and prepare(index); Band -> readRow(y, rowBuf) for every row of the
// band from its bottom row up, each copied with bridge.writeRow() (stops when
// the band was dropped); then finishJob(), capturing() and ret.afterPass().
// `rowBuf` holds `rowBytes` (B,G,R per pixel) — static in the adapter, never
// on the UI's stack.
template <typename Lock, typename PrepareFn, typename ReadRowFn>
ScreenshotPassResult screenshotUiPass(ScreenshotBridge<Lock> & /*bridge*/,
                                      ScreenshotReturn & /*ret*/, int /*currentScreen*/,
                                      int /*screenCount*/, uint32_t /*nowMs*/,
                                      PrepareFn && /*prepare*/, ReadRowFn && /*readRow*/,
                                      uint8_t * /*rowBuf*/, size_t /*rowBytes*/) {
  return {};  // TODO(spec 12): stub from test-author
}
