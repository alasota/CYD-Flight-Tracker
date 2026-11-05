#include "display/adapters/service_flows.h"

#include <Arduino.h>

#include "display/adapters/backlight_ldr.h"
#include "display/adapters/lgfx/service_screen_draw.h"

OtaScreenStep runOtaScreen(LGFX &tft, OtaScreenFlow &flow, const OtaProgress &o,
                           bool restartPending, const char *installedVersion) {
  const OtaScreenStep r = flow.step(o, restartPending, millis());
  if (r.fullBrightnessAndFrame) {
    backlightFullNow(tft);
    drawOtaScreen(tft, installedVersion, o.fromGithub);
  }
  if (r.drawProgress) {
    drawOtaProgress(tft, installedVersion, o.newVersion.c_str(), o.written, o.total);
  }
  if (r.result != OtaScreenResult::None) drawOtaResult(tft, r.result == OtaScreenResult::Ok);
  return r;
}
