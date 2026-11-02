// lookup_store_nvs — the Preferences (NVS) half of lookup_store: one namespace
// per kind of record, the codec from lookup_store, a wipe-and-retry when NVS is
// full. Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#include "enrichment/core/lookup_store.h"

#include <Preferences.h>

namespace {

constexpr char kNsAircraft[] = "lk_ac";
constexpr char kNsRoute[] = "lk_rt";
constexpr char kNsAirport[] = "lk_ap";

// NVS keys are limited to 15 characters; all our keys (icao24 = 6,
// callsign <= 8, airport ICAO = 4) fit, anything longer is skipped.
bool validKey(const std::string &key) { return !key.empty() && key.size() <= 15; }

bool load(const char *ns, const std::string &key, std::string &out) {
  if (!validKey(key)) return false;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/true)) return false;  // namespace not created yet
  const bool present = prefs.isKey(key.c_str());
  if (present) out = prefs.getString(key.c_str(), "").c_str();
  prefs.end();
  return present;
}

bool has(const char *ns, const std::string &key) {
  std::string ignored;
  return load(ns, key, ignored);
}

void save(const char *ns, const std::string &key, const std::string &value) {
  if (!validKey(key)) return;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/false)) return;
  if (prefs.putString(key.c_str(), value.c_str()) == 0) {
    // NVS full: drop this kind's whole cache and start over.
    prefs.clear();
    prefs.putString(key.c_str(), value.c_str());
  }
  prefs.end();
}

}  // namespace

bool storeLoadAircraft(const std::string &icao24, AircraftInfo &out) {
  std::string s;
  return load(kNsAircraft, icao24, s) && decodeAircraftInfo(s, out);
}
void storeSaveAircraft(const std::string &icao24, const AircraftInfo &info) {
  save(kNsAircraft, icao24, encodeAircraftInfo(info));
}
bool storeHasAircraft(const std::string &icao24) { return has(kNsAircraft, icao24); }

bool storeLoadRoute(const std::string &callsign, RouteInfo &out) {
  std::string s;
  return load(kNsRoute, callsign, s) && decodeRouteInfo(s, out);
}
void storeSaveRoute(const std::string &callsign, const RouteInfo &info) {
  save(kNsRoute, callsign, encodeRouteInfo(info));
}
bool storeHasRoute(const std::string &callsign) { return has(kNsRoute, callsign); }

bool storeLoadAirport(const std::string &code, AirportInfo &out) {
  std::string s;
  return load(kNsAirport, code, s) && decodeAirportInfo(s, out);
}
void storeSaveAirport(const std::string &code, const AirportInfo &info) {
  save(kNsAirport, code, encodeAirportInfo(info));
}
bool storeHasAirport(const std::string &code) { return has(kNsAirport, code); }
