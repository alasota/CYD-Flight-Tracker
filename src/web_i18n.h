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
  X(TabBackup, "Kopia", "Backup")                                                                  \
  X(TabFirmware, "Firmware", "Firmware")                                                           \
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
  X(SecretKeepPlaceholder, "zapisany — zostaw puste, żeby nie zmieniać",                           \
    "saved — leave empty to keep it")                                                              \
  X(ClearCredentials, "Usuń dane logowania OpenSky (tryb anonimowy)",                              \
    "Remove the OpenSky credentials (anonymous mode)")                                             \
  X(UploadTitle, "Albo wczytaj plik z danymi", "Or load the credentials file")                     \
  X(UploadHint, "Plik JSON pobrany ze strony konta OpenSky (credentials.json).",                   \
    "The JSON file downloaded from your OpenSky account page (credentials.json).")                 \
  X(UploadButton, "Wczytaj plik", "Load file")                                                     \
  X(PollInterval, "Odświeżanie co (s)", "Refresh every (s)")                                       \
  X(CreditsPerDay, "kredytów dziennie", "credits a day")                                           \
  X(DailyLimit, "limit", "limit")                                                                  \
  X(OverLimit, "ponad limit — odświeżaj rzadziej albo zmniejsz promień",                           \
    "over the limit — refresh less often or shrink the radius")                                    \
  X(QuotaLeftToday, "Pozostało dziś kredytów:", "Credits left today:")                             \
  X(QuotaFromLastRequest, "(z ostatniego zapytania)", "(as of the last request)")                  \
  X(QuotaUnknown, "Pozostałe kredyty: jeszcze nieznane, pokażą się po pierwszym zapytaniu.",       \
    "Credits left: not known yet, shown after the first request.")                                 \
  X(QuotaExhausted, "Dzienny limit wyczerpany", "Daily limit reached")                             \
  X(QuotaRenewsIn, "odnowienie za ok.", "renews in about")                                         \
  X(QuotaRenewsSoon, "powinien się już odnowić, płytka sprawdzi to przy następnej próbie.",        \
    "it should have renewed by now; the board checks on its next try.")                            \
  X(QuotaRenewsUnknown, "OpenSky nie podał, kiedy się odnowi.", "OpenSky didn't say when it renews.") \
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
  X(SectionAccess, "Dostęp do panelu", "Panel access")                                             \
  X(AccessPasswordSet, "Hasło ustawione · login: admin", "Password set · login: admin")            \
  X(AccessNoPassword,                                                                              \
    "Panel nie ma hasła — każdy w Twojej sieci WiFi może zmienić ustawienia i wgrać firmware.",    \
    "The panel has no password — anyone on your WiFi can change the settings and flash firmware.") \
  X(NewPassword, "Nowe hasło", "New password")                                                     \
  X(RepeatPassword, "Powtórz hasło", "Repeat password")                                            \
  X(PasswordHint,                                                                                  \
    "8–64 znaki, bez polskich liter. Login: admin. Puste pola = bez zmian. Zapomniane hasło: "     \
    "przytrzymaj palec na ekranie podczas włączania płytki.",                                      \
    "8–64 characters, plain ASCII. Login: admin. Empty fields = no change. Forgot it: hold a "     \
    "finger on the screen while the board powers up.")                                             \
  X(ClearPassword, "Usuń hasło", "Remove the password")                                            \
  X(PasswordChanged,                                                                               \
    "Zapisano. Hasło panelu zmienione — przeglądarka zapyta o nie przy następnym kliknięciu.",     \
    "Saved. The panel password changed — your browser will ask for it on the next click.")         \
  X(PasswordCleared, "Zapisano. Panel nie ma już hasła.", "Saved. The panel no longer has a password.") \
  X(PasswordMismatch, "hasło panelu (wpisane hasła się różnią)",                                   \
    "panel password (the two entries differ)")                                                     \
  X(PasswordTooShort, "hasło panelu (za krótkie, min. 8 znaków)",                                  \
    "panel password (too short, at least 8 characters)")                                           \
  X(PasswordTooLong, "hasło panelu (za długie, max. 64 znaki)",                                    \
    "panel password (too long, 64 characters max)")                                                \
  X(PasswordBadChar, "hasło panelu (tylko znaki ASCII, bez polskich liter)",                       \
    "panel password (plain ASCII characters only)")                                                \
  /* Backup */                                                                                     \
  X(BackupIntro,                                                                                   \
    "Wszystkie ustawienia w jednym pliku JSON: na wypadek wymiany płytki, wyczyszczenia pamięci "  \
    "albo eksperymentów, których potem żałujesz.",                                                 \
    "All settings in one JSON file: for a new board, a wiped flash, or experiments you end up "    \
    "regretting.")                                                                                 \
  X(BackupSaveTitle, "Zapisz kopię", "Save a backup")                                              \
  X(BackupIncludeSecrets, "Dołącz dane logowania OpenSky", "Include OpenSky credentials")          \
  X(BackupSecretsHint,                                                                             \
    "Client ID, secret i hasło panelu trafią do pliku zwykłym tekstem. Trzymaj go w bezpiecznym "  \
    "miejscu.",                                                                                    \
    "The client ID, secret and panel password go into the file as plain text. Keep it somewhere " \
    "safe.")                                                                                       \
  X(BackupDownload, "Pobierz plik JSON", "Download JSON file")                                     \
  X(RestoreTitle, "Przywróć z kopii", "Restore from a backup")                                     \
  X(RestoreHint,                                                                                   \
    "Wczytaj plik zapisany wcześniej w tym panelu. Ustawienia, których nie ma w pliku, zostaną "   \
    "bez zmian.",                                                                                  \
    "Load a file saved from this page earlier. Settings missing from the file stay as they are.")  \
  X(RestoreButton, "Przywróć", "Restore")                                                          \
  X(RestoreOk, "Przywrócono ustawienia z kopii.", "Settings restored from the backup.")            \
  X(RestoreWithInvalid, "Przywrócono, ale te pola miały błędne wartości i zostały bez zmian:",     \
    "Restored, but these fields had invalid values and were left unchanged:")                      \
  X(RestoreNotBackup, "Nie przywrócono: to nie jest plik kopii ustawień Sky Trackera.",            \
    "Not restored: this is not a Sky Tracker settings backup.")                                    \
  /* Firmware */                                                                                   \
  X(FirmwareIntro,                                                                                 \
    "Nowa wersja przez WiFi, bez kabla. Ustawienia i zapamiętane samoloty zostają.",               \
    "A new version over WiFi, no cable. Settings and remembered aircraft are kept.")               \
  X(FirmwareInstalled, "Zainstalowana wersja", "Installed version")                                \
  X(FirmwareReleases, "Najnowsze wydanie na GitHubie", "Latest release on GitHub")                 \
  X(FirmwareChoose, "Plik firmware (.bin)", "Firmware file (.bin)")                                \
  X(FirmwareHint,                                                                                  \
    "Wybierz plik cyd-sky-tracker-WERSJA.bin — nie ten z -full w nazwie.",                         \
    "Pick cyd-sky-tracker-VERSION.bin — not the one with -full in its name.")                      \
  X(FirmwareUpdateButton, "Aktualizuj", "Update")                                                  \
  X(FirmwareDontPowerOff, "Nie wyłączaj zasilania podczas aktualizacji (ok. 30 s).",               \
    "Don't power off during the update (about 30 s).")                                             \
  X(FirmwareUploading, "Wysyłanie", "Uploading")                                                   \
  X(FirmwareRestarting, "Płytka uruchamia się ponownie — strona odświeży się sama.",               \
    "The board is restarting — this page reloads by itself.")                                      \
  X(FirmwareLooksLikeFull,                                                                         \
    "To pełny obraz (-full.bin) do instalacji kablem. Wybierz plik bez -full.",                    \
    "That is the full image (-full.bin) for cable installs. Pick the file without -full.")         \
  X(OtaOk, "Zaktualizowano firmware:", "Firmware updated:")                                        \
  X(OtaNoFile, "Nie wybrano pliku.", "No file chosen.")                                            \
  X(OtaFullImage,                                                                                  \
    "Nie zaktualizowano: to pełny obraz (-full.bin) do instalacji kablem. Wybierz plik bez -full.", \
    "Not updated: that is the full image (-full.bin) for cable installs. Pick the file without "   \
    "-full.")                                                                                      \
  X(OtaNotFirmware, "Nie zaktualizowano: to nie jest plik firmware ESP32.",                        \
    "Not updated: this is not an ESP32 firmware file.")                                            \
  X(OtaWrongChip, "Nie zaktualizowano: firmware jest na inny układ niż ESP32.",                    \
    "Not updated: this firmware is for a different chip than the ESP32.")                          \
  X(OtaTooLarge, "Nie zaktualizowano: plik nie mieści się w pamięci płytki.",                      \
    "Not updated: the file does not fit the board's memory.")                                      \
  X(OtaNotSkyTracker, "Nie zaktualizowano: to nie jest firmware Sky Trackera.",                    \
    "Not updated: this is not Sky Tracker firmware.")                                              \
  X(OtaWriteFailed, "Nie zaktualizowano: błąd zapisu do pamięci flash.",                           \
    "Not updated: writing to flash failed.")                                                       \
  X(OtaVerifyFailed, "Nie zaktualizowano: plik jest uszkodzony (suma kontrolna się nie zgadza).",  \
    "Not updated: the file is corrupt (checksum mismatch).")                                       \
  X(OtaAborted, "Nie zaktualizowano: wysyłanie zostało przerwane.",                                \
    "Not updated: the upload was interrupted.")                                                    \
  X(OtaUnauthorized, "Nie zaktualizowano: wymagane hasło panelu.",                                 \
    "Not updated: the panel password is required.")                                                \
  X(OtaChecksumMismatch,                                                                            \
    "Nie zaktualizowano: suma SHA-256 pobranego pliku nie zgadza się z tą z GitHuba.",              \
    "Not updated: the downloaded file's SHA-256 does not match the one on GitHub.")                 \
  X(OtaVersionMismatch, "Nie zaktualizowano: pobrany plik ma inną wersję niż wydanie.",             \
    "Not updated: the downloaded file has a different version than the release.")                   \
  X(OtaDownloadFailed, "Nie zaktualizowano: pobieranie z GitHuba nie powiodło się.",                \
    "Not updated: the download from GitHub failed.")                                                \
  /* Firmware tab: updates from GitHub (release_check) */                                           \
  X(UpdCardTitle, "Aktualizacje z GitHuba", "Updates from GitHub")                                  \
  X(UpdLatest, "Najnowsze wydanie", "Latest release")                                               \
  X(UpdPublished, "opublikowane", "published")                                                      \
  X(UpdChecked, "sprawdzono", "checked")                                                            \
  X(UpdCheckNow, "Sprawdź teraz", "Check now")                                                      \
  X(UpdInstall, "Zainstaluj", "Install")                                                            \
  X(UpdBackToRelease, "Wróć do wydania", "Go back to release")                                      \
  X(UpdWhatsNew, "Co nowego", "What is new")                                                        \
  X(UpdDownloading, "Pobieranie z GitHuba i instalacja — ok. 1 min. Nie wyłączaj zasilania.",       \
    "Downloading from GitHub and installing — about 1 min. Do not power off.")                      \
  X(UpdAutoCheck,                                                                                   \
    "Sprawdzaj nowe wersje na GitHubie raz na dobę (tylko sprawdza — instaluje po potwierdzeniu)",  \
    "Check GitHub for new versions once a day (only checks — installs after you confirm)")          \
  X(UpdShowPrerelease, "Pokazuj wersje deweloperskie", "Show development versions")                 \
  X(UpdLatestPre, "Najnowsza deweloperska", "Latest development version")                           \
  X(UpdPreWarning, "Wersja testowa — może zawierać błędy. Powrót: zainstaluj wydanie powyżej.",     \
    "A test version — it may have bugs. To go back, install the release above.")                    \
  X(UpdInstalledHere, "zainstalowana", "installed")                                                 \
  X(UpdLastInstallFailed, "Ostatnia instalacja nie powiodła się", "The last install failed")        \
  X(UpdInstallRefused, "Nie można zainstalować tej wersji — sprawdź jeszcze raz.",                  \
    "This version cannot be installed — check again.")                                              \
  X(UpdStatusNeverChecked, "Jeszcze nie sprawdzono.", "Not checked yet.")                           \
  X(UpdStatusUpToDate, "Masz najnowszą wersję.", "You have the latest version.")                    \
  X(UpdStatusAvailable, "Dostępna nowsza wersja.", "A newer version is available.")                 \
  X(UpdStatusBackToRelease, "Masz wersję deweloperską nowszą od najnowszego wydania.",              \
    "You have a development version newer than the latest release.")                                \
  X(UpdStatusDevBuild, "Wersja deweloperska — nie da się jej porównać z wydaniem.",                 \
    "A development build — it cannot be compared with the release.")                                \
  X(UpdStatusNoRelease, "Na GitHubie nie ma jeszcze wydania.", "There is no release on GitHub yet.")\
  X(UpdStatusNoAsset, "Wydanie nie ma pliku dla tej płytki.", "The release has no file for this board.")\
  X(UpdStatusNoChecksum, "Wydanie nie ma sumy SHA-256 — wgraj plik ręcznie poniżej.",               \
    "The release has no SHA-256 — upload the file by hand below.")                                  \
  X(UpdStatusCheckFailed,                                                                           \
    "Nie udało się bezpiecznie połączyć z GitHubem — spróbuj później albo wgraj plik ręcznie.",     \
    "Could not connect to GitHub securely — try later or upload the file by hand.")                 \
  X(UpdStatusRateLimited, "GitHub chwilowo ogranicza zapytania — spróbuję później.",                \
    "GitHub is limiting requests for now — will try later.")                                        \
  X(UpdStatusDisabled, "Automatyczne sprawdzanie jest wyłączone.", "Automatic checking is off.")    \
  /* General tab: screen brightness + night mode (spec/03) */                                     \
  X(SectionDisplay, "Ekran", "Display")                                                            \
  X(BrightnessMode, "Jasność", "Brightness")                                                       \
  X(BrightnessAuto, "automatyczna (czujnik światła)", "automatic (light sensor)")                  \
  X(BrightnessFixed, "stała", "fixed")                                                             \
  X(BrightnessMin, "Minimalna (%)", "Minimum (%)")                                                 \
  X(BrightnessMax, "Maksymalna (%)", "Maximum (%)")                                                \
  X(BrightnessAutoHint, "W ciemnym pokoju ekran schodzi do minimum, w jasnym świeci na maksimum.", \
    "In a dark room the screen goes down to the minimum, in a bright one up to the maximum.")      \
  X(BrightnessFixedPct, "Stała jasność (%)", "Fixed brightness (%)")                               \
  X(NightModeLabel, "Tryb nocny", "Night mode")                                                    \
  X(NightOff, "wyłączony", "off")                                                                  \
  X(NightSchedule, "według godzin", "by the clock")                                                \
  X(NightSun, "od zachodu do wschodu słońca", "sunset to sunrise")                                 \
  X(NightFrom, "Noc od", "Night from")                                                             \
  X(NightTo, "do", "until")                                                                        \
  X(NightBrightness, "Jasność w nocy (%)", "Brightness at night (%)")                              \
  X(NightBrightnessHint, "0 % = ekran wygaszony; dotknięcie zapala go na czas wybudzenia.",         \
    "0 % = screen off; a touch lights it for the wake time.")                                      \
  X(NightWake, "Po dotknięciu w nocy świeć / pobieraj dane przez (s)",                             \
    "After a touch at night, stay on / fetch data for (s)")                                        \
  X(NightPause, "Wstrzymaj pobieranie danych w nocy", "Pause data fetching at night")              \
  X(NightPauseHint, "Oszczędza kredyty OpenSky. Dotknięcie ekranu wznawia pobieranie na czas wybudzenia.", \
    "Saves OpenSky credits. A touch on the screen resumes fetching for the wake time.")            \
  X(NightPauseSaving, "zapytań mniej tej nocy", "fewer requests tonight")                          \
  X(LightNow, "Teraz", "Now")                                                                      \
  X(LightSensor, "czujnik", "sensor")                                                              \
  X(LightAverage, "średnio", "average")                                                            \
  X(LightLevel, "jasność", "brightness")                                                           \
  X(LightDay, "dzień", "day")                                                                      \
  X(LightNight, "noc", "night")                                                                    \
  X(LightScreenOff, "noc, ekran wygaszony", "night, screen off")                                   \
  X(LightWoken, "noc, wybudzony dotykiem", "night, woken by a touch")                              \
  X(LightPollingPaused, "pobieranie wstrzymane", "fetching paused")                                \
  X(LightNotSynced, "czas niezsynchronizowany — tryb nocny nieaktywny",                            \
    "clock not synced — night mode inactive")                                                      \
  X(SunToday, "Dziś słońce: wschód", "Sun today: sunrise")                                         \
  X(SunSet, "zachód", "sunset")                                                                    \
  X(SunForLocation, "(dla Twojej lokalizacji)", "(for your location)")                             \
  X(SunPolarDay, "Dziś dzień polarny — nie ma nocy.", "Polar day today — no night.")               \
  X(SunPolarNight, "Dziś noc polarna — noc przez całą dobę.", "Polar night today — night all day.")\
  X(SunNeedsLocation, "Ustaw lokalizację, żeby liczyć wschód i zachód słońca.",                    \
    "Set the location to work out sunrise and sunset.")                                            \
  X(SunNeedsClock, "Wschód i zachód pojawią się po synchronizacji zegara.",                        \
    "Sunrise and sunset appear once the clock is synced.")                                         \
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
  X(Version, "wersja", "version")                                                                    /* Device alerts (device_alerts): the card above the tabs. Each {} is filled */                   /* in by fillTemplate(). */                                                                       X(AlertCardTitle, "Wymaga uwagi", "Needs attention")                                               X(AlertCardIntro,                                                                                    "Wykrzyknik na pasku u góry wyświetlacza (czerwony = błąd, pomarańczowy = ostrzeżenie) "          "oznacza, że urządzenie wykryło problem. Dotknij go na ekranie, żeby zobaczyć krótki opis.",       "The exclamation mark in the bar at the top of the display (red = error, orange = warning) "       "means the device has found a problem. Tap it on the screen for a short description.")           X(AlertGoTo, "Przejdź do:", "Go to:")                                                              X(AlertLoginTitle, "Błąd logowania OpenSky", "OpenSky login failed")                               X(AlertLoginWhy,                                                                                     "OpenSky odrzucił client_id / client_secret (HTTP {}). Urządzenie działa w trybie "                "anonimowym: {} kredytów dziennie zamiast {}.",                                                    "OpenSky rejected the client_id / client_secret (HTTP {}). The device runs anonymously: "          "{} credits a day instead of {}.")                                                               X(AlertLoginAction,                                                                                  "Co zrobić: sprawdź dane na stronie konta OpenSky (Account → API client) i zapisz je "             "ponownie.",                                                                                       "What to do: check them on your OpenSky account page (Account → API client) and save them "        "again.")                                                                                        X(AlertQuotaTitle, "Wyczerpany dzienny limit OpenSky", "OpenSky daily limit reached")              X(AlertQuotaWhy,                                                                                     "OpenSky odpowiada 429: dzisiejsze kredyty się skończyły i lista samolotów się nie odświeża.",     "OpenSky answers 429: today's credits are used up and the aircraft list isn't refreshing.")      X(AlertQuotaWhyIn, "Pula odnowi się za ok. {}.", "The pool renews in about {}.")                   X(AlertQuotaWhySoon, "Pula powinna się już odnowić, płytka sprawdzi to przy następnej próbie.",      "The pool should have renewed by now, the board checks on its next try.")                         X(AlertQuotaAction,                                                                                  "Co zrobić: poczekać na odnowienie. Żeby się nie powtarzało, odpytuj rzadziej albo wstrzymuj "     "pobieranie nocą.",                                                                                "What to do: wait for the renewal. To keep it from happening again, poll less often or pause "     "fetching at night.")                                                                            X(AlertBudgetTitle, "Za częste odpytywanie", "Polling too often")                                  X(AlertBudgetWhy,                                                                                    "Co {} s wychodzi ok. {} kredytów dziennie, a limit to {}. Najkrótszy interwał, który się "        "mieści: {} s.",                                                                                   "Every {} s comes to about {} credits a day, and the limit is {}. The shortest interval that "     "fits: {} s.")                                                                                   X(AlertBudgetAction,                                                                                 "Co zrobić: wydłuż interwał odświeżania, zmniejsz promień albo włącz wstrzymanie pobierania "      "nocą (zakładka Ogólne).",                                                                         "What to do: make the poll interval longer, shrink the radius or turn on pausing at night "        "(General tab).")

enum class WebStr : uint16_t {
#define WEB_I18N_ENUM(id, pl, en) id,
  WEB_I18N_STRINGS(WEB_I18N_ENUM)
#undef WEB_I18N_ENUM
      Count
};
constexpr int kWebStrCount = static_cast<int>(WebStr::Count);

// Text of `id` in `lang` (out-of-range id -> "").
const char *webTr(WebStr id, Language lang);
