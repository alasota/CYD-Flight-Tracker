// sun_times — sunrise / sunset for a date and a place, computed on the board
// (no network): the "sunset to sunrise" night mode (spec/done/03-brightness-night-mode.md).
//
// Pure — no Arduino / clock dependency (the date comes in as year / month /
// day, the time zone is the caller's business) — tested under
// `pio test -e native`.
#pragma once

struct SunTimes {
  enum class Kind { Normal, PolarDay, PolarNight };
  Kind kind = Kind::Normal;
  // Minutes after midnight, 0..1439: UTC from computeSunTimes(), local time
  // after shiftSunTimes(). Only meaningful for Kind::Normal.
  int sunrise_min = 0;
  int sunset_min = 0;
};

// The "sunrise equation" (NOAA-style: mean anomaly, equation of centre,
// declination), sun at -0.833° (refraction + the disc's radius) — good to
// ~1-2 min below the polar circles. lon_deg east-positive. 1901..2099.
SunTimes computeSunTimes(int year, int month, int day, float lat_deg, float lon_deg);

// Moves both times by `offset_min` (e.g. +60 for CET), wrapping in 0..1439.
SunTimes shiftSunTimes(const SunTimes &utc, int offset_min);
