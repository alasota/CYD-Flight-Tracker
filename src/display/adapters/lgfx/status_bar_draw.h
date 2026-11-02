// status_bar_draw — the LovyanGFX half of status_bar: paints the header strip
// with the texts and zones status_bar computes (docs/screens.md "Screen chrome").
// Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>
#include <ctime>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/status_bar.h"

// Draws the full header bar into y:0..LCARS_HEADER_HEIGHT across
// `screenWidth` px, laid out by statusBarZones(): app name, health dot
// (statusBarHealthColor()) + count (statusBarBlockLabel()), date + clock, and
// a 1px lilac rule along the bottom edge. `localEpoch` is a
// time_sync::timeSyncNowLocal() value and `timeSynced` is
// time_sync::isTimeSynced() — together they decide real text vs.
// placeholder. Repaints its own black background first and touches nothing
// below the header, so it's safe to call on its own (CLAUDE.md "Redraw
// policy": minute rollover, health change, new poll).
// `badge` != None adds the device-alert "!" right of the app name.
void drawStatusBar(LGFX &gfx, int aircraftCount, bool hasData, OpenSkyHealth health,
                   AlertSeverity badge, std::time_t localEpoch, bool timeSynced,
                   int16_t screenWidth);
