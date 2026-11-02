#include "display/core/screens/flight_screen.h"

#include <cmath>

#include "display/core/screens/featured_panel.h"  // featuredPillTexts()

namespace {
// Empty-state colour — deliberately outside the urgency palette
// ("no colour-coding", docs/screens.md "Screen 2").
constexpr uint16_t kEmptyColor = THEME_CONTENT_PRIMARY;  // white
}  // namespace

int16_t flightIdentityTopPx() { return kFlightIdentityTop; }
int16_t flightIdentityHeightPx() { return kFlightIdentityHeight; }
int16_t flightCountdownTopPx() { return kFlightCountdownTop; }
int16_t flightCountdownHeightPx() { return kFlightCountdownHeight; }
int16_t flightStatusTopPx() { return kFlightStatusTop; }
int16_t flightStatusHeightPx() { return kFlightStatusHeight; }

std::string nowStatsText(const AircraftRow &row, Language lang) {
  // Same values as the featured panel's pills, distance first.
  const std::vector<std::string> p = featuredPillTexts(row, lang);
  if (p.size() == 2) return p[1] + "  " + p[0];  // "S 6km  NA ZIEMI"
  return p[2] + "  " + p[0] + "  " + p[1];       // "S 3km  236m  266km/h"
}

CountdownDisplay computeCountdownDisplay(const CpaPrediction &cpa, Language lang,
                                         bool anyAircraft, OpenSkyHealth health) {
  CountdownDisplay out;

  const float t = cpa.t_cpa_seconds;

  if (!cpa.found || t < -10.0f) {
    out.mode = CountdownDisplay::Mode::Empty;
    out.bigText = "--";
    out.suffixText = "";
    // No aircraft at all: say why — an empty sky, or a feed problem
    // (credits exhausted, login failed, OpenSky unreachable).
    out.statusLabel = tr(anyAircraft ? Str::StatusNotOverhead : emptyStateText(health, false), lang);
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
    out.caption = tr(Str::CaptionOverheadIn, lang);
    return out;
  }

  // Seconds mode (-10..60), rounded to the nearest whole second.
  out.mode = CountdownDisplay::Mode::Seconds;
  out.bigText = std::to_string(static_cast<long>(std::lround(t)));
  out.suffixText = "s";
  if (t > 0.0f) out.caption = tr(Str::CaptionOverheadIn, lang);  // still ahead

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
