#include "flight_phase.h"

Phase classifyPhase(float distance_km, float vertical_rate_mps, bool on_ground,
                     float near_airport_km, float climb_threshold_mps) {
  if (on_ground) {
    return Phase::NONE;
  }

  bool nearAirport = distance_km <= near_airport_km;

  if (nearAirport && vertical_rate_mps >= climb_threshold_mps) {
    return Phase::TAKEOFF;
  }
  if (nearAirport && vertical_rate_mps <= -climb_threshold_mps) {
    return Phase::LANDING;
  }
  return Phase::OVERFLIGHT;
}
