# Spec 12 — Reorganizacja kodu według DDD: konteksty, warstwy, reguły zależności

Z przeglądu kodu 2026-10-25, sekcje 2–3. Status: **w trakcie** (etap 1). Jedna specyfikacja dla całej
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
- W każdym kontekście **warstwy**: `domain/` (czysta logika i model), `app/` (przypadki
  użycia, automaty stanów, porty), `adapters/` (ESP32, LovyanGFX, HTTP, NVS).
- **Reguły zależności** zapisane w tabeli i sprawdzane automatycznie w CI.
- **Wspólny język** (ubiquitous language): te same nazwy w kodzie, CLAUDE.md, panelu i rozmowie.

Zachowanie urządzenia **nie zmienia się**. To refaktoryzacja: każdy etap kończy się zielonymi
testami i tym samym firmware'em (poza etapem 4, który dodaje testy wyciągniętej logiki).

## 3. Słownik pojęć (ubiquitous language)

| Pojęcie (PL) | W kodzie | Znaczenie |
|---|---|---|
| Dom | `Home` (`lat`, `lon`) | miejsce, nad którym urządzenie patrzy w niebo |
| Zasięg | `ScanArea` | prostokąt ±`radius_deg` wokół domu (dziś `BoundingBox`) |
| Stan samolotu | `AircraftState` | jedna obserwacja: pozycja, wysokość, prędkość, kurs, „na ziemi” (dziś `Aircraft`) |
| Lot | `TrackedFlight` | samolot w zasięgu z ustaleniami: odległość, kierunek, faza, linia, typ, trasa (dziś `AircraftRow`) |
| Obraz nieba | `AirspaceSnapshot` | niezmienna lista lotów z jednego pollu + przewidywanie przelotu (dziś `DataSnapshot`) |
| Faza | `Phase` | start / lądowanie / przelot / brak |
| Przelot nad domem | `Overflight` (`CpaPrediction`) | kiedy i jak blisko domu samolot przeleci (CPA) |
| Dane o samolocie | `AircraftInfo` | linia, typ, rejestracja (hexdb.io) |
| Trasa, lotnisko | `Route`, `Airport` | skąd–dokąd, kody IATA/ICAO, kraj |
| Kredyty | `OpenSkyQuota`, `CreditCost` | limit zapytań OpenSky |
| Stan źródła | `FeedHealth` | czy dane płyną (dziś `OpenSkyHealth`) |
| Ostrzeżenie | `Alert` | coś wymaga uwagi właściciela (spec 08) |
| Ustawienia | `Settings` | wszystko, co zapisane w NVS (dziś `Config`) |

Zmiany nazw (prawa kolumna „dziś”) robi się w **osobnym etapie** (§8, etap 6), po
przeniesieniu plików — żeby diff przeniesienia był czysto mechaniczny.

## 4. Konteksty

### 4.1 `airspace` — Niebo nad domem (**dziedzina główna**)

Odpowiada na pytania „co leci, gdzie, w którą stronę i kiedy będzie nade mną”. Nie wie nic
o OpenSky, hexdb.io ani ekranie.

- `domain/`: `aircraft_state.h` (`AircraftState`, wnioskowanie „na ziemi” — dziś
  `inferOnGround()` w `opensky_client`), `tracked_flight.h` (`TrackedFlight` — dziś
  `AircraftRow` z `table_view.h`), `flight_phase`, `overflight` (dziś `cpa_predictor`),
  `flight_ordering` (dziś `sortRowsForDisplay()`, `splitFeaturedAndRest()`), `now_pick` (dziś
  `pickNowAircraft()` z `flight_screen`), `feed_health.h`, `airspace_snapshot.h`.
- `app/`: `poll_airspace` (dziś `poll_pipeline`: stany samolotów → obraz nieba), port
  `EnrichmentPort` (dziś `LookupPorts`).

### 4.2 `enrichment` — Dane o samolotach, trasach i lotniskach (**pomocniczy**)

