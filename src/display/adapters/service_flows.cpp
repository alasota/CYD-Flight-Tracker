#include "display/adapters/service_flows.h"

#include <Arduino.h>

#include "display/adapters/backlight_ldr.h"
#include "display/adapters/lgfx/service_screen_draw.h"
#include "display/adapters/touch_xpt2046.h"

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

void openUpdatePrompt(LGFX &tft, const UpdatePromptFlow &flow, const std::string &installed) {
  Serial.printf("[update] asking on the display: %s -> %s\n", installed.c_str(),
                flow.tag().c_str());
  drawUpdatePromptScreen(tft, installed, flow.tag());
}

PromptStep runUpdatePrompt(LGFX &tft, UpdatePromptFlow &flow) {
  const TouchPoint t = touchInputRead(tft);
  PromptInput in;
  in.nowMs = millis();
  in.pressed = t.pressed;
  in.x = t.x;
  in.y = t.y;
  in.screenW = static_cast<int16_t>(tft.width());
  in.screenH = static_cast<int16_t>(tft.height());
  const PromptStep r = flow.step(in);
  if (r.redrawTimer) drawUpdatePromptTimer(tft, r.remainingMs);
  return r;
}
