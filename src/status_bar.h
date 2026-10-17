// status_bar — draws the header bar shared by all three screens, per
// CLAUDE.md "Screen chrome": a rounded block on the left showing how many
// aircraft are in range ("7 LOTOW" / "7 FLIGHTS"), coloured by OpenSky data
// health (orange OK / yellow rate-limited / magenta error), then the date
// (DD/MM/YYYY) + the real wall-clock time (from time_sync) to its right in
// LCARS_CYAN. The screen name is no longer in the header — it's on the
// bottom-nav buttons. Occupies y:0..LCARS_HEADER_HEIGHT, full width. **No
// WiFi signal bars** (CLAUDE.md; rssiToBars() has no home in this layout).
//
// Called once per frame from main.cpp *before* it dispatches to whichever
// screen is active — screens draw their content starting at
// y = LCARS_HEADER_HEIGHT and don't know status_bar exists.
#pragma once

#include <ctime>
#include <cstdint>
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

// Header block text: "<n> <flights>" in `lang` with the right plural form
// (PL "1 LOT" / "3 LOTY" / "7 LOTOW", EN "1 FLIGHT" / "7 FLIGHTS"); counts
// over 99 show as "99+". "--" until the first successful poll
// (`hasData == false`) — "0 LOTOW" would claim the sky is empty when we
// simply don't know yet. Never longer than 11 chars. Pure — native-tested.
std::string statusBarBlockLabel(int aircraftCount, bool hasData, Language lang);

// Header block fill colour for the data feed's health — replaces the old
// "RATE/AUTH/NET" text tag: LCARS_ORANGE when Ok, LCARS_YELLOW while
// rate-limited (429), LCARS_MAGENTA for auth or network errors (the
// aircraft shown may be stale). Pure — native-tested.
uint16_t statusBarBlockColor(OpenSkyHealth health);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too (like lcars_theme/featured_panel) since the signature needs the
// LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the full header bar into y:0..LCARS_HEADER_HEIGHT across
// `screenWidth` px: the count block (statusBarBlockLabel() on
// statusBarBlockColor()), then date + clock. `localEpoch` is a
// time_sync::timeSyncNowLocal() value and `timeSynced` is
// time_sync::isTimeSynced() — together they decide real text vs.
// placeholder. Repaints its own black background first and touches nothing
// below the header, so it's safe to call on its own (CLAUDE.md "Redraw
// policy": minute rollover, health change, new poll).
void drawStatusBar(LGFX &gfx, int aircraftCount, bool hasData, OpenSkyHealth health,
                   std::time_t localEpoch, bool timeSynced, int16_t screenWidth);

#endif  // ARDUINO