- `domain/`: `aircraft_info`, `route`, `airport` (typy + parsowanie JSON hexdb.io —
  `parse*LookupResponse`), `aircraft_names`, `place_names`, `lookup_budget`, `lookup_cache`
  (spec 05), `lookup_codec` (kodowanie rekordów NVS — dziś czysta część `lookup_store`).
- `app/`: `enrichment_service` (RAM → NVS → sieć, nie cache'uje błędów transportu — dziś
  `lookup*WithFetcher`), implementuje `airspace::EnrichmentPort`.
- `adapters/`: `hexdb_http` (HTTPS, keep-alive), `hexdb_sources` (dziś `httpFetch*`),
  `lookup_store_nvs`.

### 4.3 `opensky` — Źródło danych OpenSky (**warstwa antykorupcyjna**)

Tłumaczy świat OpenSky (tablice `states` z polami po indeksach, OAuth, nagłówki limitów) na
`airspace::AircraftState`. **Nic z formatu OpenSky nie wychodzi poza ten katalog.**

- `domain/`: `states_parser` (dziś `parseStatesResponse`, `extractAircraftFromStates`),
  `scan_area` (bbox + `openSkyCreditCost()` + `bboxAreaSqDeg()` — dziś w `config_portal`),
  `credits` (`openSkyDailyCredits`, `openSkyDailyRequests` — dziś w `config_page`),
  `opensky_quota`, `opensky_auth` (spec 06), `poll_schedule` (`shouldPollNow`,
  `tokenNeedsRefresh`, `isBeforeDeadline`).
- `adapters/`: `opensky_http` (token + `/states/all`, dziś adapterowa połowa `opensky_client`).

### 4.4 `display` — Wyświetlacz i dotyk (**prezentacja**)

- `domain/` (czysta logika widoków — układy, teksty, kolory, bez LovyanGFX):
  `screens/` (`table_layout`, `featured_panel`, `flight_screen`, `radar_layout` + dziś
  `radar_geometry`, `aircraft_summary`, `status_bar`, `service_screen`), `theme/`
  (`palette`, `phase_icons`, `nav_tabs`), `text/` (`utf8`, `fit_to_width`, `font_set` —
  dziś w `lcars_theme`), `navigation` (dziś `screen_nav`), `redraw_policy`,
  `touch_mapping` (dziś czysta część `touch_input`), `backlight_policy` (dziś czysta część
  `backlight`), `i18n` (teksty ekranu).
- `app/` (automaty stanów wyciągnięte z `main.cpp`, §6): `ui_flow` (dispatch dotyku,
  stronicowanie tabeli), `update_prompt_flow`, `ota_screen_flow`, `alert_screen_flow`
  (spec 08), `password_reset_flow`, `screenshot_bridge` (spec 09), `ui_perf`.
- `adapters/`: `lgfx/` (wszystkie `draw*()`, `LGFX_CYD.hpp` z `include/`), `touch_xpt2046`,
  `backlight_ldr` (ADC + PWM). Wygenerowane fonty (`lcars_efont.c`) to czyste dane czytane przez
  `fonts_test` — leżą w `display/domain/theme/fonts/`.

### 4.5 `admin` — Administracja urządzeniem (**pomocniczy**)

Ustawienia, panel WWW, kopie zapasowe, hasło, ostrzeżenia, aktualizacje.

- `domain/`: `settings` (agregat `Settings` + `sanitize` — dziś `config_store` bez NVS),
  `settings_form` (**nowe**: dziś `handleSave()`, §6), `backup` (dziś `config_backup`),
  `web_auth`, `alerts` (spec 08), `firmware/` (`ota_update`, `release_check` — czyste
  części, `version`).
- `app/`: `config_page` (renderer HTML), `web_i18n`.
- `adapters/`: `web_server` (dziś `config_portal`: trasy `WebServer`), `settings_nvs`,
  `ota_flash` (`Update`), `github_http` (dziś adapter `release_check` + `github_ca.h`).
