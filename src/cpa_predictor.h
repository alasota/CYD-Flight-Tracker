// cpa_predictor — pure closest-point-of-approach time math for Screen 2
// ("Flight"), answering "how many seconds until the nearest aircraft is
// directly overhead". See CLAUDE.md "Flight ETA — closest point of
// approach".
//
// Straight-line (constant-velocity) extrapolation only — real aircraft
// turn, so accuracy degrades the further out you extrapolate. Acceptable
// here because the product only cares about the near-term result (under a
// couple of minutes). Zero I/O, zero Arduino/LovyanGFX dependency —
// tested under `pio test -e native`.
#pragma once

struct CpaPrediction {
  // False when no meaningful prediction exists — the aircraft is
  // essentially stationary (on ground / hovering), so there's no closing
  // velocity to extrapolate. Screen 2 shows an em-dash in that case.
  bool found = false;
  // Signed seconds to closest point of approach: positive = CPA is in the
  // future, negative = CPA already happened that many seconds ago. Only
  // meaningful when `found` is true.
  float t_cpa_seconds = 0.0f;
};

// Computes time-to-CPA for an aircraft moving in a straight line at
// constant speed/track, relative to the fixed home location.
//   home_lat/home_lon  — home position, decimal degrees
//   ac_lat/ac_lon      — aircraft position, decimal degrees
//   speed_mps          — ground speed, metres per second (OpenSky's
//                        `velocity`)
//   track_deg          — course over ground, degrees, 0 = north,
//                        increasing clockwise (OpenSky's `true_track`)
//
// Uses an equirectangular projection around home latitude (fine at this
// scale) then the standard CPA formula
//   t = -(d . v) / (v . v)
// where d is the home->aircraft offset vector (km) and v the aircraft
// velocity vector (km/s). Returns found=false if |v| is ~0.
CpaPrediction predictCpa(double home_lat, double home_lon, double ac_lat, double ac_lon,
                         double speed_mps, double track_deg);

// Advances a previously-computed prediction forward by `elapsed_ms` of
// wall-clock — the "local ticking between polls" from CLAUDE.md "Flight
// ETA". OpenSky data only refreshes every poll_interval, but Screen 2's
// countdown should tick every second, so main.cpp re-derives the current
// t_cpa from the last polled value plus the millis() elapsed since that
// poll, instead of re-running predictCpa() (which it can't — it has no
// fresher aircraft position between polls). A not-found prediction stays
// not-found. Pure — no clock read here, `elapsed_ms` is injected — so it's
// covered by native tests.
CpaPrediction extrapolateCpa(const CpaPrediction &polled, unsigned long elapsed_ms);
