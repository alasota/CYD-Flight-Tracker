#include "display/services/service_screen_flows.h"

#include <utility>

ServiceScreenFlows::ServiceScreenFlows(CommandSender send) : send_(std::move(send)) {}

ServiceOutcome ServiceScreenFlows::afterOtaStep(const OtaScreenStep &s) {
  ServiceOutcome o;
  o.firstDraw = s.backToScreens;
  if (s.ownsDisplay) {
    alert_.closeQuietly();  // the OTA screen comes first
    o.ownsDisplay = true;
    o.clearPending = true;
  }
  return o;
}

bool ServiceScreenFlows::promptRuns(bool installPending) {
  if (prompt_.active() && installPending) prompt_.supersededByInstall();
  return prompt_.active();
}

ServiceOutcome ServiceScreenFlows::afterPromptStep(const PromptStep &s) {
  ServiceOutcome o;
  if (s.command.type != NetCommandType::None) {
    pending_ = submitCommand(s.command, send_);
    o.logQueueFull = pending_.logQueueFull;
  }
  o.firstDraw = s.firstDraw;
  o.ownsDisplay = prompt_.active();
  return o;
}

ServiceOutcome ServiceScreenFlows::afterAlertStep(const AlertFlowStep &s) {
  ServiceOutcome o;
  o.swallowTouch = s.swallowTouch;
  if (s.close) {
    alert_.closeQuietly();
    o.restartAutoCycle = true;  // same screen and page, a fresh interval
    o.firstDraw = true;
  }
  if (alert_.isOpen()) {
    o.ownsDisplay = true;
    o.clearPending = true;
  }
  return o;
}

bool ServiceScreenFlows::mayAskAboutUpdate(bool installPending) const {
  return !alert_.isOpen() && !prompt_.active() && !installPending;
}

ServiceOutcome ServiceScreenFlows::closeAlertScreen() {
  if (!alert_.isOpen()) return ServiceOutcome{};
  AlertFlowStep closing;
  closing.close = true;
  return afterAlertStep(closing);
}

ServiceOutcome ServiceScreenFlows::screenshotDemoShown() {
  alert_.closeQuietly();
  ServiceOutcome o;
  o.clearPending = true;
  o.firstDraw = true;  // repaints the real screen once the capture is out
  return o;
}

ServiceOutcome ServiceScreenFlows::afterNormalTap() const {
  ServiceOutcome o;
  o.ownsDisplay = alert_.isOpen() || prompt_.active();  // the tap opened the "!", or it was asked
  o.clearPending = o.ownsDisplay;
  return o;
}

void ServiceScreenFlows::retryCommand() { pending_ = retryPendingCommand(pending_, send_); }

bool ServiceScreenFlows::holdsBacklight(const OtaProgress &o, bool restartPending) const {
  return prompt_.active() || ota_.holdsBacklight(o, restartPending);
}

void ServiceScreenFlows::addWakeTimes(UiWake &w, uint32_t nowMs) const {
  w.untilPromptTimerMs = prompt_.msUntilTimer(nowMs);
  if (alert_.isOpen()) w.untilAlertTimerMs = alert_.msToNextSecond(nowMs);
}
