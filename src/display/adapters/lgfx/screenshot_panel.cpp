#include "display/adapters/lgfx/screenshot_panel.h"

#include <Arduino.h>

#include "display/adapters/lgfx/service_screen_draw.h"
#include "platform/core_link.h"  // SpinLock, wakeUi(); updateDemoState(), alertDemoState()

namespace {

// One request at a time: the handler waits by polling (1 ms steps); every
// request carries a generation, and a timed-out or ended one is invalidated
// under the lock, so a band the UI is still reading never writes into a
// buffer the handler has freed nor leaves "done" behind (spec 09 §1).
ScreenshotBridge<SpinLock> bridge(ScreenshotBridge<SpinLock>::Hooks{
    [] { return static_cast<uint32_t>(millis()); }, [] { core_link::wakeUi(); },
    [] { vTaskDelay(1); }});

// The screen to put back once a ?screen=N capture is over (spec 10).
ScreenshotReturn shotReturn;

}  // namespace

bool screenshotPanelBegin(int screenIndex, uint32_t timeoutMs) {
  return bridge.begin(screenIndex, timeoutMs);
}

void screenshotPanelRequestBand(int y0, int rows, uint32_t stride, uint8_t *out) {
  bridge.requestBand(y0, rows, stride, out);
}

bool screenshotPanelWaitBand(uint32_t timeoutMs) { return bridge.waitBand(timeoutMs); }

void screenshotPanelEnd() { bridge.end(); }

ScreenshotPassResult screenshotPanelPass(LGFX &tft, int currentScreen, int screenCount,
                                         void (*prepare)(int screenIndex)) {
  // Static, not on the UI task's 8 KB stack.
  static lgfx::rgb888_t line[320];
  static uint8_t bgr[320 * 3];
  const int w = static_cast<int>(tft.width());
  auto readRow = [&tft, w](int y, uint8_t *out) {
    tft.readRect(0, y, w, 1, line);
    for (int x = 0; x < w; ++x) {
      out[3 * x + 0] = line[x].b;
      out[3 * x + 1] = line[x].g;
      out[3 * x + 2] = line[x].r;
    }
  };
  return screenshotUiPass(bridge, shotReturn, currentScreen, screenCount, millis(), prepare,
                          readRow, bgr, static_cast<size_t>(3 * w));
}

void drawScreenshotDemo(LGFX &tft, ScreenshotPrepareKind kind) {
  if (kind == ScreenshotPrepareKind::UpdatePromptDemo) {
    const UpdateState demo = updateDemoState(/*withPrerelease=*/false);
    drawUpdatePromptScreen(tft, demo.installed, demo.latest.tag);
    drawUpdatePromptTimer(tft, kUpdatePromptTimeoutMs * 2 / 3);
  } else if (kind == ScreenshotPrepareKind::AlertDemo) {
    const AlertDemo demo = alertDemoState();
    drawAlertScreen(tft, demo.alerts, demo.details, demo.details.quotaObservedMs, 12);
  }
}
