// status_bar — draws the header bar shared by all three screens, per
// CLAUDE.md "Screen chrome": an LCARS_ORANGE rounded block on the left
// labelled with the active screen name (FLIGHTS / FLIGHT / RADAR), then
// the date (DD/MM/YYYY) + the real wall-clock time (from time_sync) to its right in
// LCARS_CYAN. Occupies y:0..LCARS_HEADER_HEIGHT, full width. **No WiFi
// signal bars** — that three-zone design is explicitly superseded (see
// CLAUDE.md; rssiToBars() has no home in this layout).
//
// Called once per frame from main.cpp *before* it dispatches to whichever
// screen is active — screens draw their content starting at
// y = LCARS_HEADER_HEIGHT and don't know status_bar exists.
#pragma once

#include <ctime>
#include <string>

#include "i18n.h"  // Language

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Uppercase screen-name label in `lang` for a screen_nav index (kScreenFlights=0 /
// kScreenFlight=1, shown as NOW / TERAZ / kScreenRadar=2). Any out-of-range index yields "?".
// Also the bottom-nav button labels.
const char *statusBarScreenName(int screenIndex, Language lang);

// "DD/MM/YYYY" from time_sync::formatDate(), or a same-width
// "--/--/----" placeholder when the clock hasn't synced yet
// (time_sync::isTimeSynced() == false) — never the epoch-zero garbage the
// ESP32 boots with.
std::string statusBarDate(std::time_t localEpoch, bool timeSynced);

// "HH:MM" from time_sync::formatTime(), or "--:--" until the first NTP
// sync completes.
std::string statusBarClock(std::time_t localEpoch, bool timeSynced);

// Health of the OpenSky data feed, surfaced as a short header tag so a
// silent failure (persistent 401, exhausted rate-limit credits, a dead
// network) isn't invisible — the aircraft list on screen may be stale
// (main.cpp keeps the last good data rather than blanking it). See
// CLAUDE.md review notes 1.2/1.3/1.4.
enum class OpenSkyHealth {
  Ok,            // last poll succeeded
  RateLimited,   // 429 — credits exhausted, backing off
  AuthError,     // 401 persisted after a token refresh — check credentials
  NetworkError,  // transport / other HTTP failure
};

// Short right-aligned header tag for `health` — "" when Ok, otherwise a
// short code in `lang` (EN "RATE"/"AUTH"/"NET", PL "LIMIT"/"AUTH"/"SIEC").
// Pure — native-tested.
const char *statusBarHealthTag(OpenSkyHealth health, Language lang);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too (like lcars_theme/featured_panel) since the signature needs the
// LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the full header bar into y:0..LCARS_HEADER_HEIGHT across
// `screenWidth` px. `screenIndex` picks the name-block label;
// `localEpoch` is a time_sync::timeSyncNowLocal() value and `timeSynced`
// is time_sync::isTimeSynced() — together they decide real text vs.
// placeholder. Repaints its own black background first, so it's safe to
// call every frame.
void drawStatusBar(LGFX &gfx, int screenIndex, std::time_t localEpoch, bool timeSynced,
                   OpenSkyHealth health, int16_t screenWidth);

// Repaints only the header's text area (date, health tag, clock) — right
// of the orange name block, which is left untouched. For the once-a-minute
// clock update and health-tag changes (CLAUDE.md "Redraw policy"), so they
// don't need a full-screen redraw.
void drawStatusBarText(LGFX &gfx, std::time_t localEpoch, bool timeSynced, OpenSkyHealth health,
                       int16_t screenWidth);

#endif  // ARDUINO
