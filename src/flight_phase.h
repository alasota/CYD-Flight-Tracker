// flight_phase — classifies an aircraft's current flight phase from
// simple instantaneous signals (distance from the nearest airport,
// vertical rate, on-ground flag). Zero I/O, zero Arduino/LovyanGFX
// dependency — pure classification logic only, matching the spirit of
// this project's other pure-logic modules (see CLAUDE.md "Testing").
//
// NOTE: not wired into the live data pipeline yet — opensky_client's
// Aircraft struct doesn't carry a vertical_rate field (OpenSky's state
// vector has one, index 11, but parseStatesResponse() doesn't extract it
// today), and nothing yet supplies a "distance to nearest airport" value
// (route_lookup/airport_lookup give a country code, not coordinates).
// This module stands alone as a tested, ready-to-wire classifier.
#pragma once

enum class Phase {
  NONE,        // on the ground — taxiing, parked, etc.
  TAKEOFF,     // near an airport and climbing fast
  LANDING,     // near an airport and descending fast
  OVERFLIGHT,  // airborne, not near an airport (or near one but level)
};

// Classifies flight phase from:
//   distance_km          — aircraft's distance from the nearest airport
//   vertical_rate_mps     — vertical rate, positive = climbing
//   on_ground              — an on-ground flag (e.g. OpenSky's own); always
//                            wins, forcing Phase::NONE regardless of the
//                            other inputs — an aircraft confirmed on the
//                            ground isn't "taking off" or "landing" by
//                            this classifier's definition, it just is
//                            grounded
//   near_airport_km        — distance threshold (inclusive) at or below
//                            which the aircraft counts as "near an
//                            airport"
//   climb_threshold_mps    — vertical-rate magnitude threshold (inclusive)
//                            that counts as an active climb/descent
//                            rather than level flight
// Returns TAKEOFF/LANDING only when near an airport AND the vertical rate
// crosses the relevant threshold; OVERFLIGHT is the catch-all for
// everything else while airborne (far from any airport, or near one but
// flying level). Pure — zero I/O, zero Arduino/LovyanGFX dependency —
// tested under `pio test -e native`.
Phase classifyPhase(float distance_km, float vertical_rate_mps, bool on_ground,
                     float near_airport_km, float climb_threshold_mps);
