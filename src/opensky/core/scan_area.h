// scan_area — the size of the area polled around home and what one
// /states/all call over it costs in OpenSky credits (docs/data-sources.md
// "Rate limits / credits"). The settings page and the device alerts both
// read the cost from here.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

// Area (in square degrees) of the lat/lon bounding box implied by a
// ±radius_deg half-width scan radius (i.e. a 2*radius_deg x 2*radius_deg
// box), matching config_store's Config::radius_deg.
float bboxAreaSqDeg(float radius_deg);

// OpenSky /states/all credit cost per call for that bbox area, per
// docs/data-sources.md "Rate limits / credits": <=25 sq deg = 1, <=100 = 2,
// <=400 = 3, >400 = 4.
int openSkyCreditCost(float radius_deg);
