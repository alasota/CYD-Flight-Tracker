#include "enrichment/domain/place_names.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace {

// ASCII replacements for U+00C0..U+00FF (Latin-1 letters) and
// U+0100..U+017F (Latin Extended-A, incl. all Polish letters), one char per
// code point.
const char kLatin1Fold[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
const char kLatinExtAFold[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRr"
    "SsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static_assert(sizeof(kLatin1Fold) - 1 == 0x40, "one entry per U+00C0..U+00FF");
static_assert(sizeof(kLatinExtAFold) - 1 == 0x80, "one entry per U+0100..U+017F");

// Sorted by IATA (binary search) — Polish airports, popular destinations
// from Poland, and the big hubs whose long-haul traffic crosses Polish
// airspace. Polish names are real Polish exonyms (UTF-8, font-safe:
// ASCII + ąćęłńóśźż); English names stay plain ASCII.
const CityEntry kCities[] = {
    {"ADB", "Izmir", "Izmir"},
    {"AGP", "Malaga", "Malaga"},
    {"AMS", "Amsterdam", "Amsterdam"},
    {"ARN", "Sztokholm", "Stockholm"},
    {"ATH", "Ateny", "Athens"},
    {"AUH", "Abu Zabi", "Abu Dhabi"},
    {"AYT", "Antalya", "Antalya"},
    {"BCN", "Barcelona", "Barcelona"},
    {"BEG", "Belgrad", "Belgrade"},
    {"BER", "Berlin", "Berlin"},
    {"BEY", "Bejrut", "Beirut"},
    {"BGY", "Bergamo", "Bergamo"},
    {"BHX", "Birmingham", "Birmingham"},
    {"BKK", "Bangkok", "Bangkok"},
    {"BLL", "Billund", "Billund"},
    {"BLQ", "Bolonia", "Bologna"},
    {"BOS", "Boston", "Boston"},
    {"BRI", "Bari", "Bari"},
    {"BRU", "Bruksela", "Brussels"},
    {"BSL", "Bazylea", "Basel"},
    {"BTS", "Bratysława", "Bratislava"},
    {"BUD", "Budapeszt", "Budapest"},
    {"BVA", "Paryż-Beauvais", "Paris-Beauvais"},
    {"BZG", "Bydgoszcz", "Bydgoszcz"},
    {"CAI", "Kair", "Cairo"},
    {"CDG", "Paryż", "Paris"},
    {"CFU", "Korfu", "Corfu"},
    {"CGN", "Kolonia", "Cologne"},
    {"CHQ", "Chania", "Chania"},
    {"CIA", "Rzym-Ciampino", "Rome-Ciampino"},
    {"CPH", "Kopenhaga", "Copenhagen"},
    {"CTA", "Katania", "Catania"},
    {"DEL", "Delhi", "Delhi"},
    {"DLM", "Dalaman", "Dalaman"},
    {"DOH", "Doha", "Doha"},
    {"DUB", "Dublin", "Dublin"},
    {"DUS", "Dusseldorf", "Dusseldorf"},
    {"DXB", "Dubaj", "Dubai"},
    {"EDI", "Edynburg", "Edinburgh"},
    {"EIN", "Eindhoven", "Eindhoven"},
    {"EVN", "Erywan", "Yerevan"},
    {"EWR", "Nowy Jork-Newark", "New York-Newark"},
    {"FAO", "Faro", "Faro"},
    {"FCO", "Rzym", "Rome"},
    {"FRA", "Frankfurt", "Frankfurt"},
    {"FUE", "Fuerteventura", "Fuerteventura"},
    {"GDN", "Gdańsk", "Gdansk"},
    {"GLA", "Glasgow", "Glasgow"},
    {"GOA", "Genua", "Genoa"},
    {"GVA", "Genewa", "Geneva"},
    {"HAJ", "Hanower", "Hanover"},
    {"HAM", "Hamburg", "Hamburg"},
    {"HEL", "Helsinki", "Helsinki"},
    {"HER", "Heraklion", "Heraklion"},
    {"HND", "Tokio-Haneda", "Tokyo-Haneda"},
    {"HRG", "Hurghada", "Hurghada"},
    {"ICN", "Seul", "Seoul"},
    {"IST", "Stambul", "Istanbul"},
    {"JFK", "Nowy Jork", "New York"},
    {"KBP", "Kijów", "Kyiv"},
    {"KRK", "Kraków", "Krakow"},
    {"KTW", "Katowice", "Katowice"},
    {"KUT", "Kutaisi", "Kutaisi"},
    {"LAX", "Los Angeles", "Los Angeles"},
    {"LCA", "Larnaka", "Larnaca"},
    {"LCJ", "Łódź", "Lodz"},
    {"LGW", "Londyn-Gatwick", "London-Gatwick"},
    {"LHR", "Londyn", "London"},
    {"LIS", "Lizbona", "Lisbon"},
    {"LTN", "Londyn-Luton", "London-Luton"},
    {"LUZ", "Lublin", "Lublin"},
    {"LWO", "Lwów", "Lviv"},
    {"LYS", "Lyon", "Lyon"},
    {"MAD", "Madryt", "Madrid"},
    {"MAN", "Manchester", "Manchester"},
    {"MLA", "Malta", "Malta"},
    {"MUC", "Monachium", "Munich"},
    {"MXP", "Mediolan", "Milan"},
    {"NAP", "Neapol", "Naples"},
    {"NCE", "Nicea", "Nice"},
    {"NQZ", "Astana", "Astana"},
    {"NRT", "Tokio", "Tokyo"},
    {"OPO", "Porto", "Porto"},
    {"ORD", "Chicago", "Chicago"},
    {"OSL", "Oslo", "Oslo"},
    {"OTP", "Bukareszt", "Bucharest"},
    {"PEK", "Pekin", "Beijing"},
    {"PMI", "Palma", "Palma de Mallorca"},
    {"POZ", "Poznań", "Poznan"},
    {"PRG", "Praga", "Prague"},
    {"PSA", "Piza", "Pisa"},
    {"RHO", "Rodos", "Rhodes"},
    {"RIX", "Ryga", "Riga"},
    {"RZE", "Rzeszów", "Rzeszow"},
    {"SAW", "Stambul-Sabiha", "Istanbul-Sabiha"},
    {"SKG", "Saloniki", "Thessaloniki"},
    {"SOF", "Sofia", "Sofia"},
    {"SPU", "Split", "Split"},
    {"STN", "Londyn-Stansted", "London-Stansted"},
    {"STR", "Stuttgart", "Stuttgart"},
    {"SVQ", "Sewilla", "Seville"},
    {"SZY", "Olsztyn", "Olsztyn"},
    {"SZZ", "Szczecin", "Szczecin"},
    {"TAS", "Taszkent", "Tashkent"},
    {"TBS", "Tbilisi", "Tbilisi"},
    {"TFS", "Teneryfa", "Tenerife"},
    {"TGD", "Podgorica", "Podgorica"},
    {"TLL", "Tallin", "Tallinn"},
    {"TLV", "Tel Awiw", "Tel Aviv"},
    {"TRN", "Turyn", "Turin"},
    {"VCE", "Wenecja", "Venice"},
    {"VIE", "Wiedeń", "Vienna"},
    {"VNO", "Wilno", "Vilnius"},
    {"WAW", "Warszawa", "Warsaw"},
    {"WMI", "Warszawa-Modlin", "Warsaw-Modlin"},
    {"WRO", "Wrocław", "Wroclaw"},
    {"YYZ", "Toronto", "Toronto"},
    {"ZAD", "Zadar", "Zadar"},
    {"ZRH", "Zurych", "Zurich"},
};
constexpr int kCityCount = static_cast<int>(sizeof(kCities) / sizeof(kCities[0]));

bool isGenericAirportWord(const std::string &word) {
  static const char *const kGeneric[] = {"international", "airport", "airfield", "aerodrome",
                                         "intl"};
  std::string lower;
  for (char c : word) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char *g : kGeneric) {
    if (lower == g) return true;
  }
  return false;
}

}  // namespace

