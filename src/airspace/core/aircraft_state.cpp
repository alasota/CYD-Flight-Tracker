#include "airspace/core/aircraft_state.h"

bool inferOnGround(const Aircraft &ac) {
  if (ac.on_ground) return true;
  if (ac.has_velocity && ac.velocity < kTaxiSpeedMps) return true;  // taxiing / parked
  if (!ac.has_altitude && !ac.has_velocity) return true;            // apron, no data
  return false;
}
