#include "display/core/screens/service_screen.h"

#include <cstdio>

#include "display/core/i18n.h"
#include "admin/core/firmware/release_check.h"  // acceptPromptTap()

namespace {
constexpr int16_t kBarW = 240;
constexpr int16_t kBarH = 14;
constexpr int16_t kButtonW = 110;
constexpr int16_t kButtonH = 44;
constexpr int16_t kButtonGap = 30;
}  // namespace

Rect serviceProgressBarRect(int16_t screenW, int16_t screenH) {
  Rect r;
  r.w = kBarW;
  r.h = kBarH;
  r.x = static_cast<int16_t>((screenW - kBarW) / 2);
  r.y = static_cast<int16_t>(screenH / 2 + 4);
  return r;
}

int16_t serviceProgressFillPx(int percent, int16_t barW) {
  if (percent <= 0) return 0;
  if (percent >= 100) return barW;
  return static_cast<int16_t>(static_cast<int32_t>(barW) * percent / 100);
}

int16_t promptTimerFillPx(uint32_t remainingMs, uint32_t timeoutMs, int16_t barW) {
  if (timeoutMs == 0 || remainingMs == 0 || barW <= 0) return 0;
  if (remainingMs >= timeoutMs) return barW;
  return static_cast<int16_t>(static_cast<uint64_t>(barW) * remainingMs / timeoutMs);
}

std::string otaProgressText(size_t written, size_t total) {
  char buf[24];
  if (total > 0) {
    const size_t pct = written >= total ? 100 : written * 100 / total;
    std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(pct));
  } else {
    std::snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>(written / 1024));
  }
  return buf;
}

std::string otaVersionLine(const std::string &from, const std::string &to) {
  if (to.empty()) return "";
  if (from.empty()) return to;
  return from + " \xE2\x86\x92 " + to;  // →
}

Rect confirmButtonRect(bool yes, int16_t screenW, int16_t screenH) {
  Rect r;
  r.w = kButtonW;
  r.h = kButtonH;
  const int16_t left = static_cast<int16_t>((screenW - 2 * kButtonW - kButtonGap) / 2);
  r.x = yes ? left : static_cast<int16_t>(left + kButtonW + kButtonGap);
  r.y = static_cast<int16_t>(screenH - kButtonH - 40);
  return r;
}

ResetChoice confirmHit(int16_t x, int16_t y, int16_t screenW, int16_t screenH) {
  if (hitTest(x, y, confirmButtonRect(true, screenW, screenH))) return ResetChoice::Yes;
  if (hitTest(x, y, confirmButtonRect(false, screenW, screenH))) return ResetChoice::No;
  return ResetChoice::None;
}

// ---- Device-alert message screen ----------------------------------------------

namespace {
constexpr int16_t kAlertMargin = 8;
constexpr int16_t kAlertTitleY = 8;
constexpr int16_t kAlertTitleH = 28;
constexpr int16_t kAlertFirstEntryY = 44;
constexpr int16_t kAlertEntryH = 40;     // title row (16 px font) + body row (10 px)
constexpr int16_t kAlertEntryStep = 42;
constexpr int16_t kAlertHintH = 18;
constexpr int16_t kAlertButtonW = 160;
constexpr int16_t kAlertButtonBottomGap = 8;

Rect makeRect(int x, int y, int w, int h) {
  Rect r;
  r.x = static_cast<int16_t>(x);
  r.y = static_cast<int16_t>(y);
  r.w = static_cast<int16_t>(w);
  r.h = static_cast<int16_t>(h);
  return r;
}
}  // namespace

AlertScreenLayout alertScreenLayout(int16_t w, int16_t h, int count) {
  AlertScreenLayout l;
  l.title = makeRect(kAlertMargin, kAlertTitleY, w - 2 * kAlertMargin, kAlertTitleH);
  if (count < 0) count = 0;
  if (count > kAlertScreenMaxRows) {
    l.shown = kAlertScreenMaxRows - 1;
    l.more = count - l.shown;
  } else {
    l.shown = count;
  }
  for (int i = 0; i < kAlertScreenMaxRows; ++i) {
    l.entries[i] = makeRect(kAlertMargin, kAlertFirstEntryY + i * kAlertEntryStep,
                            w - 2 * kAlertMargin, kAlertEntryH);
  }
  if (l.more > 0) l.moreRow = l.entries[kAlertScreenMaxRows - 1];
  l.button = makeRect((w - kAlertButtonW) / 2, h - kAlertButtonBottomGap - kAlertCloseButtonMinH,
                      kAlertButtonW, kAlertCloseButtonMinH);
  const Rect &last = l.entries[kAlertScreenMaxRows - 1];
  const int hintY = last.y + last.h + (l.button.y - (last.y + last.h) - kAlertHintH) / 2;
  l.hint = makeRect(kAlertMargin, hintY, w - 2 * kAlertMargin, kAlertHintH);
  return l;
}

AlertScreen openAlertScreen(uint32_t nowMs) {
  AlertScreen s;
  s.open = true;
  s.openedMs = nowMs;
  s.released = false;
  s.wasPressed = true;
  s.secondsShown = alertScreenSecondsLeft(s, nowMs);
  return s;
}

uint32_t alertScreenSecondsLeft(const AlertScreen &s, uint32_t nowMs) {
  const uint32_t elapsed = nowMs - s.openedMs;  // unsigned: rollover-safe
  if (elapsed >= kAlertScreenTimeoutMs) return 0;
  return (kAlertScreenTimeoutMs - elapsed + 999) / 1000;
}

uint32_t alertScreenMsToNextSecond(const AlertScreen &s, uint32_t nowMs) {
  const uint32_t elapsed = nowMs - s.openedMs;
  if (elapsed >= kAlertScreenTimeoutMs) return 0;
  const uint32_t rest = (kAlertScreenTimeoutMs - elapsed) % 1000;  // ms until it ticks
  return rest == 0 ? 1000 : rest;
}

AlertScreenStepResult alertScreenStep(const AlertScreen &s, const AlertScreenInput &in) {
  AlertScreenStepResult r;
  r.state = s;
  if (!s.open) return r;
  const uint32_t elapsed = in.nowMs - s.openedMs;
  if (!in.anyDisplayAlerts || elapsed >= kAlertScreenTimeoutMs) {
    r.state.open = false;
    r.close = true;
    return r;
  }
  if (!in.pressed) r.state.released = true;
  if (in.pressed && !s.wasPressed && acceptPromptTap(r.state.released, elapsed)) {
    const AlertScreenLayout l = alertScreenLayout(in.screenW, in.screenH, 1);
    if (hitTest(in.x, in.y, l.button)) {
      r.state.open = false;
      r.close = true;
      return r;
    }
  }
  r.state.wasPressed = in.pressed;
  const uint32_t secs = alertScreenSecondsLeft(s, in.nowMs);
  if (secs != s.secondsShown) {
    r.state.secondsShown = secs;
    r.redrawTimer = true;
  }
  return r;
}

std::string alertCloseLabel(uint32_t secondsLeft, Language lang) {
  return std::string(tr(Str::AlertClose, lang)) + "  " + std::to_string(secondsLeft) + " s";
}