std::string asciiFold(const std::string &utf8) {
  std::string out;
  out.reserve(utf8.size());
  const size_t n = utf8.size();
  size_t i = 0;
  while (i < n) {
    const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
    if (b0 < 0x80) {  // plain ASCII — keep printable characters only
      if (b0 >= 0x20 && b0 < 0x7F) out += static_cast<char>(b0);
      ++i;
      continue;
    }
    // Length of this UTF-8 sequence (invalid lead bytes count as 1).
    size_t len = (b0 >= 0xF0) ? 4 : (b0 >= 0xE0) ? 3 : (b0 >= 0xC0) ? 2 : 1;
    if (i + len > n) break;  // truncated sequence at the end — drop it
    if (len == 2) {
      const unsigned cp = ((b0 & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
      if (cp >= 0xC0 && cp <= 0xFF) out += kLatin1Fold[cp - 0xC0];
      else if (cp >= 0x100 && cp <= 0x17F) out += kLatinExtAFold[cp - 0x100];
    } else if (len == 3 && b0 == 0xE2 && static_cast<unsigned char>(utf8[i + 1]) == 0x80) {
      const unsigned char b2 = static_cast<unsigned char>(utf8[i + 2]);
      if (b2 >= 0x90 && b2 <= 0x95) out += '-';            // hyphens, en/em dashes
      else if (b2 >= 0x98 && b2 <= 0x9B) out += '\'';      // curly single quotes
      else if (b2 >= 0x9C && b2 <= 0x9F) out += '"';       // curly double quotes
    }
    i += len;
  }
  return out;
}

std::string shortAirportName(const std::string &airportName) {
  std::vector<std::string> kept;
  std::string word;
  auto flush = [&]() {
    if (!word.empty() && !isGenericAirportWord(word)) kept.push_back(word);
    word.clear();
  };
  for (char c : airportName) {
    if (c == ' ') flush();
    else word += c;
  }
  flush();
  if (kept.empty()) return airportName;

  std::string out;
  for (const std::string &w : kept) {
    if (!out.empty()) out += ' ';
    out += w;
  }
  return out;
}

const char *cityForIata(const std::string &iata, Language lang) {
  if (iata.size() != 3) return nullptr;
  int lo = 0;
  int hi = kCityCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int cmp = std::strcmp(iata.c_str(), kCities[mid].iata);
    if (cmp == 0) return (lang == Language::EN) ? kCities[mid].en : kCities[mid].pl;
    if (cmp < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return nullptr;
}

int cityTableSize() { return kCityCount; }

const CityEntry &cityTableEntry(int index) { return kCities[index]; }

std::string placeName(const AirportInfo &airport, const std::string &icaoFallback, Language lang) {
  if (!airport.found) return icaoFallback;
  if (const char *city = cityForIata(airport.iata_code, lang)) return city;
  if (!airport.name.empty()) return shortAirportName(airport.name);
  if (!airport.iata_code.empty()) return airport.iata_code;
  return icaoFallback;
}
