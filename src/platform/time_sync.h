// time_sync — NTP wall-clock sync (configTzTime for Europe/Warsaw): when the
// clock is set and what time it is locally. The date / time text is
// shared/clock (formatDate(), formatTime(), minuteOf()), fed with
// timeSyncNowLocal(). See docs/screens.md "Time" and CLAUDE.md "Code
// conventions".
//
// timeSyncBegin()/isTimeSynced()/timeSyncNowLocal() touch the ESP32 SDK and
// are guarded for the device build (platform/ may, spec 12 section 5).
#pragma once

#include <ctime>

// POSIX TZ rule for Europe/Warsaw — CET (UTC+1) with CEST (UTC+2) summer
// time, DST starting last Sunday of March and ending last Sunday of
// October at 03:00 local. Stable long-standing EU rule; see docs/screens.md "Time".
constexpr const char *TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3";

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
