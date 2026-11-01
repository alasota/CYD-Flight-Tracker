#include "airspace/core/tracked_flight.h"

#include "shared/geo.h"  // computeDistanceBearing()

AircraftRow::AircraftRow(const AircraftRow &other) = default;
AircraftRow::AircraftRow(AircraftRow &&other) noexcept = default;
AircraftRow &AircraftRow::operator=(const AircraftRow &other) = default;
AircraftRow &AircraftRow::operator=(AircraftRow &&other) noexcept = default;
AircraftRow::~AircraftRow() = default;

std::vector<AircraftRow> buildEnrichedRecords(
    const std::vector<Aircraft> &aircraft, const std::map<std::string, AircraftInfo> &infoByIcao24,
    const std::map<std::string, RouteInfo> &routeByCallsign) {
  std::vector<AircraftRow> rows;
  rows.reserve(aircraft.size());

  for (const Aircraft &ac : aircraft) {
    AircraftRow row;
    row.aircraft = ac;

    auto infoIt = infoByIcao24.find(ac.icao24);
    if (infoIt != infoByIcao24.end()) {
      row.info = infoIt->second;
    }
    // else: row.info stays default-constructed (found=false, empty
    // airline/aircraft_type) — rendered as "--".

    auto routeIt = routeByCallsign.find(ac.callsign);
    if (routeIt != routeByCallsign.end()) {
      row.route = routeIt->second;
    }
    // else: row.route stays default-constructed (found=false, empty
    // origin/dest) — rendered as "--".

    rows.push_back(row);
  }

  return rows;
}

void annotateDistances(std::vector<AircraftRow> &rows, float home_lat, float home_lon) {
  for (AircraftRow &row : rows) {
    if (!row.aircraft.has_position) {
      row.has_distance = false;
      row.distance_km = 0.0f;
      row.bearing_deg = 0.0f;
      continue;
    }
    DistanceBearing db =
        computeDistanceBearing(home_lat, home_lon, row.aircraft.lat, row.aircraft.lon);
    row.distance_km = db.distance_km;
    row.bearing_deg = db.bearing_deg;
    row.has_distance = true;
  }
}

void classifyPhases(std::vector<AircraftRow> &rows, float near_airport_km,
                     float climb_threshold_mps) {
  for (AircraftRow &row : rows) {
    if (!row.has_distance) {
      row.phase = Phase::NONE;  // no position -> nothing meaningful to classify
      continue;
    }
    row.phase = classifyPhase(row.distance_km, row.aircraft.vertical_rate, row.aircraft.on_ground,
                               near_airport_km, climb_threshold_mps);
  }
}
