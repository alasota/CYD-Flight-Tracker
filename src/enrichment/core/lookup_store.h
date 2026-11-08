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
// The encode/decode half and storeSaveRecord() are pure (native-tested). The
// Preferences half is a thin adapter (enrichment/adapters/lookup_store_nvs):
// one NVS namespace per kind. Eviction is crude but bounded: a record that
// would eat into kLookupStoreReserveEntries wipes whole lookup namespaces first
// (the 20 KB NVS partition also holds the settings and the WiFi credentials,
// which are never touched here).
#pragma once

#include <cstddef>
#include <string>

#include "enrichment/core/aircraft_lookup.h"  // AircraftInfo
#include "enrichment/core/route_lookup.h"     // RouteInfo, AirportInfo

std::string encodeAircraftInfo(const AircraftInfo &info);
bool decodeAircraftInfo(const std::string &text, AircraftInfo &out);

std::string encodeRouteInfo(const RouteInfo &info);
bool decodeRouteInfo(const std::string &text, RouteInfo &out);

std::string encodeAirportInfo(const AirportInfo &info);
bool decodeAirportInfo(const std::string &text, AirportInfo &out);

// Slots the lookup records must leave free in the NVS partition they share
// with the settings and the WiFi credentials.
constexpr size_t kLookupStoreReserveEntries = 64;

// One NVS namespace per kind of record.
constexpr char kLookupNsAircraft[] = "lk_ac";
constexpr char kLookupNsRoute[] = "lk_rt";
constexpr char kLookupNsAirport[] = "lk_ap";

// What storeSaveRecord() needs from NVS. freeEntries() counts only the slots a
// write can use (not the spare page NVS keeps for its own garbage collection).
class LookupNvs {
 public:
  virtual ~LookupNvs() = default;
  virtual bool put(const char *ns, const std::string &key, const std::string &value) = 0;
  virtual void clear(const char *ns) = 0;
  virtual size_t freeEntries() = 0;
};

// Writes one record into namespace `ns`; true when it is stored. While the
// record would leave fewer than kLookupStoreReserveEntries free, lookup
// namespaces are wiped (`ns` first, then the other two); a write that still
// fails wipes the next one and is retried.
bool storeSaveRecord(LookupNvs &nvs, const char *ns, const std::string &key,
                     const std::string &value);

// ---- Hardware adapter: ESP32 Preferences (NVS), defined in
// enrichment/adapters/lookup_store_nvs.cpp. Not covered by Unity.

bool storeLoadAircraft(const std::string &icao24, AircraftInfo &out);
void storeSaveAircraft(const std::string &icao24, const AircraftInfo &info);
bool storeHasAircraft(const std::string &icao24);

bool storeLoadRoute(const std::string &callsign, RouteInfo &out);
void storeSaveRoute(const std::string &callsign, const RouteInfo &info);
bool storeHasRoute(const std::string &callsign);

bool storeLoadAirport(const std::string &code, AirportInfo &out);
void storeSaveAirport(const std::string &code, const AirportInfo &info);
bool storeHasAirport(const std::string &code);
