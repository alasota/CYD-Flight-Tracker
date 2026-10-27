// lookup_store — persists the hexdb.io lookup caches (aircraft, route,
// airport) in NVS, so after a reboot the device doesn't start from a screen
// full of "..." and doesn't re-ask hexdb.io for answers it already had
// (docs/data-sources.md "Aircraft identification": NVS-backed cache — an
// aircraft's airline/type practically never changes).
//
// Records are stored as short text: a version digit and the fields joined
// with the ASCII unit separator (0x1F). Confirmed misses are stored too
// ("hexdb.io doesn't know it" is an answer worth keeping).
//
// The encode/decode half is pure (native-tested). The Preferences half is a
// thin adapter: one NVS namespace per kind; when NVS is full the namespace
// is wiped and the write retried — a crude but bounded eviction (the 20 KB
// NVS partition also holds the settings, which live in their own namespace
// and are never touched here).
#pragma once

#include <string>

#include "aircraft_lookup.h"  // AircraftInfo
#include "route_lookup.h"     // RouteInfo, AirportInfo

std::string encodeAircraftInfo(const AircraftInfo &info);
bool decodeAircraftInfo(const std::string &text, AircraftInfo &out);

std::string encodeRouteInfo(const RouteInfo &info);
bool decodeRouteInfo(const std::string &text, RouteInfo &out);

std::string encodeAirportInfo(const AirportInfo &info);
bool decodeAirportInfo(const std::string &text, AirportInfo &out);

// ---- Hardware adapter: ESP32 Preferences (NVS). Not covered by Unity.
#ifdef ARDUINO

bool storeLoadAircraft(const std::string &icao24, AircraftInfo &out);
void storeSaveAircraft(const std::string &icao24, const AircraftInfo &info);
bool storeHasAircraft(const std::string &icao24);

bool storeLoadRoute(const std::string &callsign, RouteInfo &out);
void storeSaveRoute(const std::string &callsign, const RouteInfo &info);
bool storeHasRoute(const std::string &callsign);

bool storeLoadAirport(const std::string &code, AirportInfo &out);
void storeSaveAirport(const std::string &code, const AirportInfo &info);
bool storeHasAirport(const std::string &code);

#endif  // ARDUINO
