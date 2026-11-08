// scan_area — the size of the area polled around home and what one
// /states/all call over it costs in OpenSky credits (docs/data-sources.md
// "Rate limits / credits"). The settings page and the device alerts both
// read the cost from here.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

// Area (in square degrees) of the lat/lon bounding box implied by a
// ±radius_deg half-width scan radius (i.e. a 2*radius_deg x 2*radius_deg
// box), matching airspace's Home::radius_deg.
float bboxAreaSqDeg(float radius_deg);

struct ScanArea {
  float lamin = 0.0f;
  float lomin = 0.0f;
  float lamax = 0.0f;
  float lomax = 0.0f;
};

// Bounding box for a ±radius_deg half-width square around (lat, lon),
// clamped to valid lat/lon ranges. Same half-width convention as
// airspace's Home::radius_deg / bboxAreaSqDeg() — a
// radius_deg that crosses the 1-credit threshold (2.5°)
// produces a proportionally larger box here. Does not handle antimeridian
// wraparound (a home point near ±180° longitude just gets clipped, not
// wrapped) — an accepted limitation for a fixed home-location tracker.
ScanArea computeScanArea(float lat, float lon, float radius_deg);

// OpenSky /states/all credit cost per call for that bbox area, per
// docs/data-sources.md "Rate limits / credits": <=25 sq deg = 1, <=100 = 2,
// <=400 = 3, >400 = 4.
int openSkyCreditCost(float radius_deg);
