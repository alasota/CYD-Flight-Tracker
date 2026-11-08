// lookup_store_nvs — the Preferences (NVS) half of lookup_store: one namespace
// per kind of record, the codec and the eviction (storeSaveRecord()) from
// lookup_store. Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#include "enrichment/core/lookup_store.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

// Preferences::freeEntries() counts the page NVS keeps empty for its own
// garbage collection; a write can't use those slots.
constexpr size_t kSparePageEntries = 126;

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

class PreferencesLookupNvs : public LookupNvs {
 public:
  bool put(const char *ns, const std::string &key, const std::string &value) override {
    Preferences prefs;
    if (!prefs.begin(ns, /*readOnly=*/false)) return false;
    const size_t written = prefs.putString(key.c_str(), value.c_str());  // records are never empty
    prefs.end();
    return written > 0;
  }

  void clear(const char *ns) override {
    Preferences prefs;
    if (!prefs.begin(ns, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
  }

  size_t freeEntries() override {
    Preferences prefs;
    const size_t reported = prefs.freeEntries();
    return reported > kSparePageEntries ? reported - kSparePageEntries : 0;
  }
};

void save(const char *ns, const std::string &key, const std::string &value) {
  if (!validKey(key)) return;
  PreferencesLookupNvs nvs;
  if (!storeSaveRecord(nvs, ns, key, value)) {
    Serial.printf("[lookup] could not store %s in %s: NVS full\n", key.c_str(), ns);
  }
}

}  // namespace

bool storeLoadAircraft(const std::string &icao24, AircraftInfo &out) {
  std::string s;
  return load(kLookupNsAircraft, icao24, s) && decodeAircraftInfo(s, out);
}
void storeSaveAircraft(const std::string &icao24, const AircraftInfo &info) {
  save(kLookupNsAircraft, icao24, encodeAircraftInfo(info));
}
bool storeHasAircraft(const std::string &icao24) { return has(kLookupNsAircraft, icao24); }

bool storeLoadRoute(const std::string &callsign, RouteInfo &out) {
  std::string s;
  return load(kLookupNsRoute, callsign, s) && decodeRouteInfo(s, out);
}
void storeSaveRoute(const std::string &callsign, const RouteInfo &info) {
  save(kLookupNsRoute, callsign, encodeRouteInfo(info));
}
bool storeHasRoute(const std::string &callsign) { return has(kLookupNsRoute, callsign); }

bool storeLoadAirport(const std::string &code, AirportInfo &out) {
  std::string s;
  return load(kLookupNsAirport, code, s) && decodeAirportInfo(s, out);
}
void storeSaveAirport(const std::string &code, const AirportInfo &info) {
  save(kLookupNsAirport, code, encodeAirportInfo(info));
}
bool storeHasAirport(const std::string &code) { return has(kLookupNsAirport, code); }
