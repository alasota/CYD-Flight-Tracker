// service_screen_flows — the UI's service screens together, as the loop on
// core 1 runs them: the firmware-update screen first, then (after a
// screenshot, which main.cpp handles) the update question and the alert
// message. Each flow's step comes in, one ServiceOutcome goes out: whether
// the normal screens wait this pass and what they must do (drop the pending
// redraw, repaint in full, restart the auto-cycle interval, swallow the
// touch). It also keeps the update question's YES / NO command until the
// queue to net_task takes it (spec 09 §2).
//
// Pure — tested under `pio test -e native`; the device half (touch reads,
// drawing) is ServiceScreens in display/adapters/service_flows.
#pragma once

#include <cstdint>

#include "display/services/alert_screen_flow.h"
#include "display/services/ota_screen_flow.h"
#include "display/services/update_prompt_flow.h"
#include "platform/core_link.h"  // CommandSender, PendingCommand, UiWake

// What a service screen pass asks of the UI loop.
struct ServiceOutcome {
  bool ownsDisplay = false;       // a service screen is up: the normal screens wait
  bool clearPending = false;      // drop the pending redraw events (before firstDraw)
  bool firstDraw = false;         // the normal screens come back in full
  bool restartAutoCycle = false;  // a fresh auto-cycle interval (the alert message closed)
  bool swallowTouch = false;      // the finger on the screen is no tap for the screens below
  bool logQueueFull = false;      // log "[update] command queue full, retrying"
};

class ServiceScreenFlows {
 public:
  // `send` hands a command to net_task (device: core_link::sendCommand); false = queue full.
  explicit ServiceScreenFlows(CommandSender send);

  // ---- The firmware-update screen: first in every pass.
  // While it owns the display the alert message closes and nothing pending is
  // drawn; when a failure has been shown long enough the screens come back.
  ServiceOutcome afterOtaStep(const OtaScreenStep &s);

  // ---- The update question, after a screenshot.
  // True = it is open and runs this pass; an install started from the
  // settings page meanwhile closes it first (the OTA screen follows).
  bool promptRuns(bool installPending);
  // Sends YES / NO (or keeps it until the queue has room); while open it owns
  // the display, the pending events stay.
  ServiceOutcome afterPromptStep(const PromptStep &s);

  // ---- The alert message, after the update question.
  // Its finger (and the CLOSE tap) is no tap below; closed: the screens come
  // back with a fresh auto-cycle; open: it owns the display, nothing pending.
  ServiceOutcome afterAlertStep(const AlertFlowStep &s);

  // ---- On the normal screens.
  // The update question may be asked now: no message or question up, no install running.
  bool mayAskAboutUpdate(bool installPending) const;
  // A normal screen was asked for (screenshot): closes the alert message the
  // way its CLOSE does; nothing when it isn't open.
  ServiceOutcome closeAlertScreen();
  // A screenshot's demo screen was drawn over everything: the alert message
  // closes, nothing pending is drawn, the screens come back in full after it.
  ServiceOutcome screenshotDemoShown();
  // After the tap of a normal pass: a message or question it opened owns the
  // display, nothing pending is drawn.
  ServiceOutcome afterNormalTap() const;
  // Every pass: a command still waiting for the queue.
  void retryCommand();
  // Full brightness: the update question, or the OTA screen (ota_screen_flow).
  bool holdsBacklight(const OtaProgress &o, bool restartPending) const;
  // The question's timer strip and the message's CLOSE countdown, while up.
  void addWakeTimes(UiWake &w, uint32_t nowMs) const;

  OtaScreenFlow &otaFlow() { return ota_; }
  UpdatePromptFlow &promptFlow() { return prompt_; }
  AlertScreenFlow &alertFlow() { return alert_; }
  const UpdatePromptFlow &promptFlow() const { return prompt_; }
  const AlertScreenFlow &alertFlow() const { return alert_; }

 private:
  OtaScreenFlow ota_;
  UpdatePromptFlow prompt_;
  AlertScreenFlow alert_;
  CommandSender send_;
  PendingCommand pending_;
};
