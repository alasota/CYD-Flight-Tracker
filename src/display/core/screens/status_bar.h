// status_bar — draws the header bar shared by all three screens, per
// docs/screens.md "Screen chrome" (three zones, nothing filled, statusBarZones()):
// the app name on the left, the device-alert "!" next to it when an alert
// is active, a health dot (statusBarHealthColor()) + the number of aircraft
// in range ("7 LOTÓW" / "7 FLIGHTS") in the middle, and the date
// (DD.MM.YYYY) + time (from time_sync) on the right. The screen name is on
// the bottom-nav tabs, not here. Occupies y:0..LCARS_HEADER_HEIGHT, full
// width, with a 1px rule along its bottom edge.
//
// Drawn by the UI (main.cpp) when the redraw mask has kRedrawHeader —
// screens draw their content from LCARS_CONTENT_TOP and don't know
// status_bar exists. The text and layout here are pure; the LovyanGFX
// drawing is display/adapters/lgfx/status_bar_draw.
#pragma once

#include <ctime>
#include <cstdint>
#include <string>

#include "airspace/core/feed_health.h"  // OpenSkyHealth
#include "admin/core/device_alerts.h"  // AlertSeverity
#include "display/core/i18n.h"           // Language

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Uppercase screen-name label in `lang` for a screen_nav index (kScreenFlights=0 /
// kScreenFlight=1, shown as NOW / TERAZ / kScreenRadar=2). Any out-of-range index yields "?".
// Also the bottom-nav button labels.
const char *statusBarScreenName(int screenIndex, Language lang);

// "DD.MM.YYYY" from time_sync::formatDate(), or a same-width
// "--.--.----" placeholder when the clock hasn't synced yet
// (time_sync::isTimeSynced() == false) — never the epoch-zero garbage the
// ESP32 boots with.
std::string statusBarDate(std::time_t localEpoch, bool timeSynced);

// "HH:MM" from time_sync::formatTime(), or "--:--" until the first NTP
// sync completes.
std::string statusBarClock(std::time_t localEpoch, bool timeSynced);

// Header block text: "<n> <flights>" in `lang` with the right plural form
// (PL "1 LOT" / "3 LOTY" / "7 LOTOW", EN "1 FLIGHT" / "7 FLIGHTS"); counts
// over 99 show as "99+". "--" until the first successful poll
// (`hasData == false`) — "0 LOTOW" would claim the sky is empty when we
// simply don't know yet. Never longer than 11 chars. Pure — native-tested.
std::string statusBarBlockLabel(int aircraftCount, bool hasData, Language lang);

// Colour of the status dot in front of the flight count — the OpenSky data
// feed's health: green when Ok, yellow while rate-limited (429), red for
// auth or network errors (the aircraft shown may be stale). Never the
// peach of the active nav button. Pure — native-tested.
uint16_t statusBarHealthColor(OpenSkyHealth health);

// What a screen with no aircraft to show says — why it's empty. Ok: the sky
// really is empty ("BRAK LOTÓW W ZASIĘGU" / short "BRAK LOTU"); otherwise the
// feed's problem: credits exhausted, login failed, OpenSky unreachable — an
// empty screen must not claim "no flights" when we simply can't ask.
// `shortForm` for narrow slots (radar summary panel, <= 15 chars).
// Pure — native-tested.
Str emptyStateText(OpenSkyHealth health, bool shortForm);

// The app name shown at the left of the header.
constexpr const char *kStatusBarAppName = "SKY TRACKER";

// Header layout "variant A" — three zones on a 15px strip, nothing filled:
//   [SKY TRACKER]        [● 7 LOTÓW]        [18/10/2026  08:46]
//   left, small, dim     centred, white     right, light peach (clock larger)
// x ranges (px) computed from the strings and the fonts' fixed advances, so
// a test can prove the zones never overlap for the longest texts.
// With `badge` the device-alert "!" (spec 08 §3.4) sits kAlertBadgeGap px
// right of the name and the count is centred between the badge and the date.
struct HeaderZones {
  int name_x = 0, name_end = 0;        // app name
  int alert_x = -1;                    // left edge of the "!" badge; -1 = no badge
  int count_start = 0, count_end = 0;  // status dot + count label
  int right_start = 0, right_end = 0;  // date + clock
  int date_x = 0;                      // left edge of the date
};
// The "!" badge: a filled circle in alertBadgeColor() with a black "!",
// y:1..13 — inside the 15 px header, above its 1 px rule.
constexpr int kAlertBadgeDiameter = 13;
constexpr int kAlertBadgeGap = 4;
constexpr int kAlertBadgeTop = 1;
// The health dot left of the count, its gap to the label, and the gap between
// date and clock — read by statusBarZones() and the drawing alike.
constexpr int kStatusBarDotDiameter = 6;
constexpr int kStatusBarDotGap = 4;
constexpr int kStatusBarDateClockGap = 8;
HeaderZones statusBarZones(int screenWidth, const std::string &appName,
                           const std::string &countLabel, const std::string &date,
                           const std::string &clock, bool badge = false);
