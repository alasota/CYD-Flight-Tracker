#include "display/adapters/lgfx/flight_screen_draw.h"

#include <Arduino.h>  // Serial, ESP.getFreeHeap()

#include "display/adapters/lgfx/aircraft_summary_draw.h"  // drawAircraftSummary()
#include "display/adapters/lgfx/lcars_theme_draw.h"

namespace {
constexpr int16_t kSideMargin = 10;   // identity panel x:10..(w-10)
constexpr int16_t kTextInset = 6;     // = aircraft_summary's text margin, so lines align
constexpr int16_t kStatsLineOffset = 46;  // 3rd line (identity +6, route +26)
constexpr int16_t kArrowSize = 12;
constexpr int16_t kCaptionHeight = 18;  // "NAD TOBĄ ZA" line above the digits (16px + gap)
constexpr int16_t kSuffixGap = 6;

// Big-glyph font per countdown mode. Font8 is the ~75px 7-seg-style
// numerals font (digits + ":.-" only — hence the separate suffix draw);
// Minutes mode is "smaller than the seconds giant-digit size" (docs/screens.md)
// so it uses the heading font scaled 2x instead.
const lgfx::IFont *bigFont(CountdownDisplay::Mode mode) {
  return (mode == CountdownDisplay::Mode::Minutes) ? LCARS_FONT_HEADING : &fonts::Font8;
}
uint8_t bigTextSize(CountdownDisplay::Mode mode) {
  return (mode == CountdownDisplay::Mode::Minutes) ? 2 : 1;
}
}  // namespace

void drawFlightScreen(LGFX &gfx, const AircraftRow &nearest, bool hasNearest,
                      const CpaPrediction &cpa, int16_t screenWidth, FeedHealth health) {
  const int16_t cx = static_cast<int16_t>(screenWidth / 2);

  // ---- Identity panel (kFlightIdentityTop, kFlightIdentityHeight) — text only, no frame (same as Screen 1).
  const int16_t frameW = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  if (hasNearest) {
    drawAircraftSummary(gfx, nearest, kSideMargin, kFlightIdentityTop, frameW, kFlightIdentityHeight);

    // Third line: "where to look" arrow + distance / altitude / speed.
    const int16_t statsY = static_cast<int16_t>(kFlightIdentityTop + kStatsLineOffset);
    const int16_t textX = static_cast<int16_t>(kSideMargin + kTextInset);
    int16_t statsX = textX;
    if (nearest.has_distance) {
      drawDirectionArrow(gfx, nearest.bearing_deg, static_cast<int16_t>(textX + kArrowSize / 2),
                         static_cast<int16_t>(statsY + LCARS_FONT_BODY_HEIGHT_PX / 2), kArrowSize,
                         THEME_CONTENT_PRIMARY);
      statsX = static_cast<int16_t>(textX + kArrowSize + 6);
    }
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(top_left);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    gfx.drawString(nowStatsText(nearest, i18nLanguage()).c_str(), statsX, statsY);
  } else {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    gfx.drawString(tr(emptyStateText(health, true)), cx,
                   static_cast<int16_t>(kFlightIdentityTop + kFlightIdentityHeight / 2));
  }

  drawFlightCountdown(gfx, hasNearest, cpa, screenWidth, health);
}

