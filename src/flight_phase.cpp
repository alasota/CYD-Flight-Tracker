#include "flight_phase.h"

Phase classifyPhase(float distance_km, float vertical_rate_mps, bool on_ground,
                     float near_airport_km, float climb_threshold_mps) {
  if (on_ground) {
    return Phase::NONE;
  }

  // Beyond near_airport_km there is no phase to show (CLAUDE.md "Flight
  // phase"). The first version fell through to OVERFLIGHT here, so every
  // distant aircraft got the same icon and the FAZA column meant nothing.
  if (distance_km > near_airport_km) {
    return Phase::NONE;
  }
  if (vertical_rate_mps >= climb_threshold_mps) {
    return Phase::TAKEOFF;
  }
  if (vertical_rate_mps <= -climb_threshold_mps) {
    return Phase::LANDING;
  }
  return Phase::OVERFLIGHT;  // near, but level — transiting
}
