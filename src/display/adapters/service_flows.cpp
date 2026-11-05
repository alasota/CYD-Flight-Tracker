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

void showAlertScreen(LGFX &tft, AlertScreenFlow &flow, const AlertSet &alerts,
                     const AlertDetails &details) {
  const uint32_t now = millis();
  const uint32_t secondsLeft = flow.open(now, alerts);
  Serial.printf("[alert] message screen opened (%u alert(s))\n",
                static_cast<unsigned>(alertsInOrder(alerts, true).size()));
  drawAlertScreen(tft, alerts, details, now, secondsLeft);
}

AlertFlowStep runAlertScreen(LGFX &tft, AlertScreenFlow &flow, const AlertSet &alerts,
                             const AlertDetails &details) {
  const TouchPoint t = touchInputRead(tft);
  AlertScreenInput in;
  in.nowMs = millis();
  in.pressed = t.pressed;
  in.x = t.x;
  in.y = t.y;
  in.anyDisplayAlerts = alerts.displaySeverity() != AlertSeverity::None;
  in.screenW = static_cast<int16_t>(tft.width());
  in.screenH = static_cast<int16_t>(tft.height());
  const AlertFlowStep r = flow.step(in, alerts);
  if (r.redrawList) {
    drawAlertScreen(tft, alerts, details, in.nowMs, r.secondsLeft);
  } else if (r.redrawTimer) {
    drawAlertCloseButton(tft, static_cast<int>(alertsInOrder(alerts, true).size()),
                         r.secondsLeft);
  }
  return r;
}
