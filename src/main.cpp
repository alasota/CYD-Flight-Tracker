// CYD Sky Tracker — main orchestration.
//
// setup(): initializes LovyanGFX (configured via include/LGFX_CYD.hpp) and
// the XPT2046 touch controller (touch_input), loads the persisted config
// (config_store), connects WiFi (wifi_manager — falls back to its own
// captive portal if no saved network works), and starts the local config
// web page (config_portal) at cyd-sky.local in the background.
//
// loop(): non-blocking, millis()-based.
//   - Touch: a tap on the view-toggle button placeholder (lcars_theme)
//     logs to Serial (radar_view doesn't exist yet); a tap in the top/
//     bottom strip pages the table backward/forward (table_view).
//   - Once per config_store's poll_interval_s (via opensky_client's own
//     scheduler): fetch Aircraft[] from OpenSky (opensky_client); if the
//     user hasn't set a home location yet (config_store's
//     home_configured), show a setup prompt instead of polling further —
//     see CLAUDE.md review notes 1.5. Otherwise look up each icao24's
//     airline/type (aircraft_lookup — cached, so a given icao24 only ever
//     hits HTTP once, and capped at kMaxNewLookupsPerPoll *new* lookups
//     per cycle so one poll can't stall loop() on an unbounded chain of
//     blocking HTTP calls — see review notes 1.1), join both into
//     table_view's AircraftRow list (table_view::buildEnrichedRecords),
//     and redraw Screen 1 (table_view) with the current page.
//
// This file is deliberately thin: all actual logic lives in the modules
// above — see CLAUDE.md "Testing" for why only this kind of orchestration
// code is allowed to go untested by Unity.

#include <Arduino.h>

#include <map>
#include <string>
#include <vector>

#include "LGFX_CYD.hpp"
#include "aircraft_lookup.h"
#include "config_portal.h"
#include "config_store.h"
#include "lcars_theme.h"
#include "opensky_client.h"
#include "table_view.h"
#include "touch_input.h"
#include "wifi_manager.h"

static LGFX tft;

// Data + paging state for the currently-drawn table page. Persists across
// loop() iterations so a touch-driven page change can redraw immediately
// without waiting for the next OpenSky poll.
static std::vector<AircraftRow> lastRows;
static int currentPage = 0;
static bool tableNeedsRedraw = false;
static bool setupPromptShown = false;

// Height of the top/bottom page-tap strips (see CLAUDE.md "table paging/
// scrolling"). Deliberately not a full-blown arrow widget yet — a plain
// tap zone is enough for this step's layout. Derived from table_view's own
// row height (tableRowHeightPx()) rather than a second, independent magic
// number, so the two can't silently drift apart — see review notes 5.5.
static const int16_t kPageTapZoneHeight = tableRowHeightPx();

// How many *new* (uncached) aircraft_lookup HTTP calls one poll cycle is
// allowed to make. Each one blocks loop() for up to a few seconds; without
// a cap, a poll that turns up many never-seen-before aircraft at once
// could stall touch/config_portal responsiveness for a long, unbounded
// chain of requests — see CLAUDE.md review notes 1.1. Uncached aircraft
// past the budget just render as "--" this cycle and get their turn on a
// later poll (they typically stay in range for several poll intervals).
static constexpr int kMaxNewLookupsPerPoll = 5;

static void redrawTable() {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);
  drawTablePage(tft, lastRows, 0, 0, screenW, screenH, currentPage);
  drawViewToggleButton(tft, screenW, screenH);
}

// Shown instead of the table when config_store's home_configured is still
// false — see CLAUDE.md review notes 1.5. Drawn once (guarded by
// setupPromptShown) rather than every poll interval.
static void showSetupPrompt() {
  if (setupPromptShown) return;

  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);
  tft.setFont(LCARS_FONT_BODY);
  tft.setTextDatum(middle_center);
  tft.setTextColor(LCARS_AMBER, LCARS_BLACK);
  tft.drawString("Set your home location at", screenW / 2, screenH / 2 - 10);
  tft.drawString("http://cyd-sky.local", screenW / 2, screenH / 2 + 10);
  drawViewToggleButton(tft, screenW, screenH);

  setupPromptShown = true;
}

