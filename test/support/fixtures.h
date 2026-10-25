// Test data builders shared by the suites (spec/done/07-test-reorganization.md §4):
// aircraft, table rows, a configured Config and stub hexdb.io lookup ports.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "config_store.h"
#include "opensky_client.h"
#include "poll_pipeline.h"
#include "table_view.h"

// ---- Aircraft / AircraftRow --------------------------------------------------

struct AircraftOpts {
  bool ground = false;
  float alt_m = 3000.0f;     // ignored on the ground (has_altitude = false)
  float speed_mps = 200.0f;
  float track_deg = 180.0f;  // southbound
};

// Taxiing: no altitude, 5 m/s.
inline AircraftOpts groundOpts() { return AircraftOpts{true, 0.0f, 5.0f, 180.0f}; }

// A state vector with a position, altitude (airborne only), speed and track.
inline Aircraft makeAircraft(const char *hex, const char *callsign, float lat, float lon, AircraftOpts o = {}) {
  Aircraft a;
  a.icao24 = hex;
  a.callsign = callsign;
  a.lat = lat;
  a.lon = lon;
  a.has_position = true;
  a.has_altitude = !o.ground;
  a.baro_altitude = o.ground ? 0.0f : o.alt_m;
  a.has_velocity = true;
  a.velocity = o.speed_mps;
  a.true_track = o.track_deg;
  a.on_ground = o.ground;
  return a;
}

// A table row `km` from home (no distance without a position). Lookups,
// route and phase are left empty — set what the test needs.
inline AircraftRow makeRow(const char *callsign, float km, bool onGround = false, bool hasPos = true) {
  AircraftRow r;
  r.aircraft.callsign = callsign;
  r.aircraft.on_ground = onGround;
  r.aircraft.has_position = hasPos;
  r.has_distance = hasPos;
  r.distance_km = km;
  return r;
}

// ---- Config --------------------------------------------------------------------

// defaultConfig() with the home set to central Warsaw.
inline Config makeConfig() {
  Config c = defaultConfig();
  c.home_lat = 52.2297f;
  c.home_lon = 21.0122f;
  c.home_configured = true;
  return c;
}

// ---- stub hexdb.io (poll_pipeline's LookupPorts) -------------------------------

// `known*` keys count as cached; every fetch is recorded in `calls`
// ("ac:<icao24>", "rt:<callsign>", "ap:<icao>"). Reset before every test.
struct StubHexdb {
  std::set<std::string> knownAircraft, knownRoutes, knownAirports;
  std::vector<std::string> calls;
};

inline StubHexdb &stubHexdb() {
  static StubHexdb s;
  return s;
}

inline void resetStubPorts() { stubHexdb() = StubHexdb{}; }

inline LookupPorts stubPorts() {
  LookupPorts p;
  p.aircraftKnown = [](const std::string &k) { return stubHexdb().knownAircraft.count(k) > 0; };
  p.routeKnown = [](const std::string &k) { return stubHexdb().knownRoutes.count(k) > 0; };
  p.airportKnown = [](const std::string &k) { return stubHexdb().knownAirports.count(k) > 0; };
  p.aircraft = [](const std::string &k) {
    stubHexdb().calls.push_back("ac:" + k);
    AircraftInfo i;
    i.found = true;
    i.airline = "Airline " + k;
    return i;
  };
  p.route = [](const std::string &k) {
    stubHexdb().calls.push_back("rt:" + k);
    RouteInfo r;
    r.found = true;
    r.origin_icao = "EPWA";
    r.dest_icao = "LIRF";
    return r;
  };
  p.airport = [](const std::string &k) {
    stubHexdb().calls.push_back("ap:" + k);
    AirportInfo a;
    a.found = true;
    a.iata_code = k == "EPWA" ? "WAW" : "FCO";
    return a;
  };
  return p;
}
