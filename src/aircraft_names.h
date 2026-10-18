// aircraft_names — built-in tables that make aircraft readable even when
// hexdb.io doesn't know the airframe:
//   - airline from the callsign's ICAO airline prefix ("SXS6JZ" ->
//     "SunExpress"), since an airline flight's callsign is the 3-letter
//     operator code + flight number;
//   - a short, friendly aircraft type from the ICAO type designator
//     ("B38M" -> "737 MAX 8", "E75L" -> "E175") instead of hexdb.io's long
//     "Type" strings ("737MAX 8", "EMB-175 STD") that got cut off.
//
// Focused on what flies over Poland: European airlines/types, the big
// long-haul carriers crossing the region, cargo, plus common business /
// GA types. Unknown codes return nullptr so callers fall back to hexdb.io.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <string>

struct NameEntry {
  const char *code;
  const char *name;
};

// Airline for an airline-flight callsign: 3 letters followed by a digit
// ("LOT281", "WZZ620U"). Registration-style callsigns ("SPKAX", "N123AB")
// and unknown prefixes -> nullptr.
const char *airlineForCallsign(const std::string &callsign);

// Friendly name for an ICAO type designator ("B738" -> "737-800"), or
// nullptr if it isn't in the table. Names are <= 12 characters (the
// table's TYP column).
const char *friendlyAircraftType(const std::string &icaoType);

// The tables — exposed for the consistency tests.
int airlineTableSize();
const NameEntry &airlineTableEntry(int index);
int typeTableSize();
const NameEntry &typeTableEntry(int index);