namespace {

// Draws the countdown zone + status strip into `gfx` with its top edge at
// local y = `originY` (0 for the sprite, kFlightCountdownTop for direct drawing).
// Clears its own background first.
void renderCountdown(lgfx::LovyanGFX &gfx, const CountdownDisplay &cd, int16_t originY,
                     int16_t screenWidth) {
  const int16_t cx = static_cast<int16_t>(screenWidth / 2);
  const int16_t yOff = static_cast<int16_t>(originY - kFlightCountdownTop);

  gfx.fillRect(0, originY, screenWidth, kFlightStatusTop + kFlightStatusHeight - kFlightCountdownTop, LCARS_BLACK);

  // ---- Optional caption ("NAD TOBĄ ZA", review #10) at the top of the zone;
  // the digits are then centred in what's left below it.
  int16_t digitsTop = kFlightCountdownTop;
  if (!cd.caption.empty()) {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextSize(1);
    gfx.setTextDatum(top_center);
    gfx.setTextColor(THEME_CONTENT_MUTED, LCARS_BLACK);
    gfx.drawString(cd.caption.c_str(), cx, static_cast<int16_t>(kFlightCountdownTop + yOff));
    digitsTop = static_cast<int16_t>(kFlightCountdownTop + kCaptionHeight);
  }

  // ---- Countdown zone (kFlightCountdownTop, kFlightCountdownHeight) — big number, small suffix, centered.
  const int16_t countdownCy = static_cast<int16_t>(
      digitsTop + (kFlightCountdownTop + kFlightCountdownHeight - digitsTop) / 2 + yOff);

  gfx.setFont(bigFont(cd.mode));
  gfx.setTextSize(bigTextSize(cd.mode));
  const int16_t numW = static_cast<int16_t>(gfx.textWidth(cd.bigText.c_str()));

  int16_t suffixW = 0;
  if (!cd.suffixText.empty()) {
    gfx.setFont(LCARS_FONT_HEADING);
    gfx.setTextSize(1);
    suffixW = static_cast<int16_t>(kSuffixGap + gfx.textWidth(cd.suffixText.c_str()));
  }

  const int16_t startX = static_cast<int16_t>(cx - (numW + suffixW) / 2);

  gfx.setTextColor(cd.color, LCARS_BLACK);
  gfx.setFont(bigFont(cd.mode));
  gfx.setTextSize(bigTextSize(cd.mode));
  gfx.setTextDatum(middle_left);
  gfx.drawString(cd.bigText.c_str(), startX, countdownCy);

  if (!cd.suffixText.empty()) {
    gfx.setFont(LCARS_FONT_HEADING);
    gfx.setTextSize(1);
    gfx.setTextDatum(middle_left);
    gfx.drawString(cd.suffixText.c_str(), static_cast<int16_t>(startX + numW + kSuffixGap),
                   countdownCy);
  }
  gfx.setTextSize(1);  // don't leak scale to later draws

  // ---- Status strip (kFlightStatusTop, kFlightStatusHeight) — label matching the countdown state.
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(cd.color, LCARS_BLACK);
  gfx.drawString(cd.statusLabel.c_str(), cx,
                 static_cast<int16_t>(kFlightStatusTop + kFlightStatusHeight / 2 + yOff));
}

// Off-screen buffer for the countdown zone + status strip,
// created once — at boot via flightScreenReserveSprite(), before WiFi and the
// network task start allocating — and kept for the life of the program.
// Created on the first visit to the NOW screen instead, it landed in the
// middle of the largest free block and split it (110 KB -> 41 KB), below the
// 50 KB release_check needs for a verified TLS session. 8-bit colour
// (RGB332) halves it to ~45 KB at 320px wide — the palette colours are
// quantized but stay recognisable on a black background — which matters
// once WiFi + TLS have taken their share of the heap.
LGFX_Sprite countdownSprite;
bool countdownSpriteTried = false;

}  // namespace

void flightScreenReserveSprite(int16_t screenWidth) {
  if (countdownSpriteTried) return;
  countdownSpriteTried = true;
  const int16_t zoneH = static_cast<int16_t>(kFlightStatusTop + kFlightStatusHeight - kFlightCountdownTop);
  countdownSprite.setColorDepth(8);
  if (countdownSprite.createSprite(screenWidth, zoneH) == nullptr) {
    Serial.printf("[flight_screen] countdown sprite alloc failed (free heap %u) - drawing direct\n",
                  static_cast<unsigned>(ESP.getFreeHeap()));
  }
}

void drawFlightCountdown(LGFX &gfx, bool hasNearest, const CpaPrediction &cpa,
                         int16_t screenWidth, FeedHealth health) {
  const CountdownDisplay cd = computeCountdownDisplay(hasNearest ? cpa : CpaPrediction{},
                                                      i18nLanguage(), hasNearest, health);
  flightScreenReserveSprite(screenWidth);  // no-op after boot

  if (countdownSprite.getBuffer() != nullptr) {
    renderCountdown(countdownSprite, cd, 0, screenWidth);
    countdownSprite.pushSprite(&gfx, 0, kFlightCountdownTop);
  } else {
    renderCountdown(gfx, cd, kFlightCountdownTop, screenWidth);
  }
}
