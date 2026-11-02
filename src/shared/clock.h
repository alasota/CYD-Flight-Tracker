// clock — date and time as the screens and the settings page show them
// ("07.03.2026", "08:46") and the minute the header clock repaints on. Shared
// kernel: plain arithmetic on an epoch, no wall clock, no timezone database
// (platform/time_sync gets the local time from NTP). See docs/screens.md
// "Time".
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <ctime>
#include <string>

// All of these interpret the supplied `epoch` as "the wall-clock instant to
// render", broken down as UTC fields — i.e. the caller passes an epoch
// that has already been shifted to the target timezone (on the device,
// via platform/time_sync's timeSyncNowLocal()). Keeping the breakdown UTC-based is what
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
