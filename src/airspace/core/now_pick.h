// now_pick — which aircraft Screen 2 (NOW / TERAZ) shows and its
// countdown: the one that will be overhead soonest, not simply the nearest
// (docs/screens.md "Screen 2 — Flight (NOW / TERAZ)", "Flight ETA — closest
// point of approach").
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <vector>

#include "airspace/core/cpa_predictor.h"   // CpaPrediction
#include "airspace/core/tracked_flight.h"  // TrackedFlight

// "Will pass overhead" = the closest point of approach is within this many
// km of home.
constexpr float kOverheadRadiusKm = 5.0f;

// Which aircraft Screen 2 shows, and its countdown.
struct NowPick {
  int index = -1;     // into the rows; -1 = no aircraft at all
  CpaPrediction cpa;  // found == false -> nobody will pass overhead
};

// Screen 2's job is "when will it be overhead", so it picks — among
// airborne aircraft whose CPA is at most `overheadKm` from home and not
// more than 10 s in the past — the one with the *soonest* CPA (not simply
// the nearest one). Without such a candidate it shows the nearest airborne
// aircraft (rows are nearest-first) with no prediction, or row 0 if all are
// on the ground. `cpas[i]` belongs to `rows[i]`. Pure — native-tested.
NowPick pickNowAircraft(const std::vector<TrackedFlight> &rows,
                        const std::vector<CpaPrediction> &cpas, float overheadKm);
