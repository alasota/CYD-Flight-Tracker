// redraw_policy — decides *which regions* of the display need repainting
// for a given set of events, per CLAUDE.md "Redraw policy". The first
// implementation did a full fillScreen() + redraw on every countdown tick,
// so Screen 2 blinked once a second; this keeps full clears for structural
// changes only (screen/page switch, first draw).
//
// Pure — no TFT/Arduino dependency — tested under `pio test -e native`.
// main.cpp gathers the events each loop and acts on the returned mask.
#pragma once

#include <cstdint>

// Bit flags. kRedrawFull implies everything (fillScreen + header + content
// + bottom nav) and is never combined with the others.
constexpr uint8_t kRedrawFull = 1 << 0;        // fillScreen + all chrome + content
constexpr uint8_t kRedrawContent = 1 << 1;     // content area (LCARS_CONTENT_TOP..BOTTOM) only
constexpr uint8_t kRedrawHeader = 1 << 2;      // header bar only (count, health dot, "!", clock)
constexpr uint8_t kRedrawCountdown = 1 << 3;   // Screen 2 countdown zone + status strip only

// What happened since the last redraw.
struct RedrawEvents {
  bool firstDraw = false;      // boot / leaving the setup prompt
  bool screenChanged = false;  // manual tap or auto-cycle
  bool pageChanged = false;    // Screen 1 table page flip
  bool languageChanged = false;  // display language switched on the config page
  bool newPoll = false;        // fresh aircraft data
  bool minuteChanged = false;  // header "HH:MM" would read differently (incl. first NTP sync)
  bool healthChanged = false;  // OpenSky health tag changed
  bool alertsChanged = false;  // the header's "!" badge changed (device_alerts)
  bool countdownTick = false;  // 1 Hz Screen 2 tick
};

// Regions to repaint for `e` while `currentScreen` (screen_nav index) is
// active:
//   - firstDraw / screenChanged / pageChanged / languageChanged
//                                             -> kRedrawFull, nothing else.
//   - newPoll                                 -> kRedrawContent + kRedrawHeader
//                                                (the header block shows the aircraft count).
//   - minuteChanged / healthChanged / alertsChanged
//                                             -> kRedrawHeader (health dot, "!" badge).
//   - countdownTick on kScreenFlight          -> kRedrawCountdown, unless
//     kRedrawContent is already set (it covers the countdown zone).
//     Ignored on other screens.
uint8_t computeRedrawMask(const RedrawEvents &e, int currentScreen);
