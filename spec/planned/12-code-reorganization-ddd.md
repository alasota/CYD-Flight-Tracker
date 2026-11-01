# Spec 12 — Reorganizacja kodu według DDD: konteksty, warstwy, reguły zależności

Z przeglądu kodu 2026-10-25, sekcje 2–3.
Status: **etap 2 z 8 zrobiony (v1.11.1)**, etap 1 wydany w v1.11.0, etapy 3–8 do zrobienia. Jedna specyfikacja dla całej
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

Zmiany nazw (prawa kolumna „dziś”) robi się w **osobnym etapie** (§8, etap 7), po
przeniesieniu plików — żeby diff przeniesienia był czysto mechaniczny.

## 4. Konteksty

### 4.1 `airspace` — Niebo nad domem (**dziedzina główna**)

Odpowiada na pytania „co leci, gdzie, w którą stronę i kiedy będzie nade mną”. Nie wie nic
o OpenSky, hexdb.io ani ekranie.

- `core/`: `aircraft_state.h` (`AircraftState`, wnioskowanie „na ziemi” — dziś
  `inferOnGround()` w `opensky_client`), `tracked_flight.h` (`TrackedFlight` — dziś
  `AircraftRow` z `table_view.h`), `flight_phase`, `overflight` (dziś `cpa_predictor`),
  `flight_ordering` (dziś `sortRowsForDisplay()`, `splitFeaturedAndRest()`), `now_pick` (dziś
  `pickNowAircraft()` z `flight_screen`), `feed_health.h`, `airspace_snapshot.h`.
- `services/`: `poll_airspace` (dziś `poll_pipeline`: stany samolotów → obraz nieba), port
  `EnrichmentPort` (dziś `LookupPorts`).

### 4.2 `enrichment` — Dane o samolotach, trasach i lotniskach (**pomocniczy**)

- `core/`: `aircraft_info`, `route`, `airport` (typy + parsowanie JSON hexdb.io —
  `parse*LookupResponse`), `aircraft_names`, `place_names`, `lookup_budget`, `lookup_cache`
  (spec 05), `lookup_codec` (kodowanie rekordów NVS — dziś czysta część `lookup_store`).
- `services/`: `enrichment_service` (RAM → NVS → sieć, nie cache'uje błędów transportu — dziś
  `lookup*WithFetcher`), implementuje `airspace::EnrichmentPort`.
- `adapters/`: `hexdb_http` (HTTPS, keep-alive), `hexdb_sources` (dziś `httpFetch*`),
  `lookup_store_nvs`.

### 4.3 `opensky` — Źródło danych OpenSky (**warstwa antykorupcyjna**)

Tłumaczy świat OpenSky (tablice `states` z polami po indeksach, OAuth, nagłówki limitów) na
`airspace::AircraftState`. **Nic z formatu OpenSky nie wychodzi poza ten katalog.**

- `core/`: `states_parser` (dziś `parseStatesResponse`, `extractAircraftFromStates`),
  `scan_area` (bbox + `openSkyCreditCost()` + `bboxAreaSqDeg()` — dziś w `config_portal`),
  `credits` (`openSkyDailyCredits`, `openSkyDailyRequests` — dziś w `config_page`),
  `opensky_quota`, `opensky_auth` (spec 06), `poll_schedule` (`shouldPollNow`,
  `tokenNeedsRefresh`, `isBeforeDeadline`).
- `adapters/`: `opensky_http` (token + `/states/all`, dziś adapterowa połowa `opensky_client`).

### 4.4 `display` — Wyświetlacz i dotyk (**prezentacja**)

- `core/` (czysta logika widoków — układy, teksty, kolory, bez LovyanGFX):
  `screens/` (`table_layout`, `featured_panel`, `flight_screen`, `radar_layout` + dziś
  `radar_geometry`, `aircraft_summary`, `status_bar`, `service_screen`), `theme/`
  (`palette`, `phase_icons`, `nav_tabs`), `text/` (`utf8`, `fit_to_width`, `font_set` —
  dziś w `lcars_theme`), `navigation` (dziś `screen_nav`), `redraw_policy`,
  `touch_mapping` (dziś czysta część `touch_input`), `backlight_policy` (dziś czysta część
  `backlight`), `i18n` (teksty ekranu).
