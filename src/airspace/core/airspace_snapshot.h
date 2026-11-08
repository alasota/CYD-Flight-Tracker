// airspace_snapshot — one poll's processed data: the sorted rows, Screen 2's
// pick and its closest approach (AirspaceSnapshot). poll_pipeline builds it,
// core_link carries it to the UI, the screens only read it. Immutable once
// published.
//
// Pure — plain data, no Arduino dependency.
#pragma once

#include <cstdint>
#include <vector>

#include "airspace/core/cpa_predictor.h"   // CpaPrediction
#include "airspace/core/tracked_flight.h"  // TrackedFlight

struct AirspaceSnapshot {
  std::vector<TrackedFlight> rows;  // sorted, enriched, phases classified
  int nowRowIndex = -1;             // pickNowAircraft(), -1 = none
  CpaPrediction cpa;                // for nowRowIndex, as of polledAtMs
  uint32_t polledAtMs = 0;          // millis() of the /states/all answer
  uint32_t fetches = 0;             // new hexdb.io requests that went into it
};
