#include "airspace/core/now_pick.h"

NowPick pickNowAircraft(const std::vector<TrackedFlight> &rows,
                        const std::vector<CpaPrediction> &cpas, float overheadKm) {
  NowPick pick;
  if (rows.empty()) return pick;

  // Soonest CPA among airborne aircraft that will actually pass overhead.
  for (size_t i = 0; i < rows.size() && i < cpas.size(); ++i) {
    const CpaPrediction &c = cpas[i];
    if (rows[i].aircraft.on_ground || !c.found) continue;
    if (c.t_cpa_seconds < -10.0f || c.miss_distance_km > overheadKm) continue;
    if (pick.index < 0 || c.t_cpa_seconds < pick.cpa.t_cpa_seconds) {
      pick.index = static_cast<int>(i);
      pick.cpa = c;
    }
  }
  if (pick.index >= 0) return pick;

  // Nobody passes overhead: show the nearest airborne aircraft, no countdown.
  pick.index = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (!rows[i].aircraft.on_ground) {
      pick.index = static_cast<int>(i);
      break;
    }
  }
  return pick;
}
