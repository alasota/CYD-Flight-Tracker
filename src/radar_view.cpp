#include "radar_view.h"

#include <algorithm>
#include <cmath>

namespace {
// Same km-per-degree approximation table_view's computeDistanceBearing()
// (haversine) is built on, for short distances.
constexpr float kKmPerDegree = 111.2f;
}  // namespace

RadarLayout computeRadarLayout(int16_t x, int16_t y, int16_t w, int16_t h, int16_t margin) {
  RadarLayout layout;

  int16_t usableW = static_cast<int16_t>(w - 2 * margin);
  int16_t usableH = static_cast<int16_t>(h - 2 * margin);
  int16_t diameter = std::min(usableW, usableH);
  if (diameter < 0) diameter = 0;

  layout.radius_px = static_cast<int16_t>(diameter / 2);
  // Round the centre to nearest so an odd content height (y:18..215 -> 197)
  // lands on CLAUDE.md's stated (95, 117) rather than 1px low.
  layout.center_x = static_cast<int16_t>(x + (w + 1) / 2);
  layout.center_y = static_cast<int16_t>(y + (h + 1) / 2);
  return layout;
}

float radiusDegToKm(float radius_deg) { return radius_deg * kKmPerDegree; }

RadarPoint ringLabelAnchor(int16_t cx, int16_t cy, int16_t radiusPx) {
  constexpr float kCos45 = 0.70710678f;
  RadarPoint p;
  p.x = static_cast<int16_t>(std::lround(cx + radiusPx * kCos45));
  p.y = static_cast<int16_t>(std::lround(cy - radiusPx * kCos45));
  return p;
}

RadarPoint headingTickEnd(int16_t x, int16_t y, float trackDeg, int16_t len) {
  const float rad = trackDeg * 3.14159265f / 180.0f;
  RadarPoint p;
  p.x = static_cast<int16_t>(std::lround(x + len * std::sin(rad)));
  p.y = static_cast<int16_t>(std::lround(y - len * std::cos(rad)));  // screen y grows down
  return p;
}

std::string radarSummaryStats(const AircraftRow &row, Language lang) {
  // Same values as the featured panel's chips: distance (with compass point)
  // first, then altitude — or "NA ZIEMI".
  const std::vector<std::string> p = featuredPillTexts(row, lang);
  if (p.size() == 2) return p[1] + "  " + p[0];  // "S 4km  NA ZIEMI"
  return p[2] + "  " + p[0];                     // "S 4km  511m"
}

#ifdef ARDUINO

#include <cmath>
#include <cstdio>

#include "aircraft_summary.h"
#include "lcars_theme.h"