- `assets/`: `style.css`, `app.js`, `favicon.svg` — dziś literały w `config_page.cpp`;
  osadzane przy budowaniu przez `tools/embed_assets.py` (pre-skrypt jak `pio_version.py`)
  do wygenerowanego `admin/app/web_assets.h`.

### 4.6 `shared` — Wspólne jądro (shared kernel)

Małe, stabilne typy i funkcje używane przez wiele kontekstów: `geo` (odległość, namiar,
kierunek świata — dziś `computeDistanceBearing()` z `table_view`, `compassPoint()` z
`lcars_theme`), `language.h` (`Language`), `clock` (formatowanie daty/czasu — czysta część
`time_sync`), `sun_times`, `wrap_time` (porównania `millis()` odporne na przekręcenie).
Zmiana w `shared/` wymaga uzasadnienia w opisie commita — to kontrakt dla wszystkich.

### 4.7 `platform` — Infrastruktura urządzenia

Nie jest dziedziną; łączy konteksty na ESP32: `core_link` (`NetState` jako **opublikowany
język** między rdzeniami), `perf_stats`, `wifi_manager`, `time_sync` (adapter NTP),
`net_config.h`.

### 4.8 `app/` w katalogu głównym — korzenie kompozycji

`main.cpp` (rdzeń 1: tworzy LGFX, składa przepływy z `display/app`, pętla) i `net_task.cpp`
(rdzeń 0: składa OpenSky + enrichment + admin, pętla). **Tylko tu** wolno znać wszystkie
konteksty naraz. Cel: `main.cpp` < 250 linii, bez własnych automatów stanów.

## 5. Reguły zależności

`#include "kontekst/warstwa/plik.h"` (ścieżka od `src/`, flaga `-I src`) — zależność widać
w każdym include'zie.

| Kto (wiersz) może dołączać | shared | airspace.domain | enrichment.domain | opensky.domain | display.domain | admin.domain | `*/app` | platform | `*/adapters` | Arduino / LGFX / WiFi |
|---|---|---|---|---|---|---|---|---|---|---|
| `shared` | ✔ | | | | | | | | | |
| `airspace/domain` | ✔ | ✔ | | | | | | | | |
| `enrichment/domain` | ✔ | | ✔ | | | | | | | |
| `opensky/domain` | ✔ | ✔ | | ✔ | | | | | | |
| `display/domain` | ✔ | ✔ | ✔ | | ✔ | ✔¹ | | | | |
| `admin/domain` | ✔ | ✔² | | ✔² | ✔² | ✔ | | | | |
| `airspace/app` | ✔ | ✔ | ✔ | | | | własny | | | |
| `display/app` | ✔ | ✔ | ✔ | | ✔ | ✔¹ | własny | ✔ | | |
| `admin/app` | ✔ | ✔ | | ✔ | ✔² | ✔ | własny | | | |
| `platform` | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | | ✔ | | tylko `#ifdef ARDUINO` |
| `*/adapters` | ✔ | swój kontekst | swój | swój | swój | swój | swój | ✔ | swój | ✔ |
| `app/` (korzenie) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |

¹ tylko typy do odczytu: `AlertSet`, ustawienia ekranu (§7). ² tylko typy wartości do złożenia
ustawień i ostrzeżeń (`NightMode`, `PhaseThresholds`, `OpenSkyQuota`…).

Zasady wynikające z tabeli:

- **Nic w `domain/` ani `app/` nie dołącza Arduino, LovyanGFX, WiFi, HTTPClient, Preferences.**
  Znika `#ifdef ARDUINO` z plików czystych; zostaje tylko w `platform/` (np. globalne obiekty
  `core_link`).
- `display` nigdy nie widzi `opensky` (ekran nie wie, skąd są dane); widzi `FeedHealth`.
- `opensky` i `enrichment` nie znają się nawzajem; łączy je `airspace/app/poll_airspace`
  przez porty.
