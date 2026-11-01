// geo — distance, bearing and compass point between two places on the
// Earth: how far away an aircraft is and which way to look
// (docs/screens.md "Screen 1 — Flights", the featured panel's distance chip
// and the radar). Shared kernel: plain maths over lat / lon, no aircraft,
// no screen.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

// Great-circle distance (km) and initial bearing (degrees, 0=north,
// clockwise) from (home_lat, home_lon) to (lat, lon), via the haversine
// formula on a spherical Earth — good enough for a "how far/which way"
// home tracker, not survey-grade. distance_km == 0 and bearing_deg == 0
// when the two points coincide (bearing is undefined at zero distance).
struct DistanceBearing {
  float distance_km = 0.0f;
  float bearing_deg = 0.0f;
};
DistanceBearing computeDistanceBearing(float home_lat, float home_lon, float lat, float lon);

// 8-point compass abbreviation for a bearing: "N", "NE", "E", "SE", "S",
// "SW", "W", "NW" (each covers 45 degrees centred on its direction; any
// angle, normalised to 0..360). International letters as on a compass rose
// — Polish abbreviations ("PN-WSCH") don't fit the distance chip. Pure.
const char *compassPoint(float bearingDeg);
