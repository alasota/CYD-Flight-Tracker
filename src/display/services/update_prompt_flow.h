// update_prompt_flow — the display's "new version — install it?" question
// (spec/done/02-github-release-check.md): opened at most once per boot when
// shouldShowUpdatePrompt() lets it, answered by a fresh tap on YES / NO
// (acceptPromptTap()), gone after kUpdatePromptTimeoutMs; its timer strip is
// redrawn every kPromptTimerStepMs. YES / NO become a NetCommand for net_task.
//
// Pure — tested under `pio test -e native`; the device half (touch read,
// drawing) is display/adapters/service_flows.
#pragma once

#include <cstdint>
#include <string>

#include "admin/core/firmware/release_check.h"  // PromptGate, kUpdatePromptTimeoutMs
#include "platform/core_link.h"                 // NetCommand, kNoDeadline

constexpr uint32_t kPromptTimerStepMs = 250;

struct PromptInput {
  uint32_t nowMs = 0;
  bool pressed = false;
  int16_t x = 0, y = 0;
  int16_t screenW = 320, screenH = 240;
};

enum class PromptAnswer : uint8_t { None, Install, Skip, TimedOut };

struct PromptStep {
  PromptAnswer answer = PromptAnswer::None;
  NetCommand command;        // InstallTag / SkipTag with the question's tag; None otherwise
  bool firstDraw = false;    // closed: the normal screens come back in full
  bool redrawTimer = false;  // drawUpdatePromptTimer(remainingMs)
  uint32_t remainingMs = 0;
};

class UpdatePromptFlow {
 public:
  // Opens the question when shouldShowUpdatePrompt(gate) — gate's
  // alreadyPromptedThisBoot is filled in here. True = opened: draw it.
  bool tryOpen(PromptGate gate, const std::string &tag, uint32_t nowMs);
  // One UI pass while it's open.
  PromptStep step(const PromptInput &in);
  // An install was started from the settings page meanwhile: closes without
  // a command and without a redraw (the OTA screen takes over).
  PromptStep supersededByInstall();
  bool active() const;
  const std::string &tag() const;
  // Ms until the timer strip is due (UiWake::untilPromptTimerMs); kNoDeadline when closed.
  uint32_t msUntilTimer(uint32_t nowMs) const;

 private:
  bool active_ = false;
  bool promptedThisBoot_ = false;
  bool released_ = false;
  bool wasPressed_ = false;
  uint32_t shownAtMs_ = 0;
  uint32_t timerDrawnMs_ = 0;
  std::string tag_;
};
