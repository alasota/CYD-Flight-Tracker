// aircraft_state — one aircraft as the sky model sees it: identity,
// position, altitude, speed, track, vertical rate and whether it is on the
// ground (docs/screens.md "Screen 1 — Flights"). opensky_client translates
// OpenSky's state vectors into it; everything downstream (rows, phases,
// CPA, the screens) reads only this.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <string>

struct AircraftState {
  std::string icao24;
  std::string callsign;
  float lat = 0.0f;
  float lon = 0.0f;
  bool has_position = false;  // false if lat/lon were null in the response
  float baro_altitude = 0.0f;
  bool has_altitude = false;  // false if baro_altitude was null (often on the apron)
  float velocity = 0.0f;
  bool has_velocity = false;  // false if velocity was null
  float true_track = 0.0f;
  bool on_ground = false;        // reported *or inferred* — see inferOnGround()
  float vertical_rate = 0.0f;    // m/s, positive = climbing
};

// Speed below which an aircraft is taxiing / parked rather than flying
// (15 m/s = 54 km/h — airliners never fly that slowly; taxi speeds stay
// under ~15 m/s).
constexpr float kTaxiSpeedMps = 15.0f;

// Whether `ac` is effectively on the ground. The reported on_ground flag is
// trusted when set, but aircraft on the apron are also often reported with
// on_ground=false and no altitude / no or crawling speed — so: flagged, or
// moving slower than kTaxiSpeedMps, or with neither altitude nor speed.
// opensky_client's parseStatesResponse() folds this into
// AircraftState::on_ground, so every screen (featured slot, phase, Screen 2 pick)
// agrees. Pure — native-tested.
bool inferOnGround(const AircraftState &ac);
