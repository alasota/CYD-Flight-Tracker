#include "service_screen.h"

#include <cstdio>

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

#ifdef ARDUINO

#include "i18n.h"
#include "lcars_theme.h"
#include "release_check.h"  // kUpdatePromptTimeoutMs

namespace {

// Text rows, relative to the bar.
constexpr int16_t kTitleDy = -62;
constexpr int16_t kVersionDy = -30;
constexpr int16_t kPercentDy = 34;
constexpr int16_t kFooterDy = 70;

void centred(LGFX &gfx, const char *text, int16_t y, uint16_t color) {
  gfx.setFont(LCARS_FONT_BODY);
  // Long git-describe versions ("v1.2.0-3-g1a2b3c4 → ...") drop to the small font.
  if (gfx.textWidth(text) > gfx.width() - 8) gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(color, LCARS_BLACK);
  gfx.drawString(text, gfx.width() / 2, y);
}

// Clears one full-width text row (16 px font + margin) around `y`.
void clearRow(LGFX &gfx, int16_t y) { gfx.fillRect(0, y - 10, gfx.width(), 20, LCARS_BLACK); }

void drawBar(LGFX &gfx, int percent) {
  const Rect r = serviceProgressBarRect(static_cast<int16_t>(gfx.width()),
                                        static_cast<int16_t>(gfx.height()));
  const int16_t fill = serviceProgressFillPx(percent, r.w);
  gfx.fillRect(r.x, r.y, fill, r.h, THEME_CHROME_ACCENT);
  gfx.fillRect(r.x + fill, r.y, r.w - fill, r.h, THEME_CHROME_RECESSED);
}

int16_t barY(LGFX &gfx) {
  return serviceProgressBarRect(static_cast<int16_t>(gfx.width()),
                                static_cast<int16_t>(gfx.height()))
      .y;
}

}  // namespace

void drawOtaScreen(LGFX &gfx, const std::string &fromVersion, bool fromGithub) {
  gfx.fillScreen(LCARS_BLACK);
  const int16_t y = barY(gfx);
  centred(gfx, tr(fromGithub ? Str::OtaFromGithub : Str::OtaTitle), y + kTitleDy,
          THEME_CHROME_ACCENT);
  centred(gfx, fromVersion.c_str(), y + kVersionDy, THEME_CONTENT_SECONDARY);
  drawBar(gfx, 0);
  centred(gfx, tr(Str::OtaDontPowerOff), y + kFooterDy, THEME_CHROME_INACTIVE);
}

void drawOtaProgress(LGFX &gfx, const std::string &fromVersion, const std::string &toVersion,
                     size_t written, size_t total) {
  const int16_t y = barY(gfx);
  const std::string versions = otaVersionLine(fromVersion, toVersion);
  if (!versions.empty()) {
    clearRow(gfx, y + kVersionDy);
    centred(gfx, versions.c_str(), y + kVersionDy, THEME_CONTENT_SECONDARY);
  }
  drawBar(gfx, total > 0 ? static_cast<int>(written >= total ? 100 : written * 100 / total) : -1);
  clearRow(gfx, y + kPercentDy);
  centred(gfx, otaProgressText(written, total).c_str(), y + kPercentDy, THEME_CONTENT_PRIMARY);
}

void drawOtaResult(LGFX &gfx, bool ok) {
  const int16_t y = barY(gfx);
  if (ok) drawBar(gfx, 100);
  clearRow(gfx, y + kPercentDy);
  centred(gfx, tr(ok ? Str::OtaRestarting : Str::OtaFailed), y + kPercentDy,
          ok ? LCARS_GREEN : THEME_ALERT);
}

void drawResetHoldScreen(LGFX &gfx) {
  gfx.fillScreen(LCARS_BLACK);
  const int16_t y = barY(gfx);
  centred(gfx, tr(Str::ResetTitle), y + kTitleDy, THEME_CHROME_ACCENT);
  centred(gfx, tr(Str::ResetHold), y + kVersionDy, THEME_CONTENT_PRIMARY);
  drawBar(gfx, 0);
}