- Konteksty nie dołączają cudzych `adapters/`.

**Strażnik:** `tools/check_layers.py` — czyta `#include "…"` z `src/` i `test/`, sprawdza tabelę
(zapisaną w skrypcie jako dane), wypisuje naruszenia `plik:linia: display/domain → opensky`.
W CI w `firmware.yml` przed testami; od etapu 7 błąd = czerwony build.

## 6. Logika wyciągnięta z adapterów (warstwa `app/`)

Każdy z poniższych staje się czystym typem z testami; adapter tylko podaje wejścia (dotyk,
czas, stan) i wykonuje wyjścia (rysuj, wyślij komendę, zapisz).

| Dziś | Nowe | Wejścia → wyjścia |
|---|---|---|
| `handleTap()` + `clampPage()` w `main.cpp` | `display/app/ui_flow` (`TablePager`, `routeTap()`) | punkt dotyku, ekran, liczba stron → `JumpTo` / `PagePrev` / `PageNext` / `OpenAlerts` / nic |
| `maybeShowUpdatePrompt()`, `serviceUpdatePrompt()` (7 zmiennych `static`) | `display/app/update_prompt_flow` | `PromptGate`, dotyk, czas → pokaż / timer / `InstallTag` / `SkipTag` / zamknij |
| `serviceOtaScreen()` | `display/app/ota_screen_flow` | `OtaProgress`, czas → rysuj ekran / postęp / wynik / wróć do ekranów po 5 s |
| `offerWebPasswordReset()` | `display/app/password_reset_flow` | dotyk, czas → przytrzymaj / potwierdź / wyczyść hasło |
| `shot::` (spec 09) | `display/app/screenshot_bridge` | żądania rdzenia 0 → pasy do odczytu |
| `perfLoopTick()` | `display/app/ui_perf` | czasy przejść pętli → `UiPerfShare` |
| `handleSave()` w `config_portal` | `admin/domain/settings_form` | `Settings` + `FormArgs` (mapa nazwa→wartość) → `{Settings, rejected[], passwordChange}` |

`settings_form` usuwa też 7 kopii bloku „`hasArg` → waliduj → przypisz albo dopisz do
`rejected`”: tabela pól `{nazwa, etykieta, parser, wskaźnik na pole}`.

## 7. Ustawienia: agregat + ustawienia per kontekst

`Settings` (dziś `Config`) zostaje jednym agregatem w `admin/domain` — tylko on jest zapisywany,
sanityzowany i odtwarzany z kopii. Inne konteksty **nie dołączają `Settings`**; dostają małe,
własne obiekty wartości, wycinane w korzeniach kompozycji:

| Kontekst | Obiekt | Pola |
|---|---|---|
| `airspace` | `Home`, `PhaseThresholds` | lat/lon, `near_airport_km`, `climb_rate_threshold_mps` |
| `opensky` | `OpenSkyCredentials`, `PollSettings` | client id/secret, `radius_deg`, `poll_interval_s` |
| `display` | `DisplaySettings`, `BacklightSettings`, `AutoCycleSettings` | język, ostatni ekran, jasność, noc, auto-cycle |
| `admin` | `UpdateSettings`, `WebSettings` | sprawdzanie aktualizacji, pre-release, hasło, język panelu |

`Settings` składa się z tych obiektów (pola `settings.backlight.night_mode` zamiast
`cfg.night_mode`). Klucze NVS i format kopii zapasowej **bez zmian** (testy `config_backup`
i `config_store` pilnują zgodności wstecz). `NetState` niesie `shared_ptr<const Settings>`,
a ekran czyta z niego tylko swoje obiekty.

## 8. Etapy (każdy = osobny PR, testy zielone, firmware działa na płytce)

1. **Szkielet.** Katalogi, `git mv` plików bez dzielenia, `-I src`, include'y z pełną ścieżką
   (skrypt `tools/rewrite_includes.py`, jednorazowy). `build_src_filter` dla `native`:
   `+<*> -<app/> -<*/adapters/> -<platform/*_esp32.cpp>`. `check_layers.py` w trybie raportu.
   Diff: tylko przeniesienia i include'y.
