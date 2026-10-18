#include "aircraft_names.h"

#include <cstring>

namespace {

// ICAO airline designator -> short display name. Linear search (a few
// dozen entries, looked up a handful of times per redraw).
const NameEntry kAirlines[] = {
    {"AAL", "American"},       {"ABW", "AirBridgeCargo"}, {"ACA", "Air Canada"},
    {"AEE", "Aegean"},         {"AFL", "Aeroflot"},       {"AFR", "Air France"},
    {"AHY", "Azerbaijan Air"}, {"AIC", "Air India"},      {"ANA", "ANA"},
    {"AUA", "Austrian"},       {"AUI", "UIA"},            {"BAW", "British Airways"},
    {"BCS", "DHL (EAT)"},      {"BEL", "Brussels Airl."}, {"BTI", "airBaltic"},
    {"CAL", "China Airlines"}, {"CCA", "Air China"},      {"CES", "China Eastern"},
    {"CFG", "Condor"},         {"CLX", "Cargolux"},       {"CPA", "Cathay Pacific"},
    {"CSA", "Czech Airlines"}, {"CSN", "China Southern"}, {"CTN", "Croatia Airl."},
    {"DAL", "Delta"},          {"DHK", "DHL Air"},        {"DLH", "Lufthansa"},
    {"EDW", "Edelweiss"},      {"EIN", "Aer Lingus"},     {"EJU", "easyJet"},
    {"ELY", "El Al"},          {"ENT", "Enter Air"},      {"ETD", "Etihad"},
    {"ETH", "Ethiopian"},      {"EVA", "EVA Air"},        {"EWG", "Eurowings"},
    {"EXS", "Jet2"},           {"EZS", "easyJet"},        {"EZY", "easyJet"},
    {"FDX", "FedEx"},          {"FHY", "Freebird"},       {"FIN", "Finnair"},
    {"GEC", "Lufthansa Cargo"},{"GTI", "Atlas Air"},      {"HVN", "Vietnam Airl."},
    {"IBE", "Iberia"},         {"IBK", "Norwegian"},      {"ICE", "Icelandair"},
    {"ITY", "ITA Airways"},    {"JAL", "Japan Airlines"}, {"KAL", "Korean Air"},
    {"KLM", "KLM"},            {"KZR", "Air Astana"},     {"LDM", "Lauda"},
    {"LGL", "Luxair"},         {"LOT", "LOT"},            {"LZB", "Bulgaria Air"},
    {"MLD", "Air Moldova"},    {"MSR", "EgyptAir"},       {"NAX", "Norwegian"},
    {"NOZ", "Norwegian"},      {"NSZ", "Norwegian"},      {"OAW", "Helvetic"},
    {"PGT", "Pegasus"},        {"QFA", "Qantas"},         {"QTR", "Qatar Airways"},
    {"RJA", "Royal Jordanian"},{"ROT", "TAROM"},          {"RUK", "Ryanair"},
    {"RYR", "Ryanair"},        {"RYS", "Buzz"},           {"SAS", "SAS"},
    {"SIA", "Singapore Airl."},{"SVA", "Saudia"},         {"SWR", "Swiss"},
    {"SXS", "SunExpress"},     {"TAP", "TAP Portugal"},   {"TAY", "ASL Belgium"},
    {"THA", "Thai"},           {"THY", "Turkish Airl."},  {"TOM", "TUI"},
    {"TRA", "Transavia"},      {"TUI", "TUI fly"},        {"TVF", "Transavia"},
    {"TVS", "Smartwings"},     {"UAE", "Emirates"},       {"UAL", "United"},
    {"UPS", "UPS"},            {"UZB", "Uzbekistan Airw."},{"VLG", "Vueling"},
    {"VOE", "Volotea"},        {"WMT", "Wizz Air"},       {"WUK", "Wizz Air"},
    {"WZZ", "Wizz Air"},
};
constexpr int kAirlineCount = static_cast<int>(sizeof(kAirlines) / sizeof(kAirlines[0]));

// ICAO type designator -> friendly name (<= 12 characters).
const NameEntry kTypes[] = {
    // Airbus
    {"A318", "A318"},          {"A319", "A319"},          {"A320", "A320"},
    {"A321", "A321"},          {"A19N", "A319neo"},       {"A20N", "A320neo"},
    {"A21N", "A321neo"},       {"BCS1", "A220-100"},      {"BCS3", "A220-300"},
    {"A306", "A300-600"},      {"A332", "A330-200"},      {"A333", "A330-300"},
    {"A338", "A330-800"},      {"A339", "A330-900"},      {"A343", "A340-300"},
    {"A359", "A350-900"},      {"A35K", "A350-1000"},     {"A388", "A380"},
    {"A400", "A400M"},
    // Boeing
    {"B733", "737-300"},       {"B734", "737-400"},       {"B735", "737-500"},
    {"B736", "737-600"},       {"B737", "737-700"},       {"B738", "737-800"},
    {"B739", "737-900"},       {"B37M", "737 MAX 7"},     {"B38M", "737 MAX 8"},
    {"B39M", "737 MAX 9"},     {"B3XM", "737 MAX 10"},    {"B744", "747-400"},
    {"B748", "747-8"},         {"B752", "757-200"},       {"B753", "757-300"},
    {"B762", "767-200"},       {"B763", "767-300"},       {"B764", "767-400"},
    {"B772", "777-200"},       {"B77L", "777-200LR"},     {"B77W", "777-300ER"},
    {"B778", "777-8"},         {"B779", "777-9"},         {"B788", "787-8"},
    {"B789", "787-9"},         {"B78X", "787-10"},        {"MD11", "MD-11"},
    // Regional
    {"E170", "E170"},          {"E75L", "E175"},          {"E75S", "E175"},
    {"E175", "E175"},          {"E190", "E190"},          {"E195", "E195"},
    {"E290", "E190-E2"},       {"E295", "E195-E2"},       {"CRJ2", "CRJ200"},
    {"CRJ7", "CRJ700"},        {"CRJ9", "CRJ900"},        {"CRJX", "CRJ1000"},
    {"DH8D", "Dash 8-400"},    {"AT45", "ATR 42"},        {"AT72", "ATR 72"},
    {"AT75", "ATR 72-500"},    {"AT76", "ATR 72-600"},    {"SB20", "Saab 2000"},
    {"SF34", "Saab 340"},      {"SU95", "Superjet 100"},
    // Business / GA / military
    {"C25A", "Citation CJ2"},  {"C56X", "Citation XLS"},  {"C68A", "Citation Lat"},
    {"CL35", "Challenger"},    {"CL60", "Challenger"},    {"GLF5", "Gulfstream V"},
    {"GLF6", "G650"},          {"GLEX", "Global Expr."},  {"FA7X", "Falcon 7X"},
    {"F900", "Falcon 900"},    {"LJ45", "Learjet 45"},    {"PC12", "PC-12"},
    {"PC24", "PC-24"}, {"TBM9", "TBM 900"},       {"BE20", "King Air 200"},
    {"B350", "King Air 350"},  {"C172", "Cessna 172"},    {"C152", "Cessna 152"},
    {"P28A", "Piper PA-28"},   {"C130", "C-130"},         {"C17", "C-17"},
    {"K35R", "KC-135"},        {"AN12", "An-12"},         {"A124", "An-124"},
    {"IL76", "Il-76"},
};
constexpr int kTypeCount = static_cast<int>(sizeof(kTypes) / sizeof(kTypes[0]));

const char *find(const NameEntry *table, int count, const char *code) {
  for (int i = 0; i < count; ++i) {
    if (std::strcmp(table[i].code, code) == 0) return table[i].name;
  }
  return nullptr;
}

bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

const char *airlineForCallsign(const std::string &callsign) {
  if (callsign.size() < 4) return nullptr;
  if (!isUpper(callsign[0]) || !isUpper(callsign[1]) || !isUpper(callsign[2])) return nullptr;
  if (!isDigit(callsign[3])) return nullptr;
  return find(kAirlines, kAirlineCount, callsign.substr(0, 3).c_str());
}

const char *friendlyAircraftType(const std::string &icaoType) {
  if (icaoType.empty()) return nullptr;
  return find(kTypes, kTypeCount, icaoType.c_str());
}

int airlineTableSize() { return kAirlineCount; }
const NameEntry &airlineTableEntry(int index) { return kAirlines[index]; }
int typeTableSize() { return kTypeCount; }
const NameEntry &typeTableEntry(int index) { return kTypes[index]; }
