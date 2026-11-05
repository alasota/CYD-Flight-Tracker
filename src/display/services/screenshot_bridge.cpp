#include "display/services/screenshot_bridge.h"

ScreenshotPrepare screenshotPrepareTarget(int index, int screenCount) {
  ScreenshotPrepare p;
  if (index >= 0 && index < screenCount) {
    p.kind = ScreenshotPrepareKind::Screen;
    p.screen = index;
  } else if (index == screenCount) {
    p.kind = ScreenshotPrepareKind::UpdatePromptDemo;
  } else if (index == screenCount + 1) {
    p.kind = ScreenshotPrepareKind::AlertDemo;
  }
  return p;
}