2. **Przeniesienie źle położonych typów.** `AircraftRow` z `table_view.h`, `OpenSkyHealth` ze
   `status_bar.h`, `computeDistanceBearing`, `compassPoint`, `openSkyCreditCost`, UTF-8 /
   `fitToWidth`, `inferOnGround`, `pickNowAircraft`, `sortRowsForDisplay` do kontekstów z §4.
   Testy idą za kodem (np. testy odległości z `table_view_test.cpp` do `shared/geo_test.cpp`).
3. **Podział czyste / sprzętowe.** Każdy plik z `#ifdef ARDUINO` → `domain|app/x.cpp` +
   `adapters/x_esp32.cpp` (albo `adapters/lgfx/x_draw.cpp`). Po tym etapie `grep -rl "ifdef
   ARDUINO" src --include=*.cpp` zwraca tylko `platform/` i `adapters/`.
4. **Warstwa `app/`** z §6 + testy (to jedyny etap, który dodaje testy, a nie tylko je przenosi).
   `main.cpp` < 250 linii, `config_portal` bez `handleSave` logiki.
5. **Ustawienia per kontekst** (§7) + zasoby WWW jako pliki (`admin/assets/`, `embed_assets.py`).
6. **Nazwy ze słownika** (§3), po jednej zmianie nazwy na commit (`AircraftRow` →
   `TrackedFlight` itd.), automatycznie (`sed` + kompilacja), bez innych zmian w tym commicie.
7. **Egzekwowanie.** `check_layers.py` blokuje CI; `docs/architecture.md` (konteksty, tabela
   z §5, słownik z §3) i sekcja „Code conventions” w CLAUDE.md zastąpiona krótkim odsyłaczem.

## 9. Testy

- Grupy testów ze spec 07 odpowiadają kontekstom 1:1 (`test_airspace`, `test_enrichment`,
  `test_opensky`, `test_display`, `test_admin`, `test_platform` — ten obejmuje też `shared/`); pliki testów leżą w grupie
  kontekstu testowanego kodu, z nazwą `<moduł>_test.cpp`.
- `check_layers.py` ma własny test (`tools/test_check_layers.py`, pytest w CI): przykładowe
  include'y dozwolone / zakazane z tabeli §5.
- Kryterium regresji po każdym etapie: liczba `RUN_TEST` nie maleje; firmware z etapów 1–3 i 6
  ma ten sam rozmiar ± 1 KB (refaktoryzacja nie zmienia kodu wynikowego istotnie).

## 10. Kryteria akceptacji (całość)

1. `tools/check_layers.py` bez naruszeń, wymuszane w CI.
2. Brak `#ifdef ARDUINO` w `domain/` i `app/`; środowisko `native` kompiluje wszystko poza
   `adapters/`, `app/` (korzenie) i adapterową częścią `platform/`.
3. `main.cpp` < 250 linii, `net_task.cpp` < 300, żaden plik w `src/` > 600 linii
   (`config_page`, `release_check` dzielone wg sekcji/kart, jeśli przekraczają).
4. Moduły spoza `admin/` nie dołączają `Settings`.
5. Płytka: wszystkie kryteria akceptacji spec 01–04 i 08 przechodzą bez zmian (OTA z panelu,
   z GitHuba, noc, dwa rdzenie, ostrzeżenia); `[perf]` bez pogorszenia.

## 11. Poza zakresem

- Wirtualne interfejsy / dependency injection framework — porty to `std::function` albo
  struktury funkcji, jak dziś `LookupPorts`.
- Zmiana formatu NVS, kopii zapasowej, API WWW.
- Biblioteki PlatformIO w `lib/` (zamiast katalogów w `src/`) — możliwe później, jeśli konteksty
  mają się budować osobno; dziś katalogi + `check_layers.py` dają to samo taniej.
