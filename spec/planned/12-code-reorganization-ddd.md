# Spec 12 — Reorganizacja kodu według DDD: konteksty, warstwy, reguły zależności

Z przeglądu kodu 2026-10-25, sekcje 2–3.
Status: **etap 7 z 8 zrobiony (v1.11.4)** 2026-11-08, etap 1 wydany w v1.11.0, etap 2 w v1.11.1, etap 3 i 4 w v1.11.2, etap 5 w v1.11.3, etap 6 i 7 w v1.11.4, etap 8 na gałęzi `spec/12-code-reorganization-ddd-stage8`, czeka na sprawdzenie instalacji z GitHuba (build `-dev` tego PR). Jedna specyfikacja dla całej
reorganizacji, wdrażana etapami (§8). Warunki wstępne: spec 07 (testy w grupach) i spec 11
(martwy kod usunięty).

## 1. Problem

- `src/` to 80 płaskich plików. Z nazw nie widać, co jest logiką dziedziny (lot, faza, przelot
  nad domem), co integracją (OpenSky, hexdb.io, GitHub), a co ekranem.
- Zależności idą w złą stronę: `core_link.h` (współbieżność) dołącza `table_view.h` (widok) dla
  `AircraftRow` i `status_bar.h` dla `OpenSkyHealth`; `config_store.h` dołącza `backlight.h`;
  `computeDistanceBearing()` jest w `table_view`, `openSkyCreditCost()` w `config_portal`,
  UTF-8 i `fitToWidth()` w `lcars_theme`.
- 40 plików dzieli się wewnątrz `#ifdef ARDUINO` na „czystą” i „sprzętową” połowę.
- `main.cpp` (824 linie) i `config_portal.cpp` (`handleSave`, ~180 linii) zawierają logikę
  aplikacji — automaty stanów, parsowanie formularza — bez testów.
- `Config` to jeden wielki struct, który dołącza prawie każdy moduł, choć każdy potrzebuje
  kilku pól.

## 2. Cel

