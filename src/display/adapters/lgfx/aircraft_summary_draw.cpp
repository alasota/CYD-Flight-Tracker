#include "display/adapters/lgfx/aircraft_summary_draw.h"

#include "display/core/theme/lcars_theme.h"

namespace {
constexpr int16_t kTextMargin = 6;
constexpr int16_t kLineHeight = 20;
constexpr int16_t kIconGap = 6;  // min space between identity text and phase icon
}  // namespace

void drawIdentityLine(LGFX &gfx, const AircraftRow &row, int16_t x, int16_t y, int16_t rightX) {
  gfx.setFont(LCARS_FONT_BODY);

  // Phase icon first: a small aircraft pinned to the right edge, vertically
  // centred on the 16px text line (the slot is reserved even with no phase,
  // so the text never shifts when the phase changes).
  drawPhaseIcon(gfx, phaseIconKind(row.phase, row.aircraft.on_ground),
                static_cast<int16_t>(rightX - kPhaseIconW / 2),
                static_cast<int16_t>(y + LCARS_FONT_BODY_HEIGHT_PX / 2));
  const int iconW = kPhaseIconW;

  // Then the identity text, fitted into what's left: airline/type in the
  // secondary colour, then the callsign re-drawn over its own prefix in
  // the primary colour (bitmap font, no kerning — the glyphs land on
  // exactly the same pixels, so this just recolours it).
  const int maxPx = (rightX - iconW - kIconGap) - x;
  gfx.setTextDatum(top_left);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(fitSummaryIdentity(row, maxPx, gfxTextMeasure(gfx)).c_str(), x, y);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(summaryFlight(row).c_str(), x, y);
}

void drawAircraftSummary(LGFX &gfx, const AircraftRow &row, int16_t x, int16_t y, int16_t w,
                         int16_t h) {
  (void)h;

  const int16_t textX = static_cast<int16_t>(x + kTextMargin);
  const int16_t rightX = static_cast<int16_t>(x + w - kTextMargin);

  drawIdentityLine(gfx, row, textX, static_cast<int16_t>(y + kTextMargin), rightX);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(
      formatRowRouteCities(row, i18nLanguage(), rightX - textX, gfxTextMeasure(gfx)).c_str(),
      textX, static_cast<int16_t>(y + kTextMargin + kLineHeight));
}
