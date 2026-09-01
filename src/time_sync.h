// time_sync — NTP wall-clock sync (configTzTime for Europe/Warsaw) plus
// pure, testable date/time/stardate formatting helpers. See CLAUDE.md
// "Time & WiFi status" and "Code conventions".
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
// October at 03:00 local. Stable long-standing EU rule; see CLAUDE.md.
constexpr const char *TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---- Pure logic (no Arduino/network dependency) — tested under
// `pio test -e native`.
//
// All three interpret the supplied `epoch` as "the wall-clock instant to
// render", broken down as UTC fields — i.e. the caller passes an epoch
// that has already been shifted to the target timezone (on the device,
// via timeSyncNowLocal() below). Keeping the breakdown UTC-based is what
// makes these deterministic regardless of the host's TZ when the test
// runs. Negative epochs (pre-1970) are not supported and clamp to the
// epoch.

// "DD.MM.YYYY", zero-padded (e.g. "09.09.2001").
std::string formatDate(std::time_t epoch);

// "HH:MM", 24-hour, zero-padded (e.g. "01:46").
std::string formatTime(std::time_t epoch);

// Decorative Star Trek-style stardate as "XXXX.XX" (see CLAUDE.md — this
// is fan flavor text, not canonical). Deterministic function of the date:
//   stardate = (year - 2323) * 1000 + day_of_year * 2.7378
// where day_of_year is 1 for January 1st. Always two decimal places; may
// be negative for present-day dates (the formula's zero point is the
// 24th century) — that's expected, it's flavor, not a real calendar.
std::string computeStardate(std::time_t epoch);

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
// time, ready to hand straight to formatDate()/formatTime()/
// computeStardate(). Returns 0 if time isn't synced yet.
std::time_t timeSyncNowLocal();

#endif  // ARDUINO
