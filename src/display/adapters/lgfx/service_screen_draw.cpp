#include "display/adapters/lgfx/service_screen_draw.h"

#include <vector>

#include "display/core/i18n.h"
#include "display/core/text/fit_to_width.h"
#include "display/adapters/lgfx/lcars_theme_draw.h"

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

namespace {

// The header's badge, larger: a filled circle with a black "!".
void alertIcon(LGFX &gfx, int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  gfx.fillCircle(cx, cy, r, color);
  const int16_t bar = static_cast<int16_t>(r <= 7 ? 2 : 3);
  gfx.fillRect(static_cast<int16_t>(cx - bar / 2), static_cast<int16_t>(cy - r * 2 / 3), bar,
               r, THEME_ON_FILL);
  gfx.fillRect(static_cast<int16_t>(cx - bar / 2), static_cast<int16_t>(cy + r / 2), bar, bar,
               THEME_ON_FILL);
}

TextMeasure measureWith(LGFX &gfx) {
  return [&gfx](const std::string &t) { return static_cast<int>(gfx.textWidth(t.c_str())); };
}

}  // namespace

void drawAlertScreen(LGFX &gfx, AlertSet alerts, const AlertDetails &details, uint32_t nowMs,
                     uint32_t secondsLeft) {
  const int16_t w = static_cast<int16_t>(gfx.width());
  const int16_t h = static_cast<int16_t>(gfx.height());
  const std::vector<AlertId> ids = alertsInOrder(alerts, /*displayOnly=*/true);
  const AlertScreenLayout l = alertScreenLayout(w, h, static_cast<int>(ids.size()));
  gfx.fillScreen(LCARS_BLACK);

  // Title in the colour of the worst alert.
  const uint16_t worst = alertBadgeColor(alerts.displaySeverity());
  const int16_t titleMid = static_cast<int16_t>(l.title.y + l.title.h / 2);
  alertIcon(gfx, static_cast<int16_t>(l.title.x + 11), titleMid, 11, worst);
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(worst, LCARS_BLACK);
  gfx.drawString(tr(Str::AlertScreenTitle), l.title.x + 30, titleMid);

  constexpr int16_t kTextX = 26;  // right of the entry icon
  for (int i = 0; i < l.shown; ++i) {
    const Rect &e = l.entries[i];
    const AlertDef &def = alertDef(ids[static_cast<size_t>(i)]);
    const uint16_t c = alertBadgeColor(def.severity);
    alertIcon(gfx, static_cast<int16_t>(e.x + 8), static_cast<int16_t>(e.y + 9), 7, c);
    const int maxW = e.w - kTextX;
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(c, LCARS_BLACK);
    gfx.drawString(fitToWidth(tr(def.displayTitle), maxW, measureWith(gfx)).c_str(),
                   e.x + kTextX, e.y + 9);
    gfx.setFont(LCARS_FONT_SMALL);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    const std::string body = alertDisplayBody(def.id, details, nowMs, i18nLanguage());
    gfx.drawString(fitToWidth(body, maxW, measureWith(gfx)).c_str(), e.x + kTextX, e.y + 28);
  }
  if (l.more > 0) {
    gfx.setFont(LCARS_FONT_SMALL);
    gfx.setTextDatum(middle_left);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    const std::string more = fillTemplate(tr(Str::AlertMore), {std::to_string(l.more)});
    gfx.drawString(more.c_str(), l.moreRow.x + kTextX, l.moreRow.y + 9);
  }

  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(THEME_CONTENT_MUTED, LCARS_BLACK);
  gfx.drawString(tr(Str::AlertDetailsHint), w / 2, l.hint.y + l.hint.h / 2);
  drawAlertCloseButton(gfx, static_cast<int>(ids.size()), secondsLeft);
}

void drawAlertCloseButton(LGFX &gfx, int count, uint32_t secondsLeft) {
  const AlertScreenLayout l = alertScreenLayout(static_cast<int16_t>(gfx.width()),
                                                static_cast<int16_t>(gfx.height()), count);
  const Rect &b = l.button;
  drawPillButton(gfx, b.x, b.y, b.w, b.h, THEME_CHROME_ACCENT, THEME_ON_FILL,
                 alertCloseLabel(secondsLeft, i18nLanguage()).c_str());
}
