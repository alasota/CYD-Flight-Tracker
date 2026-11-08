#include "enrichment/core/lookup_store.h"

#include <cstring>
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

// Slots a string takes in NVS: one header plus one per started 32 bytes,
// counting its NUL.
size_t slotsForString(size_t length) { return 1 + (length + 1 + 31) / 32; }

}  // namespace

bool storeSaveRecord(LookupNvs &nvs, const char *ns, const std::string &key,
                     const std::string &value) {
  // Wipe order: this record's own kind first, then the others.
  const char *const kinds[] = {kLookupNsAircraft, kLookupNsRoute, kLookupNsAirport};
  const char *order[1 + sizeof(kinds) / sizeof(kinds[0])] = {ns};
  size_t count = 1;
  for (const char *kind : kinds) {
    if (strcmp(kind, ns) != 0) order[count++] = kind;
  }

  const size_t needed = kLookupStoreReserveEntries + slotsForString(value.size());
  size_t next = 0;
  while (next < count && nvs.freeEntries() < needed) nvs.clear(order[next++]);

  while (!nvs.put(ns, key, value)) {
    if (next == count) return false;
    nvs.clear(order[next++]);
  }
  return true;
}

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
