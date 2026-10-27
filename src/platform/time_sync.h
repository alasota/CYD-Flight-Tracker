// time_sync — NTP wall-clock sync (configTzTime for Europe/Warsaw) plus
// pure, testable date/time formatting helpers. See docs/screens.md
// "Time" and CLAUDE.md "Code conventions".
//
// Split per CLAUDE.md "Testing": the formatting functions below take a
// time_t as a parameter (never read the wall clock internally) and have
// zero Arduino/LovyanGFX/network dependency, so they run under
// `pio test -e native`. Only timeSyncBegin()/isTimeSynced()/
// timeSyncNowLocal() touch the ESP32 SDK and are guarded for the device
// build.
#pragma once

#include <ctime>
#include <string>

// POSIX TZ rule for Europe/Warsaw — CET (UTC+1) with CEST (UTC+2) summer
// time, DST starting last Sunday of March and ending last Sunday of
// October at 03:00 local. Stable long-standing EU rule; see docs/screens.md "Time".
constexpr const char *TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---- Pure logic (no Arduino/network dependency) — tested under
// `pio test -e native`.
//
// All of these interpret the supplied `epoch` as "the wall-clock instant to
// render", broken down as UTC fields — i.e. the caller passes an epoch
// that has already been shifted to the target timezone (on the device,
// via timeSyncNowLocal() below). Keeping the breakdown UTC-based is what
// makes these deterministic regardless of the host's TZ when the test
// runs. Negative epochs (pre-1970) are not supported and clamp to the
// epoch.

// "DD.MM.YYYY", zero-padded (e.g. "07.03.2026") — the usual Polish date format.
std::string formatDate(std::time_t epoch);

// "HH:MM", 24-hour, zero-padded (e.g. "01:46").
std::string formatTime(std::time_t epoch);

// Whole minutes since the epoch — changes exactly when formatTime()'s
// "HH:MM" would. main.cpp compares successive values to know when the
// header clock needs repainting (CLAUDE.md "Redraw policy").
long minuteOf(std::time_t epoch);

// ---- Hardware adapter: ESP32 SDK / lwIP SNTP. Not covered by Unity
// (see CLAUDE.md "Testing").
#ifdef ARDUINO

// Kicks off SNTP with the Europe/Warsaw TZ rule. Call once, right after
// WiFi connects. Non-blocking — the first sync lands a few seconds later;
// until then isTimeSynced() stays false.
void timeSyncBegin();

// True once at least one SNTP sync has completed (heuristic: the system
// clock has advanced past a sanity threshold well after 1970). Screens
// should show a placeholder (e.g. "--:--") while this is false rather
// than the garbage epoch-zero date the ESP32 powers up with.
bool isTimeSynced();

// Current instant as an epoch already shifted into Europe/Warsaw local
// time, ready to hand straight to formatDate()/formatTime()/minuteOf().
// Returns 0 if time isn't synced yet.
std::time_t timeSyncNowLocal();

#endif  // ARDUINO
