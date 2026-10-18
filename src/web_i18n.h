// web_i18n — Polish / English text of the config web page (config_page),
// chosen independently of the display language (Config::web_language).
// Same X-macro pattern as i18n: one list with both translations side by
// side, the enum and both tables generated from it, so a string can't be
// added without both translations. Literals stay in flash.
//
// Unlike i18n's on-screen strings these are rendered by a browser, so any
// UTF-8 is fine — but they're inserted raw into HTML attributes and a JS
// string table, so they must not contain '"', '<', '>', '\\' or '&'
// (test_web_i18n enforces it).
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include "i18n.h"  // Language

// X(id, polish, english)
#define WEB_I18N_STRINGS(X)                                                                        \
  X(PageTitle, "Sky Tracker — ustawienia", "Sky Tracker — settings")                               \
  X(Subtitle, "Panel ustawień wyświetlacza", "Display settings")                                   \
  X(TabLocation, "Lokalizacja", "Location")                                                        \
  X(TabOpenSky, "Klient OpenSky", "OpenSky client")                                                \
  X(TabGeneral, "Ogólne", "General")                                                               \
  /* Location */                                                                                   \
  X(LocationIntro, "Środek obszaru, nad którym śledzone są samoloty — zwykle Twój dom.",           \
    "The centre of the area to watch — usually your home.")                                        \
  X(Latitude, "Szerokość geograficzna", "Latitude")                                                \
  X(Longitude, "Długość geograficzna", "Longitude")                                                \
  X(LatLonHint,                                                                                    \
    "Stopnie dziesiętne, np. 52.229700 i 21.012200. Najprościej skopiować z Map Google "           \
    "(prawy klik na mapie).",                                                                      \
    "Decimal degrees, e.g. 52.229700 and 21.012200. Easiest to copy from Google Maps "             \
    "(right-click the map).")                                                                      \
  X(Radius, "Promień skanowania (°)", "Scan radius (°)")                                           \
  X(RadiusHint, "Kwadrat ±promień wokół domu; 1° to ok. 111 km na północ i południe.",             \
    "A square of ±radius around home; 1° is about 111 km north and south.")                        \
  X(CreditsPerRequest, "Kredyty OpenSky na zapytanie", "OpenSky credits per request")              \
  /* OpenSky */                                                                                    \
  X(OpenSkyIntro,                                                                                  \
    "Bez konta wyświetlacz działa anonimowo (400 kredytów dziennie). Konto OpenSky podnosi "       \
    "limit do 4000.",                                                                              \
    "Without an account the display runs anonymously (400 credits a day). An OpenSky account "     \
    "raises the limit to 4000.")                                                                   \
  X(StatusAccount, "Konto OpenSky skonfigurowane", "OpenSky account configured")                   \
  X(StatusAnonymous, "Tryb anonimowy", "Anonymous mode")                                           \
  X(ClientId, "Client ID", "Client ID")                                                            \
  X(ClientSecret, "Client secret", "Client secret")                                                \
  X(ShowSecret, "Pokaż", "Show")                                                                   \
  X(CredentialsHint, "Utworzysz je na opensky-network.org → Account → API client.",                \
    "Create them at opensky-network.org → Account → API client.")                                  \
  X(UploadTitle, "Albo wczytaj plik z danymi", "Or load the credentials file")                     \
  X(UploadHint, "Plik JSON pobrany ze strony konta OpenSky (credentials.json).",                   \
    "The JSON file downloaded from your OpenSky account page (credentials.json).")                 \
  X(UploadButton, "Wczytaj plik", "Load file")                                                     \
  X(PollInterval, "Odświeżanie co (s)", "Refresh every (s)")                                       \
  X(CreditsPerDay, "kredytów dziennie", "credits a day")                                           \
  X(DailyLimit, "limit", "limit")                                                                  \
  X(OverLimit, "ponad limit — odświeżaj rzadziej albo zmniejsz promień",                           \
    "over the limit — refresh less often or shrink the radius")                                    \
  /* General */                                                                                    \
  X(SectionLanguage, "Język", "Language")                                                          \
  X(DisplayLanguage, "Język wyświetlacza", "Display language")                                     \
  X(WebLanguage, "Język panelu WWW", "Web panel language")                                         \
  X(SectionScreens, "Ekrany", "Screens")                                                           \
  X(AutoCycle, "Przełączaj ekrany automatycznie", "Cycle screens automatically")                   \
  X(AutoCycleInterval, "Co ile sekund", "Every (s)")                                               \
  X(SectionPhases, "Fazy lotu", "Flight phases")                                                   \
  X(NearAirport, "Strefa lotniska (km)", "Airport zone (km)")                                      \
  X(NearAirportHint,                                                                               \
    "W tej odległości od domu samolot dostaje ikonkę startu, lądowania lub przelotu.",             \
    "Within this distance of home an aircraft gets a takeoff, landing or overflight icon.")        \
  X(ClimbRate, "Próg wznoszenia / opadania (m/s)", "Climb / descent threshold (m/s)")              \
  X(ClimbRateHint, "Szybciej w górę to start, szybciej w dół to lądowanie.",                       \
    "Climbing faster is a takeoff, descending faster a landing.")                                  \
  /* Save + messages */                                                                            \
  X(Save, "Zapisz", "Save")                                                                        \
  X(Saved, "Zapisano.", "Saved.")                                                                  \
  X(SavedWithInvalid, "Zapisano, ale te pola były błędne i zostały poprzednie wartości:",          \
    "Saved, but these fields were invalid and kept their previous values:")                        \
  X(UploadTooLarge, "Nie wczytano: plik jest za duży.", "Not loaded: the file is too large.")      \
  X(UploadInvalid, "Nie wczytano: w pliku brak poprawnych clientId / clientSecret.",               \
    "Not loaded: the file has no valid clientId / clientSecret.")                                  \
  X(UploadOk, "Wczytano dane OpenSky z pliku.", "OpenSky credentials loaded from the file.")       \
  X(Screenshot, "Zrzut ekranu", "Screenshot")                                                     \
  X(Version, "wersja", "version")

enum class WebStr : uint16_t {
#define WEB_I18N_ENUM(id, pl, en) id,
  WEB_I18N_STRINGS(WEB_I18N_ENUM)
#undef WEB_I18N_ENUM
      Count
};
constexpr int kWebStrCount = static_cast<int>(WebStr::Count);

// Text of `id` in `lang` (out-of-range id -> "").
const char *webTr(WebStr id, Language lang);