// Dispatches one touch tap: the view-toggle button placeholder, or a
// page-back/page-forward tap zone. Called once per press (edge-detected in
// loop(), not once per loop() iteration the finger stays down).
static void handleTap(int16_t x, int16_t y) {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  Rect toggleBounds = viewToggleButtonBounds(screenW, screenH);
  if (hitTest(x, y, toggleBounds)) {
    Serial.println("view toggle tapped (radar_view not implemented yet)");
    return;
  }

  int perPage = rowsPerPage(screenH);
  int pageCount = getPageCount(static_cast<int>(lastRows.size()), perPage);

  Rect prevZone;
  prevZone.x = 0;
  prevZone.y = 0;
  prevZone.w = screenW;
  prevZone.h = kPageTapZoneHeight;

  Rect nextZone;
  nextZone.x = 0;
  nextZone.y = static_cast<int16_t>(screenH - kPageTapZoneHeight);
  nextZone.w = screenW;
  nextZone.h = kPageTapZoneHeight;

  if (hitTest(x, y, prevZone) && currentPage > 0) {
    currentPage--;
    tableNeedsRedraw = true;
  } else if (hitTest(x, y, nextZone) && currentPage < pageCount - 1) {
    currentPage++;
    tableNeedsRedraw = true;
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(LCARS_BLACK);

  touchInputBegin();

  Config cfg = loadConfig();
  (void)cfg;  // not otherwise needed here — each module below reads what it needs itself

  wifiManagerBegin();  // tries the saved network; falls back to its own captive portal

  // Starts mDNS + the config WebServer right away, non-blocking. In the
  // common case (a saved network that's reachable) wifiManagerBegin()
  // above has already connected by the time we get here; in the portal
  // fallback case cyd-sky.local won't resolve until the device later
  // joins a real network, which is fine — the captive portal itself
  // covers initial setup in that case.
  configPortalBegin();
}

void loop() {
  wifiManagerLoop();
  configPortalLoop();

  // --- Touch: view-toggle button + table paging ---------------------------
  static bool wasTouchPressed = false;
  TouchPoint touch = touchInputRead();
  if (touch.pressed && !wasTouchPressed) {
    handleTap(touch.x, touch.y);
  }
  wasTouchPressed = touch.pressed;

  // --- OpenSky polling (only meaningful once connected) --------------------
  if (wifiManagerStatus() == WifiStatus::Connected) {
    std::vector<Aircraft> aircraft;
    if (openSkyClientPoll(millis(), &aircraft)) {
      Config homeCfg = loadConfig();

      if (!homeCfg.home_configured) {
        showSetupPrompt();
      } else {
        std::map<std::string, AircraftInfo> infoByIcao24;
        int newLookupsThisCycle = 0;
        for (const Aircraft &ac : aircraft) {
          bool cached = aircraftLookupIsCached(ac.icao24);
          if (cached || newLookupsThisCycle < kMaxNewLookupsPerPoll) {
            if (!cached) {
              newLookupsThisCycle++;
            }
            infoByIcao24[ac.icao24] = lookupAircraft(ac.icao24);
          }
          // else: leave this icao24 out of infoByIcao24 for now —
          // buildEnrichedRecords() renders that as "--", and it'll get
          // looked up once it's under budget on a later poll.
        }

        lastRows = buildEnrichedRecords(aircraft, infoByIcao24);
        annotateDistances(lastRows, homeCfg.home_lat, homeCfg.home_lon);
        sortRowsByDistance(lastRows);

        int perPage = rowsPerPage(static_cast<int16_t>(tft.height()));
        int pageCount = getPageCount(static_cast<int>(lastRows.size()), perPage);
        if (currentPage >= pageCount) {
          currentPage = pageCount > 0 ? pageCount - 1 : 0;
        }

        tableNeedsRedraw = true;
      }
    }
  }

  // --- Redraw, if the data or the current page changed ---------------------
  if (tableNeedsRedraw) {
    redrawTable();
    tableNeedsRedraw = false;
  }
}
