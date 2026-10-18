#include "lookup_store.h"

#include <vector>

namespace {

constexpr char kSep = '\x1F';    // ASCII unit separator — never in real names
constexpr char kVersion = '1';

// A value with any separator byte replaced, so it can't split the record.
std::string clean(const std::string &s) {
  std::string out = s;
  for (char &c : out) {
    if (c == kSep) c = ' ';
  }
  return out;
}

std::string join(const std::vector<std::string> &fields) {
  std::string out(1, kVersion);
  for (const std::string &f : fields) {
    out += kSep;
    out += clean(f);
  }
  return out;
}

// Splits a record; false unless it has our version and exactly `count`
// fields after it.
bool split(const std::string &text, size_t count, std::vector<std::string> &fields) {
  if (text.empty() || text[0] != kVersion) return false;
  fields.clear();
  size_t start = 1;
  while (start <= text.size()) {
    if (start == text.size() || text[start] != kSep) return false;
    const size_t end = text.find(kSep, start + 1);
    fields.push_back(text.substr(start + 1, end == std::string::npos ? std::string::npos
                                                                      : end - start - 1));
    if (end == std::string::npos) break;
    start = end;
  }
  return fields.size() == count;
}

const char *flag(bool b) { return b ? "1" : "0"; }

}  // namespace

std::string encodeAircraftInfo(const AircraftInfo &info) {
  return join({flag(info.found), info.airline, info.aircraft_type, info.icao_type,
               info.registration});
}

bool decodeAircraftInfo(const std::string &text, AircraftInfo &out) {
  std::vector<std::string> f;
  if (!split(text, 5, f)) return false;
  out = AircraftInfo{};
  out.found = (f[0] == "1");
  out.airline = f[1];
  out.aircraft_type = f[2];
  out.icao_type = f[3];
  out.registration = f[4];
  return true;
}

std::string encodeRouteInfo(const RouteInfo &info) {
  return join({flag(info.found), info.origin_icao, info.dest_icao});
}

bool decodeRouteInfo(const std::string &text, RouteInfo &out) {
  std::vector<std::string> f;
  if (!split(text, 3, f)) return false;
  out = RouteInfo{};
  out.found = (f[0] == "1");
  out.origin_icao = f[1];
  out.dest_icao = f[2];
  return true;
}

std::string encodeAirportInfo(const AirportInfo &info) {
  return join({flag(info.found), info.iata_code, info.country_code, info.name});
}

bool decodeAirportInfo(const std::string &text, AirportInfo &out) {
  std::vector<std::string> f;
  if (!split(text, 4, f)) return false;
  out = AirportInfo{};
  out.found = (f[0] == "1");
  out.iata_code = f[1];
  out.country_code = f[2];
  out.name = f[3];
  return true;
}

#ifdef ARDUINO

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

#endif  // ARDUINO
