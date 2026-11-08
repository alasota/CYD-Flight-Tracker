// flight_info — what hexdb.io says about a flight: the aircraft (airline,
// type, registration), its route and the airports at either end. Shared kernel
// types: enrichment fills them, airspace carries them in TrackedFlight, the
// screens read them. No logic here; the lookups are aircraft_lookup and
// route_lookup. See docs/data-sources.md "Aircraft identification".
#pragma once

#include <string>

struct AircraftInfo {
  bool found = false;
  std::string airline;        // hexdb.io "RegisteredOwners"
  std::string aircraft_type;  // hexdb.io "Type", e.g. "A319 111" / "737MAX 8"
  std::string icao_type;      // hexdb.io "ICAOTypeCode", e.g. "A319" / "B38M"
  std::string registration;   // hexdb.io "Registration"

  // Defined (= default) in flight_info.cpp, not inline: four strings to
  // construct, and inlining that into every function that makes one costs flash.
  AircraftInfo();
};

// Origin/destination airport for a flight, by callsign. hexdb.io's
// GET /api/v1/route/icao/{callsign} returns a single "route" string like
// "EIDW-EGLL" — both ends are ICAO (4-letter) airport codes, matching the
// callsign format used to look it up.
struct RouteInfo {
  bool found = false;
  std::string origin_icao;
  std::string dest_icao;
};

// Country (+ IATA code, for display) for an airport, by **ICAO** code —
// this project uses hexdb.io's GET /api/v1/airport/icao/{code} endpoint,
// not /iata/, so it chains directly off RouteInfo::origin_icao/dest_icao
// above without needing an ICAO->IATA conversion this project doesn't
// have. hexdb.io's airport response carries both codes, so the IATA one
// is still available for display (e.g. featured_panel's "WAW (PL)").
struct AirportInfo {
  bool found = false;
  std::string iata_code;     // hexdb.io "iata", e.g. "LHR" — for display
  std::string country_code;  // hexdb.io "country_code", e.g. "GB"
  std::string name;          // hexdb.io "airport", ASCII-folded, e.g. "Heathrow Airport"
};