Kod zorganizowany wokół **dziedziny** (Domain-Driven Design, w wersji dopasowanej do małego
firmware'u: bez frameworków, bez wirtualnych interfejsów tam, gdzie wystarczy funkcja):

- **Konteksty ograniczone** (bounded contexts) jako katalogi najwyższego poziomu w `src/`.
- W każdym kontekście **warstwy**: `core/` (czysta logika i model), `services/` (przypadki
  użycia, automaty stanów, porty), `adapters/` (ESP32, LovyanGFX, HTTP, NVS). Korzenie
  kompozycji w `src/main/` (§4.8).
- **Reguły zależności** zapisane w tabeli i sprawdzane automatycznie w CI.
- **Wspólny język** (ubiquitous language): te same nazwy w kodzie, CLAUDE.md, panelu i rozmowie.

### Warstwy: nazwy i znaczenie

| Katalog | Co tam leży | Kompiluje się na `native`? |
|---|---|---|
| `<kontekst>/core/` | czysta logika kontekstu: typy, reguły, obliczenia, układy ekranów, teksty | tak, z testami |
| `<kontekst>/services/` | automaty stanów, potoki i przypadki użycia (§6), które łączą kilka części `core` i porty | tak, z testami |
| `<kontekst>/adapters/` | wywołania SDK: ESP32, LovyanGFX, HTTP, NVS, `WebServer` | nie |
| `src/main/` | korzenie kompozycji: `main.cpp` (rdzeń 1), `net_task.cpp` (rdzeń 0) | nie |

Nazwy wybrane przez właściciela 2026-11-01, wprowadza je etap 2. Wcześniejsze `domain/` i
`app/` okazały się mylące:
- `domain` to w DDD reguły biznesowe, a `display/domain/` trzymał ekrany, motyw i fonty;
- słowo `app` znaczyło dwie różne rzeczy: korzenie w `src/app/` i przypadki użycia w
  `<kontekst>/app/`.

`core` mówi tylko „czyste i testowalne”, co pasuje do każdego kontekstu, także `display`.

Zachowanie urządzenia **nie zmienia się**. To refaktoryzacja: każdy etap kończy się zielonymi
testami i tym samym firmware'em (poza etapem 5, który dodaje testy wyciągniętej logiki).

## 3. Słownik pojęć (ubiquitous language)

| Pojęcie (PL) | W kodzie | Znaczenie |
|---|---|---|
| Dom | `Home` (`lat`, `lon`) | miejsce, nad którym urządzenie patrzy w niebo |
| Zasięg | `ScanArea` | prostokąt ±`radius_deg` wokół domu (do etapu 7 `BoundingBox`) |
| Stan samolotu | `AircraftState` | jedna obserwacja: pozycja, wysokość, prędkość, kurs, „na ziemi” (do etapu 7 `Aircraft`) |
| Lot | `TrackedFlight` | samolot w zasięgu z ustaleniami: odległość, kierunek, faza, linia, typ, trasa (do etapu 7 `AircraftRow`) |
| Obraz nieba | `AirspaceSnapshot` | niezmienna lista lotów z jednego pollu + przewidywanie przelotu (do etapu 7 `DataSnapshot`) |
| Faza | `Phase` | start / lądowanie / przelot / brak |
| Przelot nad domem | `CpaPrediction` | kiedy i jak blisko domu samolot przeleci (CPA); pojęcie „przelot” (*overflight*), typ zostaje `CpaPrediction` |
| Dane o samolocie | `AircraftInfo` | linia, typ, rejestracja (hexdb.io) |
| Trasa, lotnisko | `RouteInfo`, `AirportInfo` | skąd–dokąd, kody IATA/ICAO, kraj (hexdb.io, symetrycznie do `AircraftInfo`) |
| Kredyty | `OpenSkyQuota`, `openSkyCreditCost()` | limit zapytań OpenSky; koszt zapytania to funkcja, nie typ |
| Stan źródła | `FeedHealth` | czy dane płyną (do etapu 7 `OpenSkyHealth`) |
| Ostrzeżenie | `AlertId`, `AlertSet` | coś wymaga uwagi właściciela (spec 08); jedno ostrzeżenie to `AlertId`, aktywne razem `AlertSet` |
| Ustawienia | `Settings` | wszystko, co zapisane w NVS (do etapu 7 `Config`, moduł `config_store`) |

Zmiany nazw (dawne nazwy „do etapu 7”) zrobiono w **osobnym etapie** (§8, etap 7), po
przeniesieniu plików — żeby diff przeniesienia był czysto mechaniczny. Kolumna „W kodzie” to
nazwy, które są w kodzie; `CpaPrediction`, `RouteInfo` / `AirportInfo`, `openSkyCreditCost()`
i `AlertId` / `AlertSet` zostały celowo (decyzja Q1 etapu 7).

## 4. Konteksty

Nazwy plików poniżej to nazwy z kodu (etap 8 dopasował ten rozdział do kodu).

### 4.1 `airspace` — Niebo nad domem (**dziedzina główna**)

Odpowiada na pytania „co leci, gdzie, w którą stronę i kiedy będzie nade mną”. Nie wie nic
o OpenSky, hexdb.io ani ekranie.

- `core/`: `aircraft_state` (`AircraftState`, `inferOnGround()`), `home.h`, `tracked_flight`
  (`TrackedFlight`), `flight_phase`, `cpa_predictor` (przelot nad domem), `flight_ordering`
  (`sortRowsForDisplay()`, `splitFeaturedAndRest()`), `now_pick` (`pickNowAircraft()`),
  `feed_health.h`, `airspace_snapshot.h`.
- `services/`: `poll_pipeline` (stany samolotów → obraz nieba), port `EnrichmentPort`.

### 4.2 `enrichment` — Dane o samolotach, trasach i lotniskach (**pomocniczy**)

- `core/`: `aircraft_lookup`, `route_lookup` (parsowanie JSON hexdb.io, `lookup*WithFetcher`:
  RAM → NVS → sieć, bez cache'owania błędów transportu; typy wyników w `shared/flight_info`),
  `aircraft_names`, `place_names`, `lookup_budget`, `lookup_cache.h` (spec 05), `lookup_store`
  (kodowanie rekordów NVS).
- `adapters/`: `hexdb_http` (HTTPS, keep-alive), `hexdb_sources`, `lookup_store_nvs`.
- `EnrichmentPort` składa korzeń kompozycji (`main/net_enrichment`).

### 4.3 `opensky` — Źródło danych OpenSky (**warstwa antykorupcyjna**)

Tłumaczy świat OpenSky (tablice `states` z polami po indeksach, OAuth, nagłówki limitów) na
`AircraftState` z `airspace`. **Nic z formatu OpenSky nie wychodzi poza ten katalog.**

- `core/`: `opensky_client` (`parseStatesResponse()`, `extractAircraftFromStates()`,
  `shouldPollNow()`, `tokenNeedsRefresh()`, `isBeforeDeadline()`), `scan_area` (`ScanArea`,
  `computeScanArea()`, `openSkyCreditCost()`, `bboxAreaSqDeg()`), `opensky_quota`, `opensky_auth`
  (spec 06), `opensky_settings.h`. `openSkyDailyCredits()` i `openSkyDailyRequests()` są w
  `admin/core/config_page`, bo liczą wartości do panelu.
- `adapters/`: `opensky_http` (token + `/states/all`).

### 4.4 `display` — Wyświetlacz i dotyk (**prezentacja**)

- `core/` (czysta logika widoków — układy, teksty, kolory, bez LovyanGFX):
  `screens/` (`table_view`, `featured_panel`, `flight_screen`, `radar_view`, `radar_geometry`,
  `aircraft_summary`, `status_bar`, `service_screen`), `theme/` (`lcars_theme`, `fonts/`),
  `text/` (`utf8`, `fit_to_width`, `font_set`), `screen_nav`, `redraw_policy`, `touch_input`,
  `touch_calibration.h`, `backlight`, `i18n` (teksty ekranu), `display_settings.h`. Wygenerowane
  fonty (`lcars_efont.c`) to czyste dane czytane przez `fonts_test`.
- `services/` (automaty stanów wyciągnięte z `main.cpp`, §6): `ui_flow` (dispatch dotyku,
  stronicowanie tabeli), `update_prompt_flow`, `ota_screen_flow`, `alert_screen_flow`
  (spec 08), `service_screen_flows`, `password_reset_flow`, `screenshot_bridge` (spec 09),
  `ui_perf`.
- `adapters/`: `lgfx/` (`LGFX_CYD.hpp`, `<moduł>_draw`, `ui_draw`, `screenshot_panel`),
  `touch_xpt2046`, `backlight_ldr` (ADC + PWM), `service_flows`.

### 4.5 `admin` — Administracja urządzeniem (**pomocniczy**)

Ustawienia, panel WWW, kopie zapasowe, hasło, ostrzeżenia, aktualizacje.

- `core/`: `settings` (agregat `Settings` + `sanitize`; NVS w `settings_nvs`), `settings_form`
  (§6), `config_backup`, `web_auth`, `device_alerts` (spec 08), `firmware/` (`ota_update`,
  `release_check`, `version`), `config_page` + `config_page_tabs` + `config_page_writer.h`
  (renderer HTML), `web_i18n`, `screenshot` (BMP i `?screen=` dla trasy WWW).
- `adapters/`: `web_server` + `web_server_firmware` + `web_server_internal.h` (trasy
  `WebServer`), `settings_nvs`, `ota_flash` (`Update`), `github_http`, `github_ca.h`.
- `assets/`: `style.css`, `app.js`, `favicon.svg`, `restart.js`, `check_pending.js`;
  osadzane przy budowaniu przez `tools/embed_assets.py` (pre-skrypt jak `pio_version.py`)
  do wygenerowanego `admin/core/web_assets.h`.

### 4.6 `shared` — Wspólne jądro (shared kernel)

Małe, stabilne typy i funkcje używane przez wiele kontekstów: `geo` (odległość, namiar,
kierunek świata), `language.h` (`Language`), `flight_info` (`AircraftInfo`, `RouteInfo`,
`AirportInfo`), `clock` (formatowanie daty/czasu), `sun_times`.
Zmiana w `shared/` wymaga uzasadnienia w opisie commita — to kontrakt dla wszystkich.

### 4.7 `platform` — Infrastruktura urządzenia

Nie jest dziedziną; łączy konteksty na ESP32: `core_link` (`NetState` jako **opublikowany
język** między rdzeniami), `perf_stats`, `wifi_manager`, `time_sync` (adapter NTP),
`net_config.h`.

### 4.8 `main/` w katalogu głównym — korzenie kompozycji

`main.cpp` (rdzeń 1: tworzy LGFX, składa przepływy z `display/services`, pętla) i `net_task.cpp`
(rdzeń 0: składa OpenSky + enrichment + admin, pętla), a przy nim `net_perf`, `net_enrichment`,
`net_rollback` i `alert_sources` (`deviceAlertInputs()`, `panelLiveInputs()`: dane z kilku
kontekstów). **Tylko tu** wolno znać wszystkie konteksty naraz. `main.cpp` < 250 linii, bez
własnych automatów stanów.

## 5. Reguły zależności

`#include "kontekst/warstwa/plik.h"` (ścieżka od `src/`, flaga `-I src`) — zależność widać
w każdym include'zie.

| Kto (wiersz) może dołączać | shared | airspace.core | enrichment.core | opensky.core | display.core | admin.core | `*/services` | platform | `*/adapters` | Arduino / LGFX / WiFi |
|---|---|---|---|---|---|---|---|---|---|---|
| `shared` | ✔ | | | | | | | | | |
| `airspace/core` | ✔ | ✔ | | | | | | | | |
| `enrichment/core` | ✔ | | ✔ | | | | | | | |
| `opensky/core` | ✔ | ✔ | | ✔ | | | | | | |
| `display/core` | ✔ | ✔ | ✔ | | ✔ | ✔¹ | | | | |
| `admin/core` | ✔ | ✔² | | ✔² | ✔² | ✔ | | | | |
| `airspace/services` | ✔ | ✔ | ✔ | | | | własny | | | |
| `display/services` | ✔ | ✔ | ✔ | | ✔ | ✔¹ | własny | ✔ | | |
| `admin/services` | ✔ | ✔ | | ✔ | ✔² | ✔ | własny | | | |
| `platform` | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | | ✔ | | tylko `#ifdef ARDUINO` |
| `*/adapters` | ✔ | swój kontekst | swój | swój | swój | swój | swój | ✔ | swój | ✔ |
| `main/` (korzenie) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |

¹ tylko typy do odczytu: `AlertSet`, ustawienia ekranu (§7). ² tylko typy wartości do złożenia
ustawień i ostrzeżeń (`NightMode`, `PhaseThresholds`, `OpenSkyQuota`…).

Zasady wynikające z tabeli:

- **Nic w `core/` ani `services/` nie dołącza Arduino, LovyanGFX, WiFi, HTTPClient, Preferences.**
  Znika `#ifdef ARDUINO` z plików czystych; zostaje tylko w `platform/` (np. globalne obiekty
  `core_link`).
- `display` nigdy nie widzi `opensky` (ekran nie wie, skąd są dane); widzi `FeedHealth`.
- `opensky` i `enrichment` nie znają się nawzajem; łączy je `airspace/services/poll_pipeline`
  przez porty.
- Konteksty nie dołączają cudzych `adapters/`.

**Strażnik:** `tools/check_layers.py` — czyta `#include "…"` z `src/` i `test/`, sprawdza tabelę
(zapisaną w skrypcie jako dane), wypisuje naruszenia `plik:linia: display/core → opensky`.
W CI w `firmware.yml` przed testami; błąd = czerwony build (`--strict`).

## 6. Logika wyciągnięta z adapterów (warstwa `services/`)

Każdy z poniższych staje się czystym typem z testami; adapter tylko podaje wejścia (dotyk,
czas, stan) i wykonuje wyjścia (rysuj, wyślij komendę, zapisz).

| Dziś | Nowe | Wejścia → wyjścia |
|---|---|---|
| `handleTap()` + `clampPage()` w `main.cpp` | `display/services/ui_flow` (`TablePager`, `routeTap()`) | punkt dotyku, ekran, liczba stron → `JumpTo` / `PagePrev` / `PageNext` / `OpenAlerts` / nic |
| `maybeShowUpdatePrompt()`, `serviceUpdatePrompt()` (7 zmiennych `static`) | `display/services/update_prompt_flow` | `PromptGate`, dotyk, czas → pokaż / timer / `InstallTag` / `SkipTag` / zamknij |
| `serviceOtaScreen()` | `display/services/ota_screen_flow` | `OtaProgress`, czas → rysuj ekran / postęp / wynik / wróć do ekranów po 5 s |
| `offerWebPasswordReset()` | `display/services/password_reset_flow` | dotyk, czas → przytrzymaj / potwierdź / wyczyść hasło |
| `shot::` (spec 09) | `display/services/screenshot_bridge` | żądania rdzenia 0 → pasy do odczytu |
| `perfLoopTick()` | `display/services/ui_perf` | czasy przejść pętli → `UiPerfShare` |
| `handleSave()` w `config_portal` | `admin/core/settings_form` | `Settings` + `FormArgs` (mapa nazwa→wartość) → `{Settings, rejected[], passwordChange}` |

`settings_form` usuwa też 7 kopii bloku „`hasArg` → waliduj → przypisz albo dopisz do
`rejected`”: tabela pól `{nazwa, etykieta, parser, wskaźnik na pole}`.

## 7. Ustawienia: agregat + ustawienia per kontekst

`Settings` (do etapu 7 `Config`) zostaje jednym agregatem w `admin/core` — tylko on jest zapisywany,
sanityzowany i odtwarzany z kopii. Inne konteksty **nie dołączają `Settings`**; dostają małe,
własne obiekty wartości, wycinane w korzeniach kompozycji:

| Kontekst | Obiekt | Pola |
|---|---|---|
| `airspace` | `Home`, `PhaseThresholds` | lat/lon, `radius_deg`, `configured`, `near_airport_km`, `climb_rate_threshold_mps` |
| `opensky` | `OpenSkyCredentials`, `PollSettings` | client id/secret, `interval_s` |
| `display` | `DisplaySettings`, `BacklightSettings`, `AutoCycleSettings` | język, ostatni ekran, jasność, noc (z `night_pause_polling`), auto-cycle |
| `admin` | `UpdateSettings`, `WebSettings` | sprawdzanie aktualizacji, pre-release, pominięta wersja (`skipped_tag`), hasło, język panelu |

Poprawione w etapie 6 (decyzja właściciela 2026-11-07): `radius_deg` jest w `Home`, nie w
`PollSettings` — czytają go też pierścienie radaru, różnica `NetState` i ostrzeżenia, a ekran nie
widzi `opensky` (§5). Trzy pola, których tabela nie nazywała, trafiły do `Home::configured`,
`BacklightSettings::night_pause_polling` i `UpdateSettings::skipped_tag`.

`Settings` składa się z tych obiektów (pola `settings.backlight.night_mode` zamiast
`cfg.night_mode`). Klucze NVS i format kopii zapasowej **bez zmian** (testy `config_backup`
i `settings` pilnują zgodności wstecz). `NetState` niesie `shared_ptr<const Settings>`,
a ekran czyta z niego tylko swoje obiekty.

## 8. Etapy (każdy = osobny PR, testy zielone, firmware działa na płytce)

1. **Szkielet.** Katalogi, `git mv` plików bez dzielenia, `-I src`, include'y z pełną ścieżką
   (skrypt `tools/rewrite_includes.py`, zostaje do etapu 8 — etapy 2–4 znów przenoszą pliki).
   `build_src_filter` dla `native`: `+<*> -<app/> -<*/adapters/> -<platform/*_esp32.cpp>`.
   `check_layers.py` w trybie raportu. Diff: tylko przeniesienia i include'y. Warstwy nazywały
   się wtedy `domain/` i `app/`, korzenie `src/app/`.
2. **Nazwy warstw.** `git mv` katalogów, bez zmian w plikach poza include'ami:
   - `<kontekst>/domain/` → `<kontekst>/core/`, `<kontekst>/app/` → `<kontekst>/services/`,
     `src/app/` → `src/main/`;
   - `admin/app/`: `config_page` i `web_i18n` (czyste) → `admin/core/`, `config_portal` (trasy
     `WebServer`) → `admin/adapters/`; jego czyste części (`openSkyCreditCost`, `handleSave`)
     wychodzą w etapach 3 i 5.

   Ten sam mechanizm co w etapie 1: nowa tabela `MOVES` w `rewrite_includes.py` (ok. 260
   include'ów w `src/` i `test/`). Do tego:
   - tabela warstw w `check_layers.py` i przykłady w jego testach oraz w scenariuszach
     `.feature`;
   - `build_src_filter`: `+<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp>
     +<admin/adapters/config_portal.cpp>` — ostatni wzorzec dodaje z powrotem jeden plik:
     `config_portal.cpp` ma nad `#ifdef ARDUINO` czyste, testowane funkcje (`bboxAreaSqDeg`,
     `openSkyCreditCost`, `parseOpenSkyCredentialsJson`, `isValidFloatString`,
     `isValidUnsignedIntString`), z których korzystają `config_page` i `device_alerts`; wyjątek
     znika, gdy etapy 3 i 4 je wyniosą (decyzja właściciela 2026-11-01);
   - `tools/subset_efont.py` (katalog fontów), komentarze z odwołaniami do ścieżek;
   - CLAUDE.md („Code conventions”), `docs/architecture.md`, `test/README.md`, spec 14.

   Kryteria jak w etapie 1:
   - liczba `RUN_TEST` bez zmian;
   - firmware ± 1 KB;
   - `check_layers.py` zgłasza **51** naruszeń (poprawione 2026-11-01; pierwotnie „te same 55,
     tylko pod nowymi nazwami” — to się nie dało: przeniesienie `config_page` / `web_i18n` do
     `admin/core/` i `config_portal` do `admin/adapters/` zmienia, co jest dozwolone).
     55 − 9 + 5: znika 4 × `admin/domain -> admin/app` (teraz `admin/core -> admin/core`) i
     5 include'ów SDK / `platform` w `config_portal.cpp` (adapter może); dochodzi
     `config_page.cpp -> admin/adapters` (`config_portal.h`), `config_portal_test.cpp ->
     admin/adapters` i 3 include'y innych kontekstów w `config_portal.cpp` (adapter widzi tylko
     swój kontekst). W CI test narzędzi pilnuje limitu ≤ 51 (nie dokładnej listy), dokładną
     listę sprawdza się ręcznie na koniec etapu;
   - `rewrite_includes.py --check` = 0.
3. **Przeniesienie źle położonych typów.** `AircraftRow` z `table_view.h`, `OpenSkyHealth` ze
   `status_bar.h`, `computeDistanceBearing`, `compassPoint`, `openSkyCreditCost`, UTF-8 /
   `fitToWidth`, `inferOnGround`, `pickNowAircraft`, `sortRowsForDisplay` do kontekstów z §4.
   Testy idą za kodem (np. testy odległości z `table_view_test.cpp` do `shared/geo_test.cpp`).
   Decyzje (plan etapu 3, właściciel 2026-11-01): z typami idą ich towarzysze (`buildEnrichedRecords`,
   `annotateDistances`, `classifyPhases`, `sortRowsByDistance`, `FeaturedSplit`, `NowPick`,
   `DistanceBearing`, `TextMeasure`, `kTaxiSpeedMps`, `kOverheadRadiusKm`), bo woła je
   `poll_pipeline`; `Aircraft` idzie z `inferOnGround` do `airspace/core/aircraft_state`; zbiór
   znaków fontów (`isFontSafeText` i in.) też do `display/core/text/font_set`, a `DataSnapshot` i
   `Language` zostają; 2 nowe naruszenia `tracked_flight.h -> enrichment/core` przyjęte, decyzja
   przed etapem 8; wyjątek `config_portal.cpp` w filtrze `native` zostaje do etapu 4; kryterium
   `check_layers.py`: **47** (51 − 6 + 2), limit w teście narzędzi obniżony do 47.
4. **Podział czyste / sprzętowe.** Każdy plik z `#ifdef ARDUINO` → `core|services/x.cpp` +
   adapter (pierwotnie `adapters/x_esp32.cpp` albo `adapters/lgfx/x_draw.cpp`). Po tym etapie
   `grep -rl "ifdef ARDUINO" src --include=*.cpp` zwraca tylko `platform/` i `adapters/`.
   Decyzje (plan etapu 4, właściciel 2026-11-01 / 02):
   - adaptery mają od razu nazwy z §4, nie `x_esp32`: `display/adapters/lgfx/<moduł>_draw`,
     `touch_xpt2046`, `backlight_ldr`, `hexdb_sources` (aircraft + route), `lookup_store_nvs`,
     `opensky_http`, `ota_flash`, `github_http`, `settings_nvs`, `alert_sources`
     (`deviceAlertInputs()`, nazwa spoza §4); `hexdb_http` bez zmian;
   - deklaracje z typami prostymi zostają w nagłówkach `core/` bez `#ifdef` (port), adapter je
     definiuje; adaptery widzą tylko swój kontekst (§5 bez zmian) → 5 nowych naruszeń, razem 14;
   - czyste funkcje `config_portal` → `admin/core/settings_form`, potem `config_portal` →
     `admin/adapters/web_server` jako osobny krok (`MOVES_STAGE_4` w `rewrite_includes.py`;
     funkcje `configPortal*()` zmieniają nazwy w etapie 7); formatowanie daty / czasu z
     `time_sync` → `shared/clock`; `include/LGFX_CYD.hpp` → `display/adapters/lgfx/`,
     `include/touch_calibration.h` → `display/core/`;
   - krok 11b: firmware bez wyjątków C++ i tablic unwind (`-fno-exceptions -fno-unwind-tables
     -fno-asynchronous-unwind-tables`), kryterium ± 1 KB liczone od 1 345 520 B (start etapu
     zbudowany z tymi flagami) zamiast 1 477 568 B.
5. **Warstwa `services/`** z §6 + testy (to jedyny etap, który dodaje testy, a nie tylko je przenosi).
   `main.cpp` < 250 linii, `config_portal` bez `handleSave` logiki.
   Decyzje (plan etapu 5, właściciel 2026-11-05 — domyślne odpowiedzi na Q1–Q7):
   - `ui_flow` obejmuje poza `TablePager` / `routeTap()` też księgowość zmian ekranu (zapis,
     strona, licznik auto-cycle), `MinuteWatch`, tik odliczania, bramkę auto-cycle, zdrowie w
     nagłówku i wykrywanie zbocza dotyku;
   - nowe adaptery spoza §4.4: `display/adapters/lgfx/ui_draw`, `display/adapters/service_flows`
     (sterowniki przepływów: dotyk → krok → rysowanie), `display/adapters/lgfx/screenshot_panel`;
   - `alert_screen_flow` to cienka nakładka; `AlertScreen` / `alertScreenStep()` zostają w
     `display/core/screens/service_screen`;
   - `applySettingsForm()` zmienia `Config` w miejscu i zwraca `{rejected, password, message}`
     (bez drugiej kopii `Config` na stosie rdzenia 0), zamiast `{Settings, …}` z §6;
   - budżet rozmiaru: `firmware.bin` ≤ +4 096 B, RAM statyczny ≤ +256 B (§9 zwalnia etap 5 z ± 1 KB);
   - `net_task.cpp` (421 linii, §10 pkt 3 chce < 300) — nie w tym etapie;
   - 19 testów mostu zrzutu idzie z kodem do `test_display`, 9 testów BMP / pasów / `?screen=`
     zostaje w `test_admin`.
   Gdy `main.cpp` stanął na 327 liniach, właściciel wybrał sterownik `ServiceScreens` (czysty
   `ServiceScreenFlows` + adapter) i agregat `UiFlow` w `ui_flow` (wynik niżej).
6. **Ustawienia per kontekst** (§7) + zasoby WWW jako pliki (`admin/assets/`, `embed_assets.py`).
   Decyzje (plan etapu 6, właściciel 2026-11-07 — domyślne odpowiedzi na Q1–Q7):
   - Q1: `radius_deg` w `Home` z `airspace` („dom i jak daleko wokół niego patrzymy”),
     `PollSettings` ma tylko `interval_s` (tabela §7 poprawiona);
   - Q2: `night_pause_polling` → `BacklightSettings` (część okna nocy, decyduje rdzeń 1),
     `home_configured` → `Home::configured`, `update_skipped_tag` → `UpdateSettings::skipped_tag`
     (stan urządzenia, dalej poza kopią zapasową);
   - Q3: zasoby wstawiane w stronę (jedna odpowiedź HTML, bajt w bajt jak przedtem), nie osobne
     trasy `/style.css` / `/app.js` (§11: API WWW bez zmian, bez dodatkowych rund Digest);
   - Q4: wszystkie pięć plików: `style.css`, `app.js`, `favicon.svg` (§4.5) oraz `restart.js`,
     `check_pending.js` — w C++ nie zostaje żaden JS;
   - Q5: wygenerowany `admin/core/web_assets.h` jest w repozytorium; skrypt `pre:` w `cyd` i
     `native` odświeża go przy kompilacji, test narzędzi w CI sprawdza, że jest aktualny;
   - Q6: `Config` → `Settings` (i `config_store` → `settings`) dopiero w etapie 7;
   - Q7: podział `net_task.cpp` (§10 pkt 3) i `config_page.cpp` (897 linii) — osobny PR przed
     etapem 8, nie w tym etapie;
   - w trakcie (decyzja orkiestratora): sparsowany plik poświadczeń w `admin/core/settings_form.h`
     zmienił nazwę z `OpenSkyCredentials` na `CredentialsFile { ok, creds }` — tę nazwę ma teraz
     obiekt ustawień `opensky`.
7. **Nazwy ze słownika** (§3), po jednej zmianie nazwy na commit (`AircraftRow` →
   `TrackedFlight` itd.), automatycznie (`sed` + kompilacja), bez innych zmian w tym commicie.
   Decyzje (plan etapu 7, właściciel 2026-11-07 — domyślne odpowiedzi na Q1–Q8):
   - Q1: `CpaPrediction`, `RouteInfo` / `AirportInfo`, `openSkyCreditCost()` i `AlertId` /
     `AlertSet` zostają; §3 poprawiony do nazw z kodu;
   - Q2: `Config` → `Settings` obejmuje typ, 8 funkcji ustawień, `NetState::cfg` / `cfgSeq` →
     `settings` / `settingsSeq` i plik `config_store` → `settings`; zmienne i parametry `cfg`,
     `ConfigPageModel`, moduły `config_page` / `config_backup` zostają;
   - Q3: `LookupPorts` → `EnrichmentPort` (nazwa z §4.1) — tak;
   - Q4: tylko zmiany nazw, bez przenoszenia `ScanArea` do `scan_area.h` ani `AirspaceSnapshot`
     do `airspace/core/airspace_snapshot.h` (to etap 8 albo PR przed nim);
   - Q5: z nazw plików §4 tylko `config_store` → `settings`; reszta (`cpa_predictor`,
     `poll_pipeline`, `config_backup`, `screen_nav`, `touch_input`, `backlight`,
     `opensky_client`) zostaje, etap 8 dopasuje §4 do kodu;
   - Q6: jeden wpis słownika na commit (typ razem z funkcjami nazwanymi od niego), `Config` w
     czterech commitach (typ, funkcje, pola `NetState`, plik);
   - Q7: jedno sprawdzenie na płytce na obrazie końcowym (USB + jedna OTA);
   - Q8: `tools/rename_symbols.py` zostaje do etapu 8 (z testami w CI);
   - zamiast `sed`: `tools/rename_symbols.py` (lekser C/C++, napisy i cytaty nietknięte), bo
     `\bX\b` zmieniłby klucze NVS, tekst logów i cytowane nagłówki dokumentacji.
8. **Egzekwowanie.** `check_layers.py` blokuje CI; `docs/architecture.md` (konteksty, tabela
   z §5, słownik z §3) i sekcja „Code conventions” w CLAUDE.md zastąpiona krótkim odsyłaczem.

### Wynik etapu 1 (2026-10-27, wydany w v1.11.0)

Opis z czasu etapu 1: warstwy nazywały się `domain/` i `app/`, korzenie `src/app/` (etap 2
zmienia nazwy), a numery etapów są już po nowej numeracji.


- **Katalogi:** 94 pliki przeniesione `git mv` (94 rename'y w gicie, bez podziału i zmian
  nazw) do `src/<kontekst>/<warstwa>/` + `shared/`, `platform/`, `app/`; tabela przeniesień to
  `MOVES` w `tools/rewrite_includes.py`. Plik trafił tam, gdzie będzie jego czysta połowa;
  połowy `#ifdef ARDUINO` czekają na etap 4. Od razu w `adapters/` / `app/` tylko pliki w
  całości sprzętowe (`hexdb_http`, `github_ca.h`, `net_task`, `main.cpp`).
- **Include'y z pełną ścieżką** w `src/` i `test/` (`-I src` w `cyd` i `native`); diff `src/`:
  94 pliki, 228+/228− (same linie include + 4 komentarze ze ścieżką fontów).
  `rewrite_includes.py --check` = 0, `LayoutTest` pilnuje, że tak zostanie.
- **`native`:** `build_src_filter = +<*> -<app/> -<*/adapters/> -<platform/*_esp32.cpp>`;
  wszystkie 6 grup zielone, 876 przypadków jak przed etapem, `RUN_TEST(` = 874 (≥ 874).
- **Firmware:** `firmware.bin` 1 476 928 → 1 477 184 B (+256 B, kolejność linkowania), Flash
  used +268 B, RAM +16 B — w granicy ± 1 KB; te same biblioteki w grafie zależności.
- **`tools/check_layers.py`** w CI (krok „Layer check (report only)” przed testami native):
  **55 naruszeń** na koniec etapu 1 — punkt odniesienia dla etapów 2–8 (m.in. 10 ×
  `display/domain -> display/adapters` przez `LGFX_CYD.hpp`, 6 × `admin/domain -> admin/app`
  z `device_alerts`, 19 × SDK w `domain/` / `admin/app` pod `#ifdef ARDUINO`, domain / app →
  `platform` przez `time_sync.h`, `core_link.h`, `net_config.h`). Brak naruszeń testów,
  skróconych include'ów i plików poza kontekstem.
- **`tools/release_plan.py`:** status `**etap N z M` z N < M zostawia specyfikację w `planned/`
  mimo scalonej gałęzi `spec/12-…`.
- **Odstępstwa (decyzje właściciela 2026-10-27):** fonty w `display/domain/theme/fonts/`, nie
  w `adapters/` (§4.4 poprawiony); `screenshot` w `display/app/`, jego test na razie w
  `test_admin`; `rewrite_includes.py` zostaje do etapu 8; `include/LGFX_CYD.hpp` i
  `touch_calibration.h` zostają w `include/` do etapu 4; przypisy ¹ ² z §5 `check_layers.py`
  traktuje jako dozwolone (nie da się ich sprawdzić po include'ach); brakujące wiersze §5
  uzupełnione: `enrichment/app` (własny + `airspace` domain / app), `opensky/app` (własny),
  testy (bez `adapters/`, korzeni i SDK).
- **`@manual`:** płytka po wgraniu przez USB (2026-10-27): trzy ekrany z lotem na żywo
  (`/screenshot.bmp?screen=0..2`), wersja gałęzi w stopce panelu, `/perf.json` po 2 min:
  przerwa pętli UI maks. 74 ms, 0 wolnych przejść, sterta 146 KB (największy blok 59 KB), bez
  restartów. Dotyk i jedna aktualizacja OTA — do sprawdzenia przez właściciela (build `-dev`).
  Rozmiar firmware zmierzony lokalnie (wyżej).

### Wynik etapu 2 (2026-11-01, wydany w v1.11.1)

- **Katalogi:** 80 plików przeniesionych `git mv` (80 rename'ów w gicie): `<kontekst>/domain/` →
  `core/`, `<kontekst>/app/` → `services/`, `src/app/` → `src/main/`; `admin/app/` rozdzielony
  (`config_page`, `web_i18n` → `admin/core/`, `config_portal` → `admin/adapters/`). Bez zmian
  w kodzie: include'y w 103 plikach `src/` / `test/` i ścieżki w komentarzach
  (`fonts_test.cpp`, `platformio.ini`, `subset_efont.py`, `pio_version.py`).
- **Narzędzia:** `rewrite_includes.py` ma obie tabele (`MOVES_STAGE_1`, `MOVES_STAGE_2` w
  `STAGES`, `final_path()` prowadzi ścieżkę z etapu 0 przez wszystkie etapy);
  `check_layers.py` zna warstwy `core` / `services` / `adapters` i korzeń `main`, stare nazwy
  to „poza kontekstem i warstwą”; `subset_efont.py` pisze do `display/core/theme/fonts/`.
  `rewrite_includes.py --check` = 0.
- **`native`:** filtr `+<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp>
  +<admin/adapters/config_portal.cpp>`; wszystkie 6 grup zielone, 876 przypadków (265 / 46 /
  340 / 80 / 67 / 78, 3 pominięte) — tyle samo co przed etapem; `RUN_TEST(` = 876.
- **Firmware:** `firmware.bin` 1 477 184 → 1 477 200 B (+16 B, kolejność linkowania), Flash
  used +8 B, RAM bez zmian (62 044 B) — w granicy ± 1 KB; ten sam graf zależności.
- **`tools/check_layers.py`:** **51 naruszeń** (55 − 9 + 5, dokładnie jak w §8 pkt 2), m.in.
  10 × `display/core -> display/adapters` (`LGFX_CYD.hpp`), 4 × `admin/core -> admin/adapters`,
  15 × SDK w `core/` pod `#ifdef ARDUINO`, 3 × `config_portal.cpp` → inne konteksty,
  1 × `test -> admin/adapters` (`config_portal_test.cpp`). Test narzędzi
  `LayoutTest.test_layer_findings_do_not_exceed_the_stage_2_limit` pilnuje limitu ≤ 51.
- **Odstępstwa (decyzje właściciela 2026-11-01):** `config_portal` w `admin/adapters/` z wyjątkiem
  w filtrze `native` do etapu 4 (zamiast dzielenia go teraz albo zostawienia w `services/`);
  kryterium 55 → 51 (wyżej); tabela przeniesień etapu 1 zostaje obok tabeli etapu 2.
- **`@manual` (płytka, 2026-11-01):** wgrane przez USB (`2b29f48`): bez paniki i restartów w logu szeregowym przez 3 min; trzy ekrany z lotem na żywo (`/screenshot.bmp?screen=0..2`); stopka panelu `v1.11.0-6-g2b29f48-dirty`; `/perf.json` po 3,5 min: przerwa pętli UI maks. 93 ms, 0 wolnych przejść, sterta 147 KB (największy blok 59 KB). Po etapie 1 było: 74 ms, 0, 146 / 59 KB; dłuższa przerwa (93 ms) mieści się poniżej progu wolnego przejścia, wolnych przejść dalej 0. OTA przez WiFi (`POST /update`, trasa z przeniesionego `config_portal`): 1,48 MB w 16 s, HTTP 200, nowy obraz `c9b49d5` oznaczony jako ważny po 30 s. `pio run -e cyd-ota -t upload` przy panelu bez hasła kończy się 400 — `curl --digest` wysyła pierwsze żądanie bez treści; błąd sprzed etapu 2, osobna poprawka. Dotyk — do sprawdzenia przez właściciela.

### Wynik etapu 3 (2026-11-01, wydany w v1.11.2)

- **Co przeszło gdzie** (symbole, nie pliki — nowe pliki pisane ręcznie, bez `git mv` i bez
  tabeli w `rewrite_includes.py`; każdy symbol znikał ze starego nagłówka w tym samym commicie):
  - `airspace/core/aircraft_state` — `Aircraft`, `kTaxiSpeedMps`, `inferOnGround()` (z
    `opensky_client`, który go dołącza i tłumaczy na niego tablicę OpenSky; komentarze pól nie
    cytują już indeksów OpenSky);
  - `airspace/core/feed_health.h` — `OpenSkyHealth` (ze `status_bar.h`);
  - `airspace/core/tracked_flight` — `AircraftRow`, `buildEnrichedRecords()`,
    `annotateDistances()`, `classifyPhases()` (z `table_view`);
  - `airspace/core/flight_ordering` — `sortRowsForDisplay()`, `sortRowsByDistance()`,
    `FeaturedSplit`, `splitFeaturedAndRest()` (z `table_view`);
  - `airspace/core/now_pick` — `kOverheadRadiusKm`, `NowPick`, `pickNowAircraft()` (z `flight_screen`);
  - `shared/geo` — `DistanceBearing`, `computeDistanceBearing()` (z `table_view`), `compassPoint()`
    (z `lcars_theme`);
  - `opensky/core/scan_area` — `bboxAreaSqDeg()`, `openSkyCreditCost()` (z `config_portal`);
  - `display/core/text/` — `utf8` (`utf8Length()`, `utf8IsCharBoundary()`, wcześniej anonimowy
    pomocnik), `fit_to_width` (`TextMeasure`, `fitToWidth()`), `font_set` (`isFontSafeText()`,
    `isFontCodepoint()`, `fontHasGlyph()`) — z `lcars_theme`.

  `core_link.h` dołącza `tracked_flight.h` i `feed_health.h`, żadnego nagłówka z
  `display/core/screens/`; `poll_pipeline` nie dołącza już ekranów ani `opensky_client.h`.
- **Testy:** 49 przeniesionych do grupy kontekstu nowego miejsca kodu (nazwy bez zmian), 1 nowy
  (`test_char_boundary_on_ascii_and_continuation_bytes` — `utf8IsCharBoundary()` stała się
  publiczna). Wszystkie 6 grup zielone: admin 258 (3 pominięte) / airspace 71 / display 312 /
  enrichment 80 / opensky 73 / platform 83 = **877** (przed etapem 876); `RUN_TEST(` = 877.
  Nowe testy narzędzi w `LayoutTest` (symbole w nowym miejscu, testy za kodem, 6 usuniętych
  naruszeń, `core_link.h` bez ekranów); `check_bdd.py` 0 problemów.
- **`tools/check_layers.py`: 47 naruszeń** (51 − 6 + 2, dokładnie jak w planie): znika
  `poll_pipeline.cpp -> display/core` × 2, `poll_pipeline.h -> opensky/core`, `table_view.h ->
  opensky/core`, `config_page.cpp` i `device_alerts.cpp -> admin/adapters`; dochodzi
  `tracked_flight.h -> enrichment/core` × 2 (`AircraftRow` niesie `AircraftInfo`, `RouteInfo`,
  `AirportInfo` — §4.1 i §5 są tu sprzeczne). Limit w
  `LayoutTest.test_layer_findings_do_not_exceed_the_stage_limit` (nowa nazwa) = 47.
  `rewrite_includes.py --check` = 0, `check_comments.py --strict` czysto.
- **Firmware:** `firmware.bin` 1 477 200 → 1 477 568 B (**+368 B**), Flash used +364 B, RAM bez
  zmian (62 044 B). Po samych przeniesieniach było +1 200 B (ponad ± 1 KB): `buildEnrichedRecords`
  i `splitFeaturedAndRest` w osobnych jednostkach kompilacji sprawiły, że GCC wstawiał inline
  kopie `AircraftRow` (dużo `std::string`), a każdy nowy `.cpp` dokłada ok. 160 B tablic wyjątków.
  Odzyskane (decyzja właściciela) przez zadeklarowanie konstruktorów kopiujących / przenoszących,
  przypisań i destruktora `AircraftRow` w `tracked_flight.h` i `= default` w `tracked_flight.cpp`
  — ta sama semantyka (docs/history.md „Tooling and docs”).
- **Decyzje właściciela (2026-11-01):** Q1 — 2 naruszenia `tracked_flight.h -> enrichment/core`
  przyjęte, rozstrzygnięcie przed etapem 8; Q2 — z §4 poza listą etapu przeniesiony tylko
  `font_set`, `DataSnapshot` i `Language` zostają; Q3 — wyjątek `config_portal.cpp` w filtrze
  `native` do etapu 4 (zostały w nim `parseOpenSkyCredentialsJson()` i dwa walidatory, 11
  przypadków w `config_portal_test.cpp`); Q4 — limit w teście narzędzi 51 → 47 (scenariusz
  etapu 2 o limicie dostosowany); Q5 — `sortRowsByDistance()` (bez wywołania w produkcji)
  przeniesiona bez zmian, usunięcie osobnym PR.
- **Odstępstwa od §8 pkt 3:** przeniesione też funkcje towarzyszące (wyżej) i `Aircraft`;
  `font_set` z §4.4 dodany do etapu (Q2).
- **`@manual` (płytka, 2026-11-01):** po każdym kroku wgrane przez USB (COM3), log szeregowy
  2–3 min, `/perf.json`, trzy zrzuty ekranów. Obraz końcowy `666b69b`: bez paniki, jedyny restart
  to start; ekrany 0–2 poprawne (tabela, TERAZ z odliczaniem, RADAR); panel nadal pokazuje
  koszt w kredytach; przerwa pętli UI maks. 95 ms, 0 wolnych przejść; sterta 142 KB, największy
  blok 59 KB (po etapie 2: 147 / 59 KB; w kolejnych krokach etapu sterta wahała się 142–146 KB,
  największy blok 53–70 KB między uruchomieniami). Po drodze sprawdzone też: kropka zdrowia w
  nagłówku, ikony lądowania / startu i linie lotnicze / trasy w wierszach, kolejność tabeli,
  wybór TERAZ (zbliżający się LOT348). Odczyt po 14 min pracy na `666b69b`: sterta 140 440 B,
  największy blok 59 380 B, cache hexdb.io 15 / 15 / 11 wpisów (przy odczycie po 3 min 5 / 5 / 6) —
  wolna sterta idzie za zapełnianiem ograniczonych cache'y w RAM, statyczny RAM bez zmian
  (62 044 B), więc to nie regresja. Po przeglądzie `baa5ae4` poprawił include'y (`table_view.h`
  bierze `OpenSkyHealth` z `feed_health.h`, `radar_view.cpp` dołącza `status_bar.h`) — bez
  zmiany zachowania i rozmiaru. Dotyk — do sprawdzenia przez właściciela.

### Wynik etapu 4 (2026-11-02, wydany w v1.11.2)

- **Co przeszło gdzie** (połowy `#ifdef ARDUINO` z `core/` do adapterów, pisane ręcznie; część
  czysta bez zmian; jeden krok = jeden commit, płytka sprawdzana po każdym):
  - `display/adapters/lgfx/` — `LGFX_CYD.hpp` (z `include/`), `status_bar_draw`,
    `service_screen_draw`, `radar_view_draw`, `flight_screen_draw` (z globalnym sprite'em
    odliczania), `table_view_draw`, `featured_panel_draw`, `aircraft_summary_draw`,
    `lcars_theme_draw` (fonty efont, `LCARS_FONT_*`, `gfxTextMeasure()`, ikony, strzałki,
    pigułki, pasek zakładek); dzielone od wołających (bez przejściowych `display/core ->
    display/adapters`);
  - `display/adapters/touch_xpt2046` (`touchInputRead()`), `display/adapters/backlight_ldr`
    (LDR, PWM, okno nocne); `touch_calibration.h` → `display/core/`;
  - `enrichment/adapters/hexdb_sources` (pobieranie samolotów, tras i lotnisk + `lookup*()`),
    `lookup_store_nvs`; `opensky/adapters/opensky_http` (token, `/states/all`, 401 / 429,
    harmonogram; `extractAircraftFromStates()` publiczna w `opensky_client.h`);
  - `admin/adapters/ota_flash`, `github_http`, `settings_nvs`, `alert_sources`; `OtaObserver`
    do `ota_update.h`; czyste funkcje `config_portal` → `admin/core/settings_form`; potem
    `config_portal` → `web_server`; `formatDate()` / `formatTime()` / `minuteOf()` →
    `shared/clock` (`time_sync` ma tylko NTP); `main/net_task` bez osłony `#ifdef` całego pliku.

  Stałe wspólne dla obu połówek w nagłówkach `core/` jako `constexpr` z prefiksem modułu
  (`kTableRowStep`, `kFlightCountdownTop` …), tablica kolumn tabeli zdefiniowana raz
  (`kTableColumns`). `grep -rl "ifdef ARDUINO" src --include=*.cpp` → tylko
  `enrichment/adapters/hexdb_http.cpp` i `platform/` (`core_link`, `time_sync`, `wifi_manager`).
  Filtr `native`: `+<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp>`, bez wyjątku.
- **Testy:** 1 nowy (`test_extract_aircraft_from_states_matches_parse_states_response`),
  `config_portal_test.cpp` → `settings_form_test.cpp`, `time_sync_test.cpp` → `clock_test.cpp`.
  Wszystkie 6 grup zielone: admin 258 (3 pominięte) / airspace 71 / display 312 / enrichment 80 /
  opensky 74 / platform 83 = **878**; `RUN_TEST(` = 878. Testy narzędzi zielone,
  `check_bdd.py` 0 problemów (191 scenariuszy), `rewrite_includes.py --check` = 0,
  `check_comments.py --strict` czysto.
- **`tools/check_layers.py`: 14 naruszeń** (47 − 38 + 5, dokładnie jak w planie), limit w teście
  narzędzi 47 → 14. Zostają: `web_server.cpp` × 4, `tracked_flight.h -> enrichment/core` × 2,
  `poll_pipeline.h -> platform`, `place_names.h` / `opensky_quota.h -> display/core` (`Language`)
  i 5 nowych (adapter widzi tylko swój kontekst): `service_screen_draw.cpp`, `backlight_ldr.cpp`,
  `opensky_http.cpp -> admin/core`, `alert_sources.cpp -> display/core` i `-> opensky/core`.
- **Firmware:** po krokach (bajty względem poprzedniego): 2 ±0 · 3 +256 · 4 +48 · 5 +48 · 6 +160 ·
  7 −16 · 8 ±0 · 9 +48 · 10 +64 · 11 +1 248 · 11b −133 408 (flagi) · 12 +464 · 13 −16 · 14 +80 ·
  15 ±0 · 16 ±0. Do kroku 11 etap kosztował +1 856 B (ponad ± 1 KB): każdy nowy `.cpp` dokładał
  ok. 50 B `.eh_frame`, a podział `opensky_client` +1 248 B — linker ESP32 zostawia całe
  `.eh_frame` każdego obiektu, więc szablony ArduinoJson wspólne dla dwóch plików mają
  zdublowane rekordy unwind. Decyzja właściciela (krok 11b): `env:cyd` bez wyjątków i tablic
  unwind, −133 408 B; nowa baza 1 345 520 B (start etapu z tymi flagami). Koniec: **1 346 544 B
  (+1 024 B — dokładnie na granicy ± 1 KB)**, Flash used 1 339 961, RAM bez zmian (62 044 B).
  `AircraftInfo()` zadeklarowany w `aircraft_lookup.h`, `= default` w `.cpp` (inaczej GCC
  wstawiał go inline, +112 B). Dźwignia w rezerwie: `~FirmwareMarkerScanner()` poza linią.
- **Odstępstwa od §8 pkt 4:** nazwy adapterów z §4 zamiast `x_esp32`; zmiana nazwy
  `config_portal` → `web_server` i `shared/clock` już w tym etapie; flagi kompilacji (krok 11b) i
  nowa baza rozmiaru (wyżej, decyzje właściciela).
- **`@manual` (płytka, 2026-11-02):** po każdym kroku wgrane (USB COM3, od kroku 12 też OTA), log
  szeregowy 2–17 min, `/perf.json`, zrzuty ekranów. Obraz końcowy `c32297d`: bez paniki przez
  5 min, jedyny restart programowy to restart po OTA; ekrany 0–2 poprawne; zegar i data w
  nagłówku (22:46 → 22:47, `shared/clock`); karta Firmware „sprawdzono 02.11.2026 22:46”;
  „Sprawdź teraz”: HTTP 200, weryfikowany TLS przy 144 KB wolnej sterty; formularz: zapis bez
  zmian zachowuje wartości, szerokość „abc” odrzucona i zachowana, uszkodzony plik poświadczeń
  odrzucony (poświadczenia zachowane); `/light.json` noc 10 %; OTA przez WiFi 1,35 MB w 17 s →
  app1, oznaczony jako ważny po 30 s; przerwa pętli UI maks. 102 ms, 0 wolnych przejść, sterta
  148 / 66 KB. Po drodze: dotyk sprawdzony przez właściciela (zakładki, krawędzie, stronicowanie
  tabeli, „!”) po krokach 2 i 8; po kroku 10 cache NVS (23 trafienia po restarcie); po kroku 12
  OTA przez WiFi i „Sprawdź teraz”; po kroku 13 zapis ustawień przetrwał restart.
  **Raz (krok 8, `efe4b11`) panika rdzenia 0** w timerze DNS lwIP (`dns_tmr` → `dns_send` →
  `udp_sendto`, LoadProhibited na pustym pcb UDP); płytka sama wstała. Krok 8 zmienił tylko odczyt
  dotyku na rdzeniu 1 (kod wynikowy identyczny), więc nie przypisana etapowi — jedno wystąpienie
  na ok. 15 uruchomień etapów 3–4; zalecana osobna poprawka (`/fix`). Po kroku 14 raz odczyt
  czasu pracy 57 s bez paniki w logu — niewyjaśniony, nie powtórzył się (najpewniej start po
  wgraniu przez USB).

### Wynik etapu 5 (2026-11-05, wydany w v1.11.3)

- **§6 → moduły** (wszystkie czyste, bez `#ifdef ARDUINO`, krok → struktura odpowiedzi; testy
  w większości pisane najpierw — testy `ServiceScreens` / `UiFlow` i 6 testów funkcji
  pomocniczych powstały razem z kodem):

  | §6 „dziś” | Teraz | Testy |
  |---|---|---|
  | `handleTap()` + `clampPage()`, zmiany ekranu, tiki, auto-cycle, zdrowie, `wasTouchPressed` | `display/services/ui_flow`: `routeTap()`, `tablePageCount()` + `TablePager`, `ScreenChangeTracker`, `MinuteWatch`, `CountdownTick`, `shouldAutoCycleNow()`, `shownHealthStep()`, `TouchEdge`, `UiView` (`makeUiView()`, `tapContextOf()`, `netRows()`, `netCpaAt()`, `autoCycleInputAt()`), agregat `UiFlow` | `ui_flow_test.cpp` |
  | `maybeShowUpdatePrompt()`, `serviceUpdatePrompt()` | `update_prompt_flow` (`UpdatePromptFlow`, `updatePromptGate()`) | `update_prompt_flow_test.cpp` |
  | `serviceOtaScreen()` | `ota_screen_flow` (`OtaScreenFlow`, `holdsBacklight()`) | `ota_screen_flow_test.cpp` |
  | `openAlertMessage()` / `serviceAlertScreen()` | `alert_screen_flow` (`AlertScreenFlow`) | `alert_screen_flow_test.cpp` |
  | (spoza §6) kolejność ekranów serwisowych, komenda czekająca na kolejkę | `service_screen_flows` (`ServiceScreenFlows`, `ServiceOutcome`) | `service_screen_flows_test.cpp` |
  | `offerWebPasswordReset()` | `password_reset_flow` (`PasswordResetFlow`) | `password_reset_flow_test.cpp` |
  | `shot::` | `screenshot_bridge` (wydzielony z `screenshot.h`: `ScreenshotBridge`, `ScreenshotReturn`, `screenshotPrepareTarget()`, `screenshotUiPass()`) | `screenshot_bridge_test.cpp` (19 przeniesionych + nowe) |
  | `perfLoopTick()` | `ui_perf` (`UiPerfMeter`) | `ui_perf_test.cpp` |
  | `handleSave()` | `admin/core/settings_form` (`FormArgs`, `applySettingsForm()`, `passwordProblem()`, `rejectedFieldsText()`); `handleSave()` ma 22 linie | `settings_form_test.cpp` |

  Połówki sprzętowe: `display/adapters/service_flows` (`runOtaScreen()`, `runUpdatePrompt()`,
  `runAlertScreen()`, `runPasswordReset()`, `ServiceScreens`), `lgfx/ui_draw` (`applyRedraw()`,
  `drawSetupPrompt()`), `lgfx/screenshot_panel` (instancje mostu i powrotu, funkcje dla
  `ScreenshotSource`, `screenshotPanelPass()`, dema). Do tego `statusBarZonesNow()` (`status_bar`),
  `touchLogTap()` (`touch_xpt2046`), `otaFlashRunningPendingVerify()` (`ota_flash`).
- **`main.cpp`: 909 → 249 linii**, jedyna zwykła zmienna `static` to `netSeen`; `handleSave()`
  bez logiki formularza. Pilnują tego testy narzędzi (`LayoutTest`:
  `test_main_cpp_is_under_250_lines`, `test_main_cpp_keeps_no_flow_state`,
  `test_stage_5_flows_left_main_cpp`, `test_web_server_save_route_has_no_form_logic`,
  `test_stage_5_services_have_their_tests`, `test_stage_5_moved_tests_follow_their_code`).
  `web_server.cpp` 834 → 660 linii; `net_task.cpp` 421 — §10 pkt 3 dla niego czeka (Q6).
- **Testy:** wszystkie 6 grup zielone: admin 251 (3 pominięte; −19 przeniesionych, +12 nowych),
  airspace 71, display 408 (+19 przeniesionych, +77 nowych), enrichment 80, opensky 74, platform
  83 = **967** (było 878); `RUN_TEST(` = 967. `check_bdd.py` 0 problemów (242 scenariusze),
  `check_comments.py --strict` czysto, `rewrite_includes.py --check` = 0, testy narzędzi zielone
  (`test_check_layers.py`: 71). `tools/check_layers.py`: **te same 14 naruszeń** co przed etapem.
  Tagi `@test` w `spec/done/09-concurrency-robustness.feature` (7) i
  `spec/done/10-web-panel-security.feature` (3) wskazują teraz na
  `test_display/screenshot_bridge_test.cpp` (treść scenariuszy bez zmian).
- **Firmware:** po krokach (bajty względem poprzedniego): 2 −544 · 3 +32 · 4 +128 · 5 +320 ·
  6 +320 · 7 +240 · 8 +336 · 9 +272 · 10 +336 · 11 −160 · `ServiceScreens` + `UiFlow` +928.
  Koniec: **1 348 752 B (+2 208 B wobec 1 346 544, budżet +4 096)**, Flash used 1 342 169, RAM
  62 036 B (−8). Tabela pól formularza z funkcjami zamiast lambd: pod gnu++11 konwersja lambdy na
  wskaźnik nie jest `constexpr`, a tabela inicjowana przy starcie kosztowała +160 B RAM.
- **Odstępstwa od planu:** `main.cpp` po krokach 2–12 miał 327 linii — potrzebny był sterownik
  `ServiceScreens` i agregat `UiFlow` (decyzja właściciela, docs/history.md); `UiView` w
  `ui_flow` (czysty, testowalny), nie w adapterze; instancja `ScreenshotReturn` w
  `screenshot_panel`, nie w `main.cpp`; `runPasswordReset()` woła callback czyszczący hasło
  przed ekranem „hasło usunięte” (stara kolejność), zamiast zwracać odpowiedź na końcu; wersja
  zainstalowana podawana do `runOtaScreen()` jako parametr (bez nowego naruszenia warstw);
  poprawka testu narzędzi (`explicit` przed konstruktorem to nie druga deklaracja klasy).
  Drobne różnice bez widocznego skutku: dotyk czytany przy starcie także bez hasła; strefy
  nagłówka i liczba stron liczone przy każdym dotknięciu; CPA dla auto-cycle liczone w każdym
  przejściu; jeden `nowMs` na całe przejście zrzutu.
- **`@manual` (płytka, 2026-11-05):** obraz `02eeb6a` wgrany przez USB (COM3).
  `/screenshot.bmp?screen=0..4`: LOTY, TERAZ, RADAR, demo pytania o aktualizację i ekranu
  ostrzeżeń poprawne, potem ekran wraca na LOTY (pokazany przed zrzutem). `/perf.json` po 77 s:
  przerwa pętli UI maks. 103 ms, p99 20 ms, 0 wolnych przejść, 11 429 przejść; wolny stos UI
  5 244 B (v1.11.2: 4 684), net 9 692 B; sterta 144 484 B, największy blok 55 284 B.
  **Zostaje dla właściciela** (końcowe sprawdzenie na płytce): dotyk (zakładki, pasy tabeli,
  „!” → ZAMKNIJ / 15 s), auto-cycle i ostatni ekran po restarcie, OTA z dobrym i złym plikiem,
  reset hasła przy starcie (NIE), formularz („abc” / „x” odrzucone w tej kolejności, zmiana
  przetrwa restart). Na wersji `-dev` pytanie o aktualizację na żywo się nie pokaże
  (`installedIsDev`) — dowodem są testy natywne i demo `screen=3`.

### Wynik etapu 6 (2026-11-07, wydany w v1.11.4)

- **§7 → obiekty.** `Config` nie ma już ani jednego płaskiego pola: to agregat dziewięciu
  obiektów wartości, każdy w nagłówku swojego kontekstu (bez `.cpp`, wartości domyślne te same
  co wcześniej):

  | Pole `Config` | Obiekt | Nagłówek |
  |---|---|---|
  | `home` | `Home {lat, lon, radius_deg, configured}` | `airspace/core/home.h` (nowy) |
  | `phase` | `PhaseThresholds {near_airport_km, climb_rate_threshold_mps}` | `airspace/core/flight_phase.h` |
  | `opensky` | `OpenSkyCredentials {client_id, client_secret}` | `opensky/core/opensky_settings.h` (nowy) |
  | `poll` | `PollSettings {interval_s}` | jw. |
  | `display` | `DisplaySettings {language, last_screen}` | `display/core/display_settings.h` (nowy) |
  | `auto_cycle` | `AutoCycleSettings {enabled, interval_s}` | jw. |
  | `backlight` | `BacklightSettings` (jasność, noc, `night_pause_polling`) | `display/core/backlight.h` |
  | `update` | `UpdateSettings {check_enabled, show_prerelease, skipped_tag}` | `admin/core/config_store.h` |
  | `web` | `WebSettings {language, password}` | `admin/core/config_store.h` |

  `sanitizeConfig()`, klamry, `defaultConfig()`, zapis / odczyt NVS i kopia zapasowa zostały w
  `admin` (agregat pilnuje niezmienników); zmieniły się tylko ścieżki pól. Funkcje poza `admin/`
  dostają tylko swoje obiekty: `fetchAircraftStates(creds, home)`, `openSkyClientPoll(now, creds,
  home, poll)`, `openSkyAuthStatus(creds)`, `openSkyMsUntilNextPoll(now, poll)`,
  `backlightHandleTouch()` / `backlightStatus(const BacklightSettings &)`, `backlightLoop(tft,
  backlight, home, …)`, `PipelineParams {Home home; PhaseThresholds phase; …}`,
  `releaseCheckBegin(const UpdateSettings &)`, `nightMinutesToday()` / `pausedMinutesPerDay(const
  BacklightSettings &, …)`. `github_http` trzyma `UpdateSettings` zamiast całej kopii `Config` w
  `.bss`. Ekran czyta z `NetState` tylko `home`, `display`, `auto_cycle`, `backlight` i
  `update.skipped_tag`. `Config` nazywa się tak do etapu 7.
- **Klucze NVS i kopia zapasowa bez zmian:** test narzędzi przypina 28 kluczy i ich typy w
  `settings_nvs.cpp`; złoty JSON kopii (z sekretami i bez) i odtworzenie kopii sprzed etapu 6
  (25 ustawień) w `config_backup_test.cpp`; wszystkie 28 wartości domyślnych w
  `config_store_test.cpp`.
- **`check_layers.py`: 14 → 12** — odpadły `opensky_http.cpp -> admin/core` i
  `backlight_ldr.cpp -> admin/core` (oba `config_store.h`), pozostałe 12 jak przed etapem;
  limit w teście narzędzi 12. `config_store.h` dołączają tylko `admin/`, korzenie i
  `core_link.h` (§10 pkt 4 czytany tak: `NetState` niesie agregat).
- **Zasoby panelu jako pliki (§4.5):** `src/admin/assets/` — `style.css`, `app.js`,
  `restart.js`, `check_pending.js`, `favicon.svg` (= `site/favicon.svg`, test pilnuje). Generator
  `tools/embed_assets.py` (skrypt `pre:` w `cyd` i `native`, też z wiersza poleceń, `--check`)
  tworzy `src/admin/core/web_assets.h` (w repozytorium, zapis tylko przy różnicy): usuwa
  komentarze w osobnych liniach, przycina i skleja linie; błąd z `plik:linia`, gdy sklejenie
  złączyłoby dwa tokeny albo gdy komentarz stoi za kodem. `config_page.cpp` nie ma już literałów
  CSS / JS / SVG: 1 053 → 897 linii. Pięć stron z `SCREENSHOT_OUT` jest **bajt w bajt** takich
  samych jak przed etapem, PNG z `tools/screenshots.py` też; `web_i18n` bez zmian.
- **Testy:** wszystkie 6 grup zielone: admin **257** (3 pominięte), airspace 71, display 408,
  enrichment 80, opensky 74, platform **84** = **974** (było 967); `RUN_TEST(` = 974.
  `check_bdd.py` 0 problemów (268 scenariuszy), `check_comments.py --strict` czysto,
  `rewrite_includes.py --check` = 0, `embed_assets.py --check` = 0; testy narzędzi zielone
  (`test_check_layers.py` 79, nowy `test_embed_assets.py` 17, w CI w `firmware.yml`).
- **Firmware** (czyste kompilacje, ta sama długość napisu wersji): **1 348 224 B (−512 wobec
  1 348 736 na starcie etapu, budżet ± 1 024)**, Flash used 1 341 641, **RAM 61 908 B (−128)** —
  `github_http` bez kopii `Config`. Po krokach: 2 −48 (RAM +8: `Home` pakuje `configured` za
  trzema `float`) · 3 +16 · 4 −240 · 5 −256 (RAM −136) · 6 ±0 · 7 +16 · 8 ±0.
- **Odstępstwa od planu:** wszystkie pięć plików zasobów powstało w kroku 7 (krok 8 tylko
  przełączył `config_page.cpp` na JS z plików); `CredentialsFile` zamiast `OpenSkyCredentials` w
  `settings_form.h` (zmiana nazwy w teście narzędzi `STAGE_4_HOMES` i w przykładzie scenariusza
  etapu 4; asercje te same); `main.cpp` przełącza język ekranu po `e.languageChanged` z
  `diffNetState()` (to samo porównanie, bez nowej linii — plik ma 249). Test „teksty skryptu z
  `web_i18n`” czyta „żaden tekst `T` nie występuje w JS” jako: nie ma go w literałach napisów ani
  jako prozy (`day` / `night` to też nazwy właściwości w skrypcie).
- **`@manual` (płytka, 2026-11-07):** obraz `25225c4` (`v1.11.3-9-g25225c4`) wgrany przez WiFi
  zwykłym `curl -F firmware=@… /update` (200, 1 348 444 B w 13,8 s); po 105 s działa, bez
  wycofania. `/backup.json` przed i po: te same 25 kluczy, różni się tylko `firmware`.
  `/perf.json` po 105 s: przerwa pętli UI maks. 96 ms, p99 20 ms, 0 wolnych przejść; wolny stos
  UI 5 244 B, net 9 888 B (etap 5: 9 692); sterta 148 208 B, największy blok 65 524 B.
  `/light.json`: noc, 10 %, polling włączony, czas zsynchronizowany. `GET /` 200, 19 919 B.
  Ekran i panel wyglądają tak samo, więc zrzutów nie odświeżano.
  **Znalezione po drodze (nie poprawione w tym etapie):** `pio run -e cyd-ota -t upload` z pustym
  `$CYD_WEB_PASSWORD` kończy się HTTP 400 bez wysłania bajtu — `curl --digest` sonduje pustym
  POST-em, a trasa wysyłania odpowiada na niego 400. Przy pierwszej próbie płytka raz się
  zrestartowała (ten sam obraz, przyczyna nieznana, bez logu szeregowego).
  **Zostaje dla właściciela:** dotyk, zmiana zasięgu → pierścienie RADARU, język ekranu PL ↔ EN,
  auto-cycle, ostatni ekran po restarcie, „Sprawdź teraz” (TLS, blok ≥ 50 KB), panel w
  przeglądarce (zakładki, kredyty na żywo, linia „Teraz”, pasek postępu OTA, przeładowanie po
  restarcie), zapis → restart → ta sama kopia zapasowa.

### Wynik etapu 7 (2026-11-08, wydany w v1.11.4)

- **Zmiany nazw (11 commitów, jeden wpis słownika na commit, każdy `python
  tools/rename_symbols.py KLUCZ` + ręczne poprawki komentarzy):**

  | Commit | Było → jest |
  |---|---|
  | A `403f965` | `BoundingBox` → `ScanArea`, `computeBoundingBox()` → `computeScanArea()` |
  | B `4e308ae` | `OpenSkyHealth` → `FeedHealth` |
  | C `0387f2c` | `DataSnapshot` → `AirspaceSnapshot`, `buildDataSnapshot()` → `buildAirspaceSnapshot()` |
  | D `8d5b5d2` | `LookupPorts` → `EnrichmentPort` |
  | E `0d0302e` | `AircraftRow` → `TrackedFlight` |
  | F `1f46326` | `Aircraft` → `AircraftState` |
  | G `89c24b8` | `configPortal*()` → `webServer*()` (8 funkcji) |
  | H1 `ce28849` | `Config` → `Settings` |
  | H2 `544db21` | `defaultConfig()`, `sanitizeConfig()`, `loadConfig()`, `saveConfig()`, `reloadConfig()`, `configToBackupJson()`, `restoreConfigFromBackupJson()`, `makeConfig()` → `…Settings()` / `settingsToBackupJson()` / `restoreSettingsFromBackupJson()` / `makeSettings()` |
  | H3 `9ac5429` | `NetState::cfg`, `cfgSeq` → `settings`, `settingsSeq` (według kompilatora) |
  | H4 `1218606` | pliki `admin/core/config_store.{h,cpp}` → `settings.{h,cpp}` (`MOVES_STAGE_7`), `config_store_test.cpp` → `settings_test.cpp`, `run_settings_tests()` |

  Przed nimi `b78832d` — narzędzie `tools/rename_symbols.py` (tabela `RENAMES_STAGE_7`, rodzaje
  `compound` / `word` / `prefix`, `--check`, `--report`, `--list`) z testami
  `tools/test_rename_symbols.py` (w CI); po nich `fe5aed3` — `check_comments.py` zgłasza dawną
  nazwę złożoną, która wraca do kodu albo komentarza (`RENAMED_SYMBOLS`, „X was renamed Y in
  spec 12 stage 7”). Słowa `Aircraft` i `Config` narzędzie zmieniało tylko w kodzie; 24 i 18
  trafień w komentarzach przejrzano ręcznie: odwołania do typu zmienione, nagłówek
  `docs/data-sources.md "Aircraft identification"`, treść `"Aircraft not found."` z hexdb.io i
  proza zostały. `wifi_manager.cpp` (WiFiManager `setConfigPortalBlocking()` …) bez zmian.
- **Kryteria scenariuszy:** 10 scenariuszy `@test` zielonych (narzędzie, układ plików,
  `NetState`, funkcje serwera WWW, strażnik w `check_comments`, klucze NVS i kopia zapasowa).
  `@manual` „Etap 7 nie zmienia kodu wynikowego ani liczby testów” — **spełniony**: po każdym
  commicie A–H4, zbudowanym z `FW_VERSION=v0.0.0-stage7`, sekcje z `size -A` są takie same jak
  przed etapem, Flash used 1 341 641, RAM 61 908 B; `firmware.bin` 1 348 224 B, w A–H3 różni się
  od poprzedniego tylko 64–65 bajtami skrótów (SHA-256 ELF w opisie aplikacji, suma kontrolna,
  SHA-256 obrazu); H4 (zmiana nazwy plików) — ten sam rozmiar i sekcje, inna kolejność
  linkowania. Testy: 974 przypadki (971 zielonych, 3 pominięte bez `SCREENSHOT_OUT`), `RUN_TEST(`
  974, jak przed etapem; `check_layers.py` te same 12 naruszeń; `check_bdd.py` 0 problemów (282
  scenariusze); `check_comments.py --strict`, `rewrite_includes.py --check`,
  `embed_assets.py --check` czyste; wszystkie testy narzędzi zielone; `main.cpp` 249 linii.
  `@manual` „Płytka z firmware'em etapu 7…” — **spełniony poza zapisem formularza** (2026-11-08): obraz `fe5aed3`
  (1 348 240 B — dłuższy napis wersji z `git describe`) wgrany przez USB (COM3); 3 min logu bez
  paniki, WDT i restartu poza startem, logowanie OpenSky (konto), pierwszy poll 8 samolotów,
  hexdb.io 200. `/backup.json`: 25 kluczy równych kopii sprzed wgrania poza `firmware` (sekrety
  też porównane). `GET /` — te same znaczniki, różnią się wartości na żywo i wersja.
  `/perf.json` po 85 s: przerwa pętli UI maks. 86 ms, 0 wolnych przejść, sterta 147 480 B,
  największy blok 65 524 B, wolny stos UI 5 244 B / net 9 792 B. OTA przez WiFi (`curl -F
  firmware=@… /update`): 200, 1 348 460 B w 15,2 s, nowy obraz działa i nie został wycofany
  (perf 92 ms / 0 wolnych, blok 59 380 B). Ekrany 0–2 po OTA: LOTY, TERAZ, RADAR z samolotami,
  licznik i zegar jak przedtem. Zrzutów nie odświeżano (nic widocznego się nie zmieniło).
  `/light.json`: tryb i jasność zgodne z porą dnia (przed: wieczór, noc 10 %; po: dzień, 100 %),
  wschód / zachód — patrz znany problem niżej. Niesprawdzone z planu, zostaje dla właściciela:
  zapis formularza bez zmian → ta sama kopia (na razie porównanie kopii przed / po wgraniu
  i złoty test kopii).
- **Odstępstwa od planu:** narzędzie zamiast `sed` (§8 pkt 7 mówił `sed`); §3 poprawiony do nazw
  z kodu (Q1) zamiast zmieniać `CpaPrediction`, `RouteInfo`, `AirportInfo`; `LookupPorts` z §4.1
  zmieniony razem ze słownikiem (Q3). Firmware ze stałym `FW_VERSION` ma ten sam rozmiar także po
  zmianie nazwy plików (plan dopuszczał ± 1 KB).
- **Znany problem (nie z etapu 7, kod słońca i czasu bez zmian):** zaraz po starcie
  `/light.json` pokazuje czasem wschód / zachód w UTC (05:44 / 14:57 po restarcie OTA, też
  05:42 / 14:58 w zrzucie „przed” etapu 6) zamiast czasu lokalnego (06:44 / 15:57 po starcie z
  USB). `refreshSun()` zapamiętuje czasy na cały dzień, więc okno nocy jest przesunięte o 1 h do
  następnego dnia. Do osobnego `/fix`. **Poprawione w v1.11.4 (fix/sun-times-utc-after-boot).**
- **Zostaje dla właściciela:** dotyk (zakładki, strony tabeli, „!”), auto-cycle, język ekranu
  PL ↔ EN.

### Wynik etapu 8 (2026-11-08, na gałęzi, czeka na instalację z GitHuba)

- **`check_layers.py`: 12 → 0**, wszystkie usunięte w kodzie, tabela §5 bez nowego ptaszka i wyjątku:
  `Language` → `shared/language.h`; `AircraftInfo`, `RouteInfo`, `AirportInfo` →
  `shared/flight_info` (Q1); `AirspaceSnapshot` → `airspace/core/airspace_snapshot.h`;
  `kUpdatePromptTimeoutMs` → `service_screen.h`; `screenshot` → `admin/core/`;
  `alert_sources.cpp` → `main/`; `panelLiveInputs()` zamiast dwóch obcych include'ów w
  `web_server.cpp`; `otaVersionLine()` poza `web_server.cpp`. Poza listą naruszeń: `ScanArea` →
  `scan_area` (Q5). `firmware.yml` uruchamia `check_layers.py --strict` przed testami native.
- **Podział (§10 pkt 3):** `net_task.cpp` 419 → 283 linie (`net_perf`, `net_enrichment`,
  `net_rollback`), `config_page.cpp` 897 → 355 (`config_page_tabs`, `config_page_writer.h`),
  `web_server.cpp` 659 → 525 (`web_server_firmware.cpp`, `web_server_internal.h`), `main.cpp` 249.
  Największy plik w `src/` ma 525 linii. Pięć stron panelu (`SCREENSHOT_OUT`) `cmp` identycznych.
- **Testy:** 981 przypadków (978 zielonych, 3 pominięte bez `SCREENSHOT_OUT`; 260 / 71 / 412 /
  80 / 74 / 84), `RUN_TEST(` 981: 978 sprzed etapu + 3 nowe testy strzałki w `ota_update_test.cpp`. `check_comments.py --strict`,
  `embed_assets.py --check` czyste, `check_bdd.py` 0 problemów (278 scenariuszy).
- **Firmware** (`FW_VERSION=v0.0.0-stage8`): `firmware.bin` 1 348 256 → 1 348 672 B (**+416 B**,
  budżet +4 096 B) na `730e577`, `.dram0.bss` −8 B, `.dram0.data` 0 (budżet ± 64 B), RAM statyczny
  61 908 B. Obraz na płytkę (`730e577`, wersja z `git describe`): 1 348 688 B.
- **Odstępstwa od planu:** tekst wersji po uploadzie jest też w `model.messageDetail`, więc strona
  zachowuje `→`, a `->` ma tylko linia `[ota]` na porcie szeregowym (Q4 bez zmiany strony);
  `test_config_page_is_split_by_tab` poprawiony, bo `config_page_test.cpp` dołącza `web_assets.h` od
  etapu 6 (scenariusz mówi o firmware); przy usuwaniu narzędzi z `.feature` zeszły 24 linie
  tagów (plan: 25), 20 scenariuszy usuniętych. Wzmianki w środku zdań starszych scenariuszy zostały.
- **`@manual` „Etap 8 nie zmienia liczby testów ani zachowania…”** — spełniony (wyżej).
- **`@manual` „Płytka z firmware'em etapu 8 działa jak przedtem”** — częściowo (2026-11-08), obraz
  `v1.11.4-22-g2ff29a8-dirty` przez USB COM3 (`-dirty` od niezwiązanej zmiany `.vscode`),
  `firmware.bin` 1 348 656 B (obraz sprzed poprawki strzałki, patrz niżej):
  - 3 min logu szeregowego: bez paniki, jedyny restart to włączenie zasilania. `/backup.json` różni
    się tylko polem `firmware`; `/light.json` identyczny (noc, 10 %).
  - Ekrany 0–2 i pytanie o aktualizację (`screen=3`): ten sam układ co przed wgraniem, inne dane na żywo.
  - `/perf.json` po 181 s: przerwa pętli UI maks. 93 ms, 0 wolnych przejść, sterta 144 992 B,
    największy blok 69 620 B, wolny stos net 9 568 B (punkt odniesienia 9 388 B).
  - OTA przez WiFi (`curl -F firmware=@… /update`): 200 w 14,6 s, restart `SW_CPU_RESET`, „running
    from app1, pending verify (rollback armed)”, „marked valid after 30 s, rollback cancelled”.
    Linia `[ota]` `->`; strzałka na stronie była w tym obrazie uszkodzona (niżej). Po OTA: stos net 9 484 B, sterta 147 220 B,
    największy blok 59 380 B.
  - „Sprawdź teraz”: `[update] check: http=200 latest=v1.11.4`, zweryfikowane TLS, 4071 ms.
  - **Poprawka z przeglądu (`83d721a`):** obrazy od `96739ad` do `2ff29a8` pokazywały na stronie po
    uploadzie podwójnie zakodowaną strzałkę (`C3 A2 C2 86 C2 92`). Znalazł to przegląd. Teksty
    wersji są teraz w `otaVersionChangePageText()` / `otaVersionChangeLogText()`
    (`admin/core/firmware/ota_update`), pilnują ich 3 testy native. Na płytce: `730e577` wgrany
    przez OTA po WiFi (obraz oznaczony jako ważny), potem wysłany drugi raz, żeby poprawiony
    firmware wyrenderował własną stronę wyniku: komunikat ma `E2 86 92`, 0 sekwencji podwójnie
    zakodowanych; linia `[ota]` `->`; „marked valid after 30 s” za oba razy. `/perf.json` po 89 s:
    przerwa pętli UI maks. 101 ms, 0 wolnych przejść, sterta 140 488 B, największy blok 59 380 B,
    wolny stos net 9 836 B.
  - Niesprawdzone: instalacja buildu `-dev` z GitHuba (po utworzeniu PR), zapis formularza bez
    zmian, dotyk i fizyczne zachowanie ostrzeżeń.
- **Kryteria §10:** pkt 1–4 spełnione. Pkt 5 — częściowo, do czasu instalacji z GitHuba i
  sprawdzeń ręcznych wyżej; dlatego status zostaje „etap 7 z 8”.
- **Obserwacja (bez zmian w tym etapie):** na panelu bez hasła `curl --digest` wysyła puste
  żądanie próbne i dostaje 400 („Nie wybrano pliku”); bez `--digest` upload działa. Środowisko
  `cyd-ota` w `platformio.ini` używa `--digest`, więc przy płytce bez hasła nie zadziała.

## 9. Testy

- Grupy testów ze spec 07 odpowiadają kontekstom 1:1 (`test_airspace`, `test_enrichment`,
  `test_opensky`, `test_display`, `test_admin`, `test_platform` — ten obejmuje też `shared/`); pliki testów leżą w grupie
  kontekstu testowanego kodu, z nazwą `<moduł>_test.cpp`.
- `check_layers.py` ma własny test (`tools/test_check_layers.py`, pytest w CI): przykładowe
  include'y dozwolone / zakazane z tabeli §5.
- Kryterium regresji po każdym etapie: liczba `RUN_TEST` nie maleje; firmware z etapów 1–4 i 7
  ma ten sam rozmiar ± 1 KB (refaktoryzacja nie zmienia kodu wynikowego istotnie).

## 10. Kryteria akceptacji (całość)

1. `tools/check_layers.py` bez naruszeń, wymuszane w CI.
2. Brak `#ifdef ARDUINO` w `core/` i `services/`; środowisko `native` kompiluje wszystko poza
   `adapters/`, `main/` (korzenie) i adapterową częścią `platform/`.
3. `main.cpp` < 250 linii, `net_task.cpp` < 300, żaden plik w `src/` > 600 linii
   (`config_page`, `release_check` dzielone wg sekcji/kart, jeśli przekraczają).
4. Moduły spoza `admin/` nie dołączają `Settings`.
5. Płytka: wszystkie kryteria akceptacji spec 01–04 i 08 przechodzą bez zmian (OTA z panelu,
   z GitHuba, noc, dwa rdzenie, ostrzeżenia); `[perf]` bez pogorszenia.

## 11. Poza zakresem

- Wirtualne interfejsy / dependency injection framework — porty to `std::function` albo
  struktury funkcji, jak `EnrichmentPort`.
- Zmiana formatu NVS, kopii zapasowej, API WWW.
- Biblioteki PlatformIO w `lib/` (zamiast katalogów w `src/`) — możliwe później, jeśli konteksty
  mają się budować osobno; dziś katalogi + `check_layers.py` dają to samo taniej.
