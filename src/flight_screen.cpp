#include "flight_screen.h"

#include <cmath>

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kIdentityTop = 28;
constexpr int16_t kIdentityHeight = 50;   // y:28..78
constexpr int16_t kCountdownTop = 82;
constexpr int16_t kCountdownHeight = 113;  // y:82..195
constexpr int16_t kStatusTop = 200;
constexpr int16_t kStatusHeight = 38;      // y:200..238

// Empty-state colour — deliberately outside the urgency palette
// ("no colour-coding" in CLAUDE.md).
constexpr uint16_t kEmptyColor = 0xFFFF;  // white
}  // namespace

int16_t flightIdentityTopPx() { return kIdentityTop; }
int16_t flightIdentityHeightPx() { return kIdentityHeight; }
int16_t flightCountdownTopPx() { return kCountdownTop; }
int16_t flightCountdownHeightPx() { return kCountdownHeight; }
int16_t flightStatusTopPx() { return kStatusTop; }
int16_t flightStatusHeightPx() { return kStatusHeight; }

CountdownDisplay computeCountdownDisplay(const CpaPrediction &cpa) {
  CountdownDisplay out;

  const float t = cpa.t_cpa_seconds;

  if (!cpa.found || t < -10.0f) {
    out.mode = CountdownDisplay::Mode::Empty;
    out.bigText = "--";
    out.suffixText = "";
    out.statusLabel = "BRAK LOTOW W ZASIEGU";
    out.color = kEmptyColor;
    out.colorCoded = false;
    return out;
  }

  if (t > 60.0f) {
    out.mode = CountdownDisplay::Mode::Minutes;
    long minutes = std::lround(t / 60.0f);
    out.bigText = "~" + std::to_string(minutes);
    out.suffixText = "MIN";
    out.statusLabel = "SZACOWANY CZAS";
    out.color = LCARS_MAGENTA;
    return out;
  }

  // Seconds mode (-10..60), rounded to the nearest whole second.
  out.mode = CountdownDisplay::Mode::Seconds;
  out.bigText = std::to_string(static_cast<long>(std::lround(t)));
  out.suffixText = "s";

  if (t > 10.0f) {
    out.color = LCARS_CYAN;
    out.statusLabel = "ZBLIZA SIE";
  } else if (t >= 0.0f) {
    out.color = LCARS_YELLOW;
    out.statusLabel = "NAD TOBA";
  } else {  // -10 <= t < 0
    out.color = LCARS_ORANGE;
    out.statusLabel = "MINAL";
  }
  return out;
}

#ifdef ARDUINO

namespace {
constexpr int16_t kSideMargin = 10;   // identity frame x:10..(w-10), per CLAUDE.md
constexpr int16_t kFrameCorner = 12;
constexpr int16_t kFrameThickness = 3;
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
                      const AirportInfo &origin, const AirportInfo &dest, const CpaPrediction &cpa,
                      int16_t screenWidth) {
  const int16_t cx = static_cast<int16_t>(screenWidth / 2);

  // ---- Identity panel, y:28..78 — same magenta elbow frame as Screen 1.
  const int16_t frameW = static_cast<int16_t>(screenWidth - 2 * kSideMargin);
  drawElbowFrame(gfx, kSideMargin, kIdentityTop, frameW, kIdentityHeight, kFrameCorner,
                 kFrameThickness, LCARS_MAGENTA);

  if (hasNearest) {
    drawAircraftSummary(gfx, nearest, origin, dest, RouteFormat::WithCountry, kSideMargin,
                        kIdentityTop, frameW, kIdentityHeight);
  } else {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
    gfx.drawString("BRAK LOTU", cx, static_cast<int16_t>(kIdentityTop + kIdentityHeight / 2));
  }

  const CountdownDisplay cd =
      computeCountdownDisplay(hasNearest ? cpa : CpaPrediction{});

  // ---- Countdown zone, y:82..195 — big number, small suffix, centered.
  const int16_t countdownCy = static_cast<int16_t>(kCountdownTop + kCountdownHeight / 2);

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

  // ---- Status strip, y:200..238 — label matching the countdown state.
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(cd.color, LCARS_BLACK);
  gfx.drawString(cd.statusLabel.c_str(), cx, static_cast<int16_t>(kStatusTop + kStatusHeight / 2));
}

#endif  // ARDUINO
