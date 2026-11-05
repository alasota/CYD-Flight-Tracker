#include "display/adapters/service_flows.h"

#include <Arduino.h>

#include "display/adapters/backlight_ldr.h"
#include "display/adapters/lgfx/service_screen_draw.h"
#include "display/adapters/touch_xpt2046.h"
#include "display/core/theme/lcars_theme.h"  // LCARS_BLACK

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

void runPasswordReset(LGFX &tft, bool passwordSet, const std::function<void()> &clearPassword) {
  if (!shouldOfferPasswordReset(passwordSet, touchInputRead(tft).pressed)) return;
  drawResetHoldScreen(tft);
  PasswordResetFlow flow(millis());
  for (;;) {
    const TouchPoint t = touchInputRead(tft);
    ResetInput in;
    in.nowMs = millis();
    in.pressed = t.pressed;
    in.x = t.x;
    in.y = t.y;
    in.screenW = static_cast<int16_t>(tft.width());
    in.screenH = static_cast<int16_t>(tft.height());
    const ResetStep r = flow.step(in);
    if (r.clearPassword) clearPassword();
    switch (r.draw) {
      case ResetDraw::HoldProgress:
        drawResetHoldProgress(tft, r.heldMs);
        break;
      case ResetDraw::ConfirmScreen:
        drawResetConfirmScreen(tft);
        break;
      case ResetDraw::DoneScreen:
        drawResetDoneScreen(tft);
        break;
      case ResetDraw::Clear:
        tft.fillScreen(LCARS_BLACK);
        break;
      case ResetDraw::None:
      default:
        break;
    }
    if (r.finished) return;
    if (r.sleepMs != 0) delay(r.sleepMs);
  }
}

ServiceScreens::ServiceScreens() : ServiceScreenFlows(core_link::sendCommand) {}

ServiceOutcome ServiceScreens::otaPass(LGFX &tft, const NetState &s, const char *installedVersion) {
  return afterOtaStep(runOtaScreen(tft, otaFlow(), s.ota, s.restartPending, installedVersion));
}

ServiceOutcome ServiceScreens::dialogsPass(LGFX &tft, const NetState &s) {
  ServiceOutcome prompt;
  if (promptRuns(s.update.installPending)) {
    prompt = afterPromptStep(runUpdatePrompt(tft, promptFlow()));
    if (prompt.logQueueFull) Serial.println("[update] command queue full, retrying");
    if (prompt.ownsDisplay) return prompt;
  }
  if (!alertFlow().isOpen()) return prompt;
  ServiceOutcome alert = afterAlertStep(runAlertScreen(tft, alertFlow(), s.alerts, s.alertDetails));
  alert.firstDraw |= prompt.firstDraw;  // an answer this pass, before the message's own
  return alert;
}

void ServiceScreens::openAlerts(LGFX &tft, const NetState &s) {
  showAlertScreen(tft, alertFlow(), s.alerts, s.alertDetails);
}

void ServiceScreens::askAboutUpdateIfDue(LGFX &tft, const NetState &s, int currentScreen,
                                         bool touchPressed, bool screenOff) {
  if (!mayAskAboutUpdate(s.update.installPending)) return;
  const PromptGate g = updatePromptGate(s, currentScreen, millis(), touchPressed, screenOff,
                                        alertFlow().isOpen());
  if (promptFlow().tryOpen(g, s.update.latestTag, millis())) {
    openUpdatePrompt(tft, promptFlow(), s.update.installed);
  }
}
