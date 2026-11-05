#include "display/services/update_prompt_flow.h"

#include "display/core/screens/service_screen.h"  // confirmHit()
#include "display/services/ui_flow.h"               // netCpaAt()

PromptGate updatePromptGate(const NetState &s, int currentScreen, uint32_t nowMs,
                            bool touchActive, bool screenOff, bool alertScreenOpen) {
  PromptGate g;
  g.status = s.update.status;
  g.latestTag = s.update.latestTag;
  g.skippedTag = s.cfg->update_skipped_tag;
  g.installedIsDev = isDevelopmentBuild(parseFwVersion(s.update.installed));
  g.homeConfigured = s.cfg->home_configured;
  g.currentScreen = currentScreen;
  const CpaPrediction cpa = netCpaAt(s, nowMs);
  g.cpaFound = cpa.found;
  g.tCpaSeconds = static_cast<int>(cpa.t_cpa_seconds);
  g.touchActive = touchActive;
  g.screenOff = screenOff;
  g.alertScreenOpen = alertScreenOpen;
  return g;
}

bool UpdatePromptFlow::tryOpen(PromptGate gate, const std::string &tag, uint32_t nowMs) {
  gate.alreadyPromptedThisBoot = promptedThisBoot_;
  if (!shouldShowUpdatePrompt(gate)) return false;
  active_ = true;
  promptedThisBoot_ = true;
  released_ = false;
  wasPressed_ = true;  // a finger already down when it appears is no answer
  shownAtMs_ = nowMs;
  timerDrawnMs_ = nowMs;
  tag_ = tag;
  return true;
}

PromptStep UpdatePromptFlow::step(const PromptInput &in) {
  PromptStep r;
  const uint32_t shownFor = in.nowMs - shownAtMs_;
  if (!in.pressed) released_ = true;
  if (in.pressed && !wasPressed_ && acceptPromptTap(released_, shownFor)) {
    const ResetChoice c = confirmHit(in.x, in.y, in.screenW, in.screenH);
    if (c != ResetChoice::None) {
      // YES: net_task starts the install, its OTA screen follows (if it no
      // longer can, the normal screens come back). NO: skip this tag.
      const bool yes = c == ResetChoice::Yes;
      active_ = false;
      r.answer = yes ? PromptAnswer::Install : PromptAnswer::Skip;
      r.command = makeNetCommand(yes ? NetCommandType::InstallTag : NetCommandType::SkipTag, tag_);
      r.firstDraw = true;
      return r;
    }
  }
  wasPressed_ = in.pressed;
  if (shownFor >= kUpdatePromptTimeoutMs) {
    active_ = false;  // "later": asked again after the next boot
    r.answer = PromptAnswer::TimedOut;
    r.firstDraw = true;
    return r;
  }
  if (in.nowMs - timerDrawnMs_ >= kPromptTimerStepMs) {
    timerDrawnMs_ = in.nowMs;
    r.redrawTimer = true;
    r.remainingMs = kUpdatePromptTimeoutMs - shownFor;
  }
  return r;
}

PromptStep UpdatePromptFlow::supersededByInstall() {
  active_ = false;
  return PromptStep{};
}

bool UpdatePromptFlow::active() const { return active_; }

const std::string &UpdatePromptFlow::tag() const { return tag_; }

uint32_t UpdatePromptFlow::msUntilTimer(uint32_t nowMs) const {
  return active_ ? msUntilPeriod(nowMs, timerDrawnMs_, kPromptTimerStepMs) : kNoDeadline;
}
