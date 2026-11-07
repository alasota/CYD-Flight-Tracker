#include "display/core/screens/aircraft_summary.h"

#include "enrichment/core/aircraft_names.h"  // airlineForCallsign(), friendlyAircraftType()
#include "enrichment/core/place_names.h"     // placeName()
#include "display/core/text/fit_to_width.h"      // fitToWidth()

namespace {

std::string orDash(const std::string &s) { return s.empty() ? "--" : s; }

// One route endpoint, formatted per `fmt`. `airport` may be unresolved
// (found == false) — then fall back to the bare ICAO code, never a
// country.
std::string endpointLabel(const std::string &icao, const AirportInfo &airport, RouteFormat fmt) {
  if (!airport.found || airport.iata_code.empty()) {
    return icao;
  }
  if (fmt == RouteFormat::WithCountry) {
    return airport.iata_code + " (" + airport.country_code + ")";
  }
  return airport.iata_code;
}

}  // namespace

std::string summaryFlight(const TrackedFlight &row) { return orDash(row.aircraft.callsign); }

std::string summaryAirline(const TrackedFlight &row) {
  // hexdb.io's registered owner first, then the airline the callsign's ICAO
  // prefix names (works even when hexdb.io doesn't know the airframe).
  if (row.info.found && !row.info.airline.empty()) return row.info.airline;
  if (const char *fromCallsign = airlineForCallsign(row.aircraft.callsign)) return fromCallsign;
  if (row.info.found) return kUnknownText;
  return row.info_pending ? kPendingText : kUnknownText;
}

std::string summaryType(const TrackedFlight &row) {
  // Short friendly name from the ICAO type code ("737 MAX 8"), else
  // hexdb.io's longer "Type" string.
  if (const char *friendly = friendlyAircraftType(row.info.icao_type)) return friendly;
  if (row.info.found) return orDash(row.info.aircraft_type);
  return row.info_pending ? kPendingText : kUnknownText;
}

std::string fitSummaryIdentity(const TrackedFlight &row, int maxPx, const TextMeasure &measure) {
  static const std::string kGap = "  ";
  const std::string flight = summaryFlight(row);
  const std::string airline = summaryAirline(row);
  const std::string type = summaryType(row);

  const std::string full = flight + kGap + airline + kGap + type;
  if (measure(full) <= maxPx) return full;

  // Airline cut to whatever's left — but only if at least "X..." fits.
  const int airlineBudget = maxPx - measure(flight + kGap) - measure(kGap + type);
  if (airlineBudget >= measure(airline.substr(0, 1) + "...")) {
    return flight + kGap + fitToWidth(airline, airlineBudget, measure) + kGap + type;
  }

  // No room for the airline at all.
  const std::string noAirline = flight + kGap + type;
  if (measure(noAirline) <= maxPx) return noAirline;

  const std::string cutType = fitToWidth(type, maxPx - measure(flight + kGap), measure);
  return cutType.empty() ? flight : flight + kGap + cutType;
}

std::string formatSummaryRoute(const RouteInfo &route, const AirportInfo &originAirport,
                               const AirportInfo &destAirport, RouteFormat fmt) {
  if (!route.found) {
    return "--";
  }
  return endpointLabel(route.origin_icao, originAirport, fmt) + " -> " +
         endpointLabel(route.dest_icao, destAirport, fmt);
}

namespace {

// One endpoint of `row`'s route, pending-aware (see rowAirportCode()).
std::string rowEndpoint(const TrackedFlight &row, bool origin, RouteFormat fmt) {
  const AirportInfo &airport = origin ? row.origin_airport : row.dest_airport;
  const bool pending = origin ? row.origin_airport_pending : row.dest_airport_pending;
  const std::string &icao = origin ? row.route.origin_icao : row.route.dest_icao;
  if (airport.found && !airport.iata_code.empty()) return endpointLabel(icao, airport, fmt);
  if (pending) return kPendingText;
  return orDash(icao);
}

}  // namespace

std::string rowAirportCode(const TrackedFlight &row, bool origin) {
  if (!row.route.found) return row.route_pending ? kPendingText : kUnknownText;
  return rowEndpoint(row, origin, RouteFormat::CodesOnly);
}

std::string formatRowRoute(const TrackedFlight &row, RouteFormat fmt) {
  if (!row.route.found) return row.route_pending ? kPendingText : kUnknownText;
  return rowEndpoint(row, true, fmt) + " -> " + rowEndpoint(row, false, fmt);
}

std::string rowPlaceName(const TrackedFlight &row, bool origin, Language lang) {
  if (!row.route.found) return row.route_pending ? kPendingText : kUnknownText;
  const bool pending = origin ? row.origin_airport_pending : row.dest_airport_pending;
  const AirportInfo &airport = origin ? row.origin_airport : row.dest_airport;
  if (!airport.found && pending) return kPendingText;
  return orDash(placeName(airport, origin ? row.route.origin_icao : row.route.dest_icao, lang));
}

std::string formatRowRouteCities(const TrackedFlight &row, Language lang, int maxPx,
                                 const TextMeasure &measure) {
  if (!row.route.found) return row.route_pending ? kPendingText : kUnknownText;

  static const std::string kArrow = " -> ";
  const std::string from = rowPlaceName(row, true, lang);
  const std::string to = rowPlaceName(row, false, lang);
  auto withCountry = [](const std::string &place, const AirportInfo &a) {
    return (a.found && !a.country_code.empty()) ? place + " (" + a.country_code + ")" : place;
  };

  const std::string full =
      withCountry(from, row.origin_airport) + kArrow + withCountry(to, row.dest_airport);
  if (measure(full) <= maxPx) return full;

  const std::string bare = from + kArrow + to;
  if (measure(bare) <= maxPx) return bare;

  const int half = (maxPx - measure(kArrow)) / 2;
  return fitToWidth(from, half, measure) + kArrow + fitToWidth(to, half, measure);
}