- `services/` (automaty stanów wyciągnięte z `main.cpp`, §6): `ui_flow` (dispatch dotyku,
  stronicowanie tabeli), `update_prompt_flow`, `ota_screen_flow`, `alert_screen_flow`
  (spec 08), `password_reset_flow`, `screenshot_bridge` (spec 09), `ui_perf`.
- `adapters/`: `lgfx/` (wszystkie `draw*()`, `LGFX_CYD.hpp` z `include/`), `touch_xpt2046`,
  `backlight_ldr` (ADC + PWM). Wygenerowane fonty (`lcars_efont.c`) to czyste dane czytane przez
  `fonts_test` — leżą w `display/core/theme/fonts/`.

### 4.5 `admin` — Administracja urządzeniem (**pomocniczy**)

Ustawienia, panel WWW, kopie zapasowe, hasło, ostrzeżenia, aktualizacje.

- `core/`: `settings` (agregat `Settings` + `sanitize` — dziś `config_store` bez NVS),
  `settings_form` (**nowe**: dziś `handleSave()`, §6), `backup` (dziś `config_backup`),
  `web_auth`, `alerts` (spec 08), `firmware/` (`ota_update`, `release_check` — czyste
  części, `version`), `config_page` (renderer HTML), `web_i18n`.
- `adapters/`: `web_server` (dziś `config_portal`: trasy `WebServer`), `settings_nvs`,
  `ota_flash` (`Update`), `github_http` (dziś adapter `release_check` + `github_ca.h`).
- `assets/`: `style.css`, `app.js`, `favicon.svg` — dziś literały w `config_page.cpp`;
  osadzane przy budowaniu przez `tools/embed_assets.py` (pre-skrypt jak `pio_version.py`)
  do wygenerowanego `admin/core/web_assets.h`.

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

### 4.8 `main/` w katalogu głównym — korzenie kompozycji

`main.cpp` (rdzeń 1: tworzy LGFX, składa przepływy z `display/services`, pętla) i `net_task.cpp`
(rdzeń 0: składa OpenSky + enrichment + admin, pętla). **Tylko tu** wolno znać wszystkie
konteksty naraz. Cel: `main.cpp` < 250 linii, bez własnych automatów stanów.

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
- `opensky` i `enrichment` nie znają się nawzajem; łączy je `airspace/services/poll_airspace`
  przez porty.
- Konteksty nie dołączają cudzych `adapters/`.

**Strażnik:** `tools/check_layers.py` — czyta `#include "…"` z `src/` i `test/`, sprawdza tabelę
(zapisaną w skrypcie jako dane), wypisuje naruszenia `plik:linia: display/core → opensky`.
W CI w `firmware.yml` przed testami; od etapu 8 błąd = czerwony build.

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

`Settings` (dziś `Config`) zostaje jednym agregatem w `admin/core` — tylko on jest zapisywany,
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
4. **Podział czyste / sprzętowe.** Każdy plik z `#ifdef ARDUINO` → `core|services/x.cpp` +
   `adapters/x_esp32.cpp` (albo `adapters/lgfx/x_draw.cpp`). Po tym etapie `grep -rl "ifdef
   ARDUINO" src --include=*.cpp` zwraca tylko `platform/` i `adapters/`.
5. **Warstwa `services/`** z §6 + testy (to jedyny etap, który dodaje testy, a nie tylko je przenosi).
   `main.cpp` < 250 linii, `config_portal` bez `handleSave` logiki.
6. **Ustawienia per kontekst** (§7) + zasoby WWW jako pliki (`admin/assets/`, `embed_assets.py`).
7. **Nazwy ze słownika** (§3), po jednej zmianie nazwy na commit (`AircraftRow` →
   `TrackedFlight` itd.), automatycznie (`sed` + kompilacja), bez innych zmian w tym commicie.
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
  struktury funkcji, jak dziś `LookupPorts`.
- Zmiana formatu NVS, kopii zapasowej, API WWW.
- Biblioteki PlatformIO w `lib/` (zamiast katalogów w `src/`) — możliwe później, jeśli konteksty
  mają się budować osobno; dziś katalogi + `check_layers.py` dają to samo taniej.