void drawResetHoldProgress(LGFX &gfx, uint32_t heldMs) {
  drawBar(gfx, static_cast<int>(heldMs >= kResetHoldMs ? 100 : heldMs * 100 / kResetHoldMs));
}

void drawResetConfirmScreen(LGFX &gfx) {
  const int16_t w = static_cast<int16_t>(gfx.width());
  const int16_t h = static_cast<int16_t>(gfx.height());
  gfx.fillScreen(LCARS_BLACK);
  centred(gfx, tr(Str::ResetTitle), 50, THEME_CHROME_ACCENT);
  centred(gfx, tr(Str::ResetConfirm), 90, THEME_CONTENT_PRIMARY);
  const Rect yes = confirmButtonRect(true, w, h);
  const Rect no = confirmButtonRect(false, w, h);
  drawPillButton(gfx, yes.x, yes.y, yes.w, yes.h, THEME_ALERT, THEME_ON_FILL, tr(Str::ResetYes));
  drawPillButton(gfx, no.x, no.y, no.w, no.h, THEME_CHROME_ACCENT, THEME_ON_FILL,
                 tr(Str::ResetNo));
}

void drawResetDoneScreen(LGFX &gfx) {
  gfx.fillScreen(LCARS_BLACK);
  centred(gfx, tr(Str::ResetDone), gfx.height() / 2, LCARS_GREEN);
}

namespace {
constexpr int16_t kPromptTimerH = 4;
Rect promptTimerRect(LGFX &gfx) {
  const int16_t w = static_cast<int16_t>(gfx.width());
  const int16_t h = static_cast<int16_t>(gfx.height());
  const Rect yes = confirmButtonRect(true, w, h);
  const Rect no = confirmButtonRect(false, w, h);
  Rect r;
  r.x = yes.x;
  r.y = static_cast<int16_t>(yes.y + yes.h + 14);
  r.w = static_cast<int16_t>(no.x + no.w - yes.x);
  r.h = kPromptTimerH;
  return r;
}
}  // namespace

void drawUpdatePromptScreen(LGFX &gfx, const std::string &fromVersion,
                            const std::string &toVersion) {
  const int16_t w = static_cast<int16_t>(gfx.width());
  const int16_t h = static_cast<int16_t>(gfx.height());
  gfx.fillScreen(LCARS_BLACK);
  centred(gfx, tr(Str::UpdateTitle), 36, THEME_CHROME_ACCENT);
  centred(gfx, otaVersionLine(fromVersion, toVersion).c_str(), 66, THEME_CONTENT_PRIMARY);
  centred(gfx, tr(Str::UpdateAsk), 98, THEME_CONTENT_SECONDARY);
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(THEME_CONTENT_MUTED, LCARS_BLACK);
  gfx.drawString(tr(Str::UpdateHint), w / 2, 122);
  const Rect yes = confirmButtonRect(true, w, h);
  const Rect no = confirmButtonRect(false, w, h);
  drawPillButton(gfx, yes.x, yes.y, yes.w, yes.h, THEME_CHROME_ACCENT, THEME_ON_FILL,
                 tr(Str::ResetYes));
  drawPillButton(gfx, no.x, no.y, no.w, no.h, THEME_CHROME_INACTIVE, THEME_ON_FILL,
                 tr(Str::ResetNo));
  drawUpdatePromptTimer(gfx, kUpdatePromptTimeoutMs);
}

void drawUpdatePromptTimer(LGFX &gfx, uint32_t remainingMs) {
  const Rect r = promptTimerRect(gfx);
  const int16_t fill = promptTimerFillPx(remainingMs, kUpdatePromptTimeoutMs, r.w);
  gfx.fillRect(r.x, r.y, fill, r.h, THEME_CHROME_INACTIVE);
  gfx.fillRect(r.x + fill, r.y, r.w - fill, r.h, LCARS_BLACK);
}

#endif  // ARDUINO
