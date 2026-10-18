#include "flight_screen.h"

#include <cmath>

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kIdentityTop = LCARS_CONTENT_TOP;  // 18
constexpr int16_t kIdentityHeight = 50;   // y:18..68
constexpr int16_t kCountdownTop = kIdentityTop + kIdentityHeight + 4;  // 72
constexpr int16_t kCountdownHeight = 113;  // y:72..185
constexpr int16_t kStatusTop = LCARS_CONTENT_BOTTOM - 25;  // 190
constexpr int16_t kStatusHeight = 25;      // y:190..215 (bottom nav: 215..240)

// Empty-state colour — deliberately outside the urgency palette
// ("no colour-coding" in CLAUDE.md).
constexpr uint16_t kEmptyColor = THEME_CONTENT_PRIMARY;  // white
}  // namespace

int16_t flightIdentityTopPx() { return kIdentityTop; }
int16_t flightIdentityHeightPx() { return kIdentityHeight; }
int16_t flightCountdownTopPx() { return kCountdownTop; }
int16_t flightCountdownHeightPx() { return kCountdownHeight; }
int16_t flightStatusTopPx() { return kStatusTop; }
int16_t flightStatusHeightPx() { return kStatusHeight; }

CountdownDisplay computeCountdownDisplay(const CpaPrediction &cpa, Language lang) {
  CountdownDisplay out;

  const float t = cpa.t_cpa_seconds;

  if (!cpa.found || t < -10.0f) {
    out.mode = CountdownDisplay::Mode::Empty;
    out.bigText = "--";
    out.suffixText = "";
    out.statusLabel = tr(Str::NoFlightsInRange, lang);
    out.color = kEmptyColor;
    out.colorCoded = false;
    return out;
  }

  if (t > 60.0f) {
    out.mode = CountdownDisplay::Mode::Minutes;
    long minutes = std::lround(t / 60.0f);
    out.bigText = "~" + std::to_string(minutes);
    out.suffixText = tr(Str::UnitMinutes, lang);
    out.statusLabel = tr(Str::StatusEstimated, lang);
    out.color = LCARS_LILAC;
    return out;
  }

  // Seconds mode (-10..60), rounded to the nearest whole second.
  out.mode = CountdownDisplay::Mode::Seconds;
  out.bigText = std::to_string(static_cast<long>(std::lround(t)));
  out.suffixText = "s";

  if (t > 10.0f) {
    out.color = LCARS_SKY;
    out.statusLabel = tr(Str::StatusApproaching, lang);
  } else if (t >= 0.0f) {
    out.color = LCARS_YELLOW;
    out.statusLabel = tr(Str::StatusOverhead, lang);
  } else {  // -10 <= t < 0
    out.color = LCARS_PEACH;
    out.statusLabel = tr(Str::StatusPassed, lang);
  }
  return out;
}

#ifdef ARDUINO

#include <Arduino.h>  // Serial, ESP.getFreeHeap()

namespace {
constexpr int16_t kSideMargin = 10;   // identity panel x:10..(w-10), per CLAUDE.md
constexpr int16_t kSuffixGap = 6;

// Big-glyph font per countdown mode. Font8 is the ~75px 7-seg-style
// numerals font (digits + ":.-" only — hence the separate suffix draw);
// Minutes mode is "smaller than the seconds giant-digit size" (CLAUDE.md)
// so it uses the heading font scaled 2x instead.
const lgfx::IFont *bigFont(CountdownDisplay::Mode mode) {
  return (mode == CountdownDisplay::Mode::Minutes) ? LCARS_FONT_HEADING : &fonts::Font8;
}
uint8_t bigTextSize(CountdownDisplay::Mode mode) {
  return (mode == CountdownDisplay::Mode::Minutes) ? 2 : 1;
}
}  // namespace

void drawFlightScreen(LGFX &gfx, const AircraftRow &nearest, bool hasNearest,
                      const CpaPrediction &cpa, int16_t screenWidth) {
  const int16_t cx = static_cast<int16_t>(screenWidth / 2);

  // ---- Identity panel, y:28..78 — text only, no frame (same as Screen 1).
  const int16_t frameW = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  if (hasNearest) {
    drawAircraftSummary(gfx, nearest, kSideMargin,
                        kIdentityTop, frameW, kIdentityHeight);
  } else {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    gfx.drawString(tr(Str::NoFlight), cx, static_cast<int16_t>(kIdentityTop + kIdentityHeight / 2));
  }

  drawFlightCountdown(gfx, hasNearest, cpa, screenWidth);
}

namespace {

// Draws the countdown zone + status strip into `gfx` with its top edge at
// local y = `originY` (0 for the sprite, kCountdownTop for direct drawing).
// Clears its own background first.
void renderCountdown(lgfx::LovyanGFX &gfx, const CountdownDisplay &cd, int16_t originY,
                     int16_t screenWidth) {
  const int16_t cx = static_cast<int16_t>(screenWidth / 2);
  const int16_t yOff = static_cast<int16_t>(originY - kCountdownTop);

  gfx.fillRect(0, originY, screenWidth, kStatusTop + kStatusHeight - kCountdownTop, LCARS_BLACK);

  // ---- Countdown zone, y:82..195 — big number, small suffix, centered.
  const int16_t countdownCy =
      static_cast<int16_t>(kCountdownTop + kCountdownHeight / 2 + yOff);

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

  // ---- Status strip, y:200..225 — label matching the countdown state.
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(cd.color, LCARS_BLACK);
  gfx.drawString(cd.statusLabel.c_str(), cx,
                 static_cast<int16_t>(kStatusTop + kStatusHeight / 2 + yOff));
}

// Off-screen buffer for the countdown zone + status strip (y:82..225),
// created on first use and kept for the life of the program. 8-bit colour
// (RGB332) halves it to ~45 KB at 320px wide — the palette colours are
// quantized but stay recognisable on a black background — which matters
// once WiFi + TLS have taken their share of the heap.
LGFX_Sprite countdownSprite;
bool countdownSpriteTried = false;

}  // namespace

void drawFlightCountdown(LGFX &gfx, bool hasNearest, const CpaPrediction &cpa,
                         int16_t screenWidth) {
  const CountdownDisplay cd =
      computeCountdownDisplay(hasNearest ? cpa : CpaPrediction{}, i18nLanguage());
  const int16_t zoneH = static_cast<int16_t>(kStatusTop + kStatusHeight - kCountdownTop);

  if (!countdownSpriteTried) {
    countdownSpriteTried = true;
    countdownSprite.setColorDepth(8);
    if (countdownSprite.createSprite(screenWidth, zoneH) == nullptr) {
      Serial.printf("[flight_screen] countdown sprite alloc failed (free heap %u) - drawing direct\n",
                    static_cast<unsigned>(ESP.getFreeHeap()));
    }
  }

  if (countdownSprite.getBuffer() != nullptr) {
    renderCountdown(countdownSprite, cd, 0, screenWidth);
    countdownSprite.pushSprite(&gfx, 0, kCountdownTop);
  } else {
    renderCountdown(gfx, cd, kCountdownTop, screenWidth);
  }
}

#endif  // ARDUINO