namespace {

// Fixed content-area layout on the 320x240 frame — CLAUDE.md "Screen 3".
constexpr int16_t kContentTop = LCARS_CONTENT_TOP;                          // 18
constexpr int16_t kContentHeight = LCARS_CONTENT_BOTTOM - LCARS_CONTENT_TOP;  // 197 (y:18..215)

constexpr int16_t kRadarZoneX = 0;
constexpr int16_t kRadarZoneW = 190;
constexpr int16_t kRadarMargin = 10;  // -> center (95,117), radius 85

// No divider between radar and summary any more (removed on request) —
// the summary text just starts a little right of the radar zone.
constexpr int16_t kSummaryX = 200;
constexpr int16_t kSummaryW = 120;  // x:200..320
constexpr int16_t kSummaryInset = 3;
constexpr int16_t kSummaryTextX = 208;
constexpr int16_t kSummaryFlightY = kContentTop + 40;    // 58  — five rows centred
constexpr int16_t kSummaryAirlineY = kContentTop + 74;   // 92    in the content area
constexpr int16_t kSummaryFromY = kContentTop + 100;     // 118  origin city
constexpr int16_t kSummaryToY = kContentTop + 120;       // 138  "-> destination city"
constexpr int16_t kSummaryStatsY = kContentTop + 150;    // 168  "S 4km  511m" (review #8)
// Text width between kSummaryTextX and the panel's right edge (less kSummaryInset),
// minus a 2px breathing gap — what fitToWidth() cuts rows 2/3 down to.
constexpr int kSummaryTextMaxPx = kSummaryX + kSummaryW - kSummaryInset - 2 - kSummaryTextX;

constexpr int kRingCount = 3;
constexpr int16_t kBlipRadius = 4;
constexpr int16_t kHeadingTick = 11;   // heading line from the blip centre, px
constexpr int16_t kSelectedRing = 8;   // white ring around the summary-panel aircraft
constexpr int16_t kGroundMarker = 4;   // small square for aircraft on the ground
constexpr int16_t kClampedBlipRadius = 3;
// Muted lavender for out-of-range (clamped) blips — deliberately not the
// sky of in-range blips, so "off the scale" reads distinct.
constexpr uint16_t kClampedBlipColor = THEME_CONTENT_MUTED;
constexpr int16_t kHomeCrosshair = 5;

void drawRadar(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg) {
  RadarLayout layout =
      computeRadarLayout(kRadarZoneX, kContentTop, kRadarZoneW, kContentHeight, kRadarMargin);
  if (layout.radius_px <= 0) return;

  const float maxKm = radiusDegToKm(radius_deg);
  const std::vector<float> ringsKm = computeRingDistances(maxKm, kRingCount);

  // Concentric rings at r = radius_px * 1/3, 2/3, 3/3. Each "Nkm" label sits
  // on its ring at 45 degrees up-right in the small font (review #5: at
  // 12 o'clock, blips north of home kept covering them).
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(bottom_left);
  gfx.setTextColor(THEME_CONTENT_MUTED, LCARS_BLACK);
  for (int i = 0; i < kRingCount; ++i) {
    // Round to nearest so radius 85 lands on the spec's 28/57/85, not 28/56/85.
    int16_t ringR = static_cast<int16_t>(
        (layout.radius_px * (i + 1) + kRingCount / 2) / kRingCount);
    if (ringR <= 0) continue;
    gfx.drawCircle(layout.center_x, layout.center_y, ringR, THEME_CONTENT_MUTED);
    char label[12];
    std::snprintf(label, sizeof(label), "%dkm",
                  static_cast<int>(std::lround(ringsKm[static_cast<size_t>(i)])));
    const RadarPoint at = ringLabelAnchor(layout.center_x, layout.center_y, ringR);
    gfx.drawString(label, static_cast<int16_t>(at.x + 2), static_cast<int16_t>(at.y - 1));
  }

  // "N" above the outer ring: the plot is north-up (review #7).
  gfx.setTextDatum(bottom_center);
  gfx.drawString("N", layout.center_x,
                 static_cast<int16_t>(layout.center_y - layout.radius_px - 1));

  // Aircraft. Ground traffic first, as small muted squares (review #6: the
  // apron at EPWA used to turn into one big blob next to home); then
  // airborne aircraft as blips with a heading tick; the one shown in the
  // summary panel (rows.front()) gets a white ring (review #7).
  for (const AircraftRow &row : rows) {
    if (!row.has_distance || !row.aircraft.on_ground) continue;
    const ScreenPoint p = polarToScreen(row.bearing_deg, row.distance_km, maxKm, layout.center_x,
                                        layout.center_y, layout.radius_px);
    gfx.fillRect(static_cast<int16_t>(p.x - kGroundMarker / 2),
                 static_cast<int16_t>(p.y - kGroundMarker / 2), kGroundMarker, kGroundMarker,
                 THEME_CONTENT_MUTED);
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    const AircraftRow &row = rows[i];
    if (!row.has_distance || row.aircraft.on_ground) continue;
    const ScreenPoint p = polarToScreen(row.bearing_deg, row.distance_km, maxKm, layout.center_x,
                                        layout.center_y, layout.radius_px);
    if (p.clamped) {
      gfx.drawCircle(p.x, p.y, kClampedBlipRadius, kClampedBlipColor);
      continue;
    }
    const RadarPoint tip = headingTickEnd(p.x, p.y, row.aircraft.true_track, kHeadingTick);
    gfx.drawWideLine(p.x, p.y, tip.x, tip.y, 1.0f, THEME_CONTENT_SECONDARY);
    gfx.fillCircle(p.x, p.y, kBlipRadius, THEME_CONTENT_SECONDARY);
    if (i == 0) gfx.drawCircle(p.x, p.y, kSelectedRing, THEME_CONTENT_PRIMARY);
  }

  // Home crosshair (drawn last so nothing obscures it).
  gfx.drawFastHLine(static_cast<int16_t>(layout.center_x - kHomeCrosshair), layout.center_y,
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.drawFastVLine(layout.center_x, static_cast<int16_t>(layout.center_y - kHomeCrosshair),
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.fillCircle(layout.center_x, layout.center_y, 2, LCARS_YELLOW);
}

void drawSummaryPanel(LGFX &gfx, const std::vector<AircraftRow> &rows, OpenSkyHealth health) {
  // No frame and no divider (both dropped by request). Keep any long text
  // from spilling past the panel into the radar.
  gfx.setClipRect(static_cast<int16_t>(kSummaryX + kSummaryInset), kContentTop,
                  static_cast<int16_t>(kSummaryW - 2 * kSummaryInset), kContentHeight);

  if (rows.empty()) {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    gfx.drawString(tr(emptyStateText(health, true)), static_cast<int16_t>(kSummaryX + kSummaryW / 2),
                   static_cast<int16_t>(kContentTop + kContentHeight / 2));
    gfx.clearClipRect();
    return;
  }

  const AircraftRow &nearest = rows.front();

  gfx.setTextDatum(top_left);

  // Row 1: callsign, emphasized.
  gfx.setFont(LCARS_FONT_HEADING);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(summaryFlight(nearest).c_str(), kSummaryTextX, kSummaryFlightY);

  // Row 2: airline, cut to the panel's text width (setClipRect above is
  // only the pixel-level backstop).
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  const TextMeasure measure = gfxTextMeasure(gfx);
  gfx.drawString(fitToWidth(summaryAirline(nearest), kSummaryTextMaxPx, measure).c_str(),
                 kSummaryTextX, kSummaryAirlineY);

  // Rows 3-4: route as city names, one per line (the panel is too narrow
  // for "Warszawa -> Rzym" on one line): "Warszawa" / "-> Rzym".
  gfx.drawString(fitToWidth(rowPlaceName(nearest, /*origin=*/true, i18nLanguage()),
                            kSummaryTextMaxPx, measure)
                     .c_str(),
                 kSummaryTextX, kSummaryFromY);
  if (nearest.route.found) {
    gfx.drawString(fitToWidth("-> " + rowPlaceName(nearest, /*origin=*/false, i18nLanguage()),
                              kSummaryTextMaxPx, measure)
                       .c_str(),
                   kSummaryTextX, kSummaryToY);
  }

  // Row 5: where it is and how high — "S 4km  511m" / "S 4km  NA ZIEMI".
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(fitToWidth(radarSummaryStats(nearest, i18nLanguage()), kSummaryTextMaxPx, measure)
                     .c_str(),
                 kSummaryTextX, kSummaryStatsY);

  gfx.clearClipRect();
}

}  // namespace

void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   int16_t screenWidth, OpenSkyHealth health) {
  (void)screenWidth;  // fixed 320px layout — parameter kept for signature symmetry

  drawRadar(gfx, rows, radius_deg);
  drawSummaryPanel(gfx, rows, health);
}

#endif  // ARDUINO
