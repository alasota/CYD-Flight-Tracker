// i18n — on-screen text in Polish or English, selected at runtime from the
// config page (stored in NVS by config_store). See CLAUDE.md "Language".
//
// How it's built (the usual pattern for small Arduino/ESP32 firmware, no
// gettext / no filesystem):
//   - One X-macro list, I18N_STRINGS below, holds every displayed string
//     with its PL and EN text side by side. The Str enum and both
//     translation tables are generated from that one list, so adding a
//     string without one of its translations doesn't compile, and the
//     tables can never get out of step with the enum.
//   - The tables are `static const char *const` arrays of string literals:
//     on the ESP32 those live in flash (.rodata, memory-mapped), not RAM —
//     no PROGMEM / F() needed (those are AVR-era and no-ops on ESP32).
//   - Lookup is an array index: tr(id) costs nothing at draw time.
//
// Text is ASCII-folded Polish ("ZBLIZA SIE", not "ZBLIŻA SIĘ"): the built-in
// LovyanGFX bitmap fonts only cover 0x20-0x7E. The native tests enforce it.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <cstdint>

enum class Language : uint8_t {
  PL = 0,
  EN = 1,
};
constexpr int kLanguageCount = 2;
constexpr Language kDefaultLanguage = Language::PL;

// X(id, polish, english)
#define I18N_STRINGS(X)                                                     \
  /* screen names: header block + bottom nav buttons (<= 7 chars) */       \
  X(ScreenFlights, "LOTY", "FLIGHTS")                                       \
  X(ScreenNow, "TERAZ", "NOW")                                              \
  X(ScreenRadar, "RADAR", "RADAR")                                          \
  /* Screen 1 table column header (Font0, must fit table_view columns) */  \
  X(ColFlight, "LOT", "FLIGHT")                                             \
  X(ColAirline, "LINIA", "AIRLINE")                                         \
  X(ColFrom, "SKAD", "FROM")                                                \
  X(ColTo, "DOKAD", "TO")                                                   \
  X(ColType, "TYP", "TYPE")                                                 \
  X(ColPhase, "FAZA", "PHASE")                                              \
  /* empty states */                                                        \
  X(NoFlightsInRange, "BRAK LOTOW W ZASIEGU", "NO FLIGHTS IN RANGE")        \
  X(NoFlight, "BRAK LOTU", "NO FLIGHT")                                     \
  /* featured pill when the aircraft is taxiing / parked */                 \
  X(OnGround, "NA ZIEMI", "ON GROUND")                                      \
  /* Screen 2 countdown status strip */                                     \
  X(StatusApproaching, "ZBLIZA SIE", "APPROACHING")                         \
  X(StatusOverhead, "NAD TOBA", "OVERHEAD")                                 \
  X(StatusPassed, "MINAL", "PASSED")                                        \
  X(StatusEstimated, "SZACOWANY CZAS", "ESTIMATED TIME")                    \
  X(UnitMinutes, "MIN", "MIN")                                              \
  /* header block "<n> flights" — plural forms, see pluralForm() */         \
  X(FlightsOne, "LOT", "FLIGHT")                                            \
  X(FlightsFew, "LOTY", "FLIGHTS")                                          \
  X(FlightsMany, "LOTOW", "FLIGHTS")                                        \
  /* first-boot setup prompt */                                             \
  X(SetupPrompt, "Ustaw lokalizacje domu na", "Set your home location at")

enum class Str : int {
#define I18N_ENUM_ENTRY(id, pl, en) id,
  I18N_STRINGS(I18N_ENUM_ENTRY)
#undef I18N_ENUM_ENTRY
};

#define I18N_COUNT_ENTRY(id, pl, en) +1
constexpr int kStrCount = 0 I18N_STRINGS(I18N_COUNT_ENTRY);
#undef I18N_COUNT_ENTRY

// `id` in `lang`. An out-of-range id returns "?" rather than reading past
// the table.
const char *tr(Str id, Language lang);

// `id` in the current language (i18nSetLanguage()) — what the draw code
// uses, so the language doesn't have to be threaded through every draw
// call. Pure logic that returns text takes an explicit Language instead.
const char *tr(Str id);

void i18nSetLanguage(Language lang);
Language i18nLanguage();

// "pl" / "en" — the value used by the config page's <select> (and nothing
// else; NVS stores the numeric index).
const char *languageCode(Language lang);

// Inverse of languageCode(); anything unknown (or nullptr) ->
// kDefaultLanguage.
Language languageFromCode(const char *code);

// A raw stored index (NVS) -> a valid Language; out of range ->
// kDefaultLanguage.
Language clampLanguage(int raw);

// Grammatical number for a count `n` (n >= 0):
//   EN: 1 -> One, everything else -> Many.
//   PL: 1 -> One ("1 LOT"); last digit 2-4 except 12-14 -> Few ("3 LOTY",
//       "22 LOTY"); everything else, incl. 0 and 12-14 -> Many ("5 LOTOW",
//       "12 LOTOW").
enum class PluralForm { One, Few, Many };
PluralForm pluralForm(int n, Language lang);
