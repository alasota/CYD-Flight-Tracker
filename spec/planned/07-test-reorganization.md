# Spec 07 — Reorganizacja i poprawa testów natywnych

Z przeglądu kodu 2026-10-25, sekcja 4. Status: **zaimplementowane 2026-10-25**, czeka tylko na zielone CI z `native-asan` (kryterium 6,
§9); potem `git mv` do `spec/done/`.

## 1. Problem

- **41 osobnych suite'ów** (`test/test_<moduł>/`), każdy to osobny plik `.exe`, który kompiluje
  cały `src/`. `pio test -e native` trwa ~2 min, a na Windowsie Application Control losowo
  blokuje świeże `.exe` (`WinError 4551`; 2026-10-25: 10 z 41 suite'ów ERRORED, część także
  po ponownym uruchomieniu).
- **Płaska lista** 41 katalogów bez grupowania: nie widać, co jest logiką domeny, co siecią,
  a co widokiem.
- **Słabe komunikaty błędów:** 202 × `TEST_ASSERT_TRUE(a == b)` (Unity pisze tylko „Expected TRUE
  Was FALSE”), 126 × `TEST_ASSERT_TRUE(contains(...))` (nie widać, czego szukano).
- **Powielone helpery:** `contains()` w 3 plikach, ręczne składanie `Aircraft` / `AircraftRow`
  w ~8 plikach.
- **Kruche testy JSON**, które porównują dokładny tekst z białymi znakami
  (`"\"update_check\": false"` w `test_config_backup`).
- Brak `-Wall -Werror` i sanitizerów w środowisku `native`.

## 2. Cel

Sześć suite'ów pogrupowanych według kontekstów z [spec 12](12-code-reorganization-ddd.md)
(DDD), wspólne helpery w jednym miejscu, czytelne błędy.
**Liczba przypadków testowych nie maleje** (dziś 725 `RUN_TEST`), a każda dzisiejsza asercja ma
odpowiednik po migracji.

## 3. Nowy układ

```
test/
  README.md                    ← jak uruchamiać, gdzie co leży, konwencje (zamiast szablonu PIO)
  support/
    test_support.h             ← asercje (§4), resetAllForTesting()
    fixtures.h                 ← makeAircraft(), makeRow(), makeConfig(), stubPorts()
    json_assert.h              ← parseJson(), assertJsonBool/Int/String(doc, "a.b.c", …)
  test_airspace/               ← niebo nad domem: faza, przelot nad domem, obraz nieba
    main.cpp
    flight_phase_test.cpp  cpa_predictor_test.cpp  poll_pipeline_test.cpp
  test_enrichment/             ← dane o samolotach, trasach, lotniskach (hexdb.io, cache, NVS)
    main.cpp
    aircraft_lookup_test.cpp  route_lookup_test.cpp  lookup_store_test.cpp
    lookup_budget_test.cpp  aircraft_names_test.cpp  place_names_test.cpp
  test_opensky/                ← źródło danych OpenSky (parsowanie, limity, logowanie)
    main.cpp
    opensky_client_test.cpp  opensky_quota_test.cpp
  test_display/                ← ekrany, motyw, nawigacja, dotyk, jasność, teksty ekranu
    main.cpp
    table_view_test.cpp  featured_panel_test.cpp  flight_screen_test.cpp  radar_view_test.cpp
    radar_geometry_test.cpp  aircraft_summary_test.cpp  status_bar_test.cpp
    service_screen_test.cpp  screen_nav_test.cpp  redraw_policy_test.cpp  touch_input_test.cpp
    lcars_theme_test.cpp  fonts_test.cpp  backlight_test.cpp  i18n_test.cpp
  test_admin/                  ← ustawienia, panel WWW, kopie, hasło, aktualizacje
    main.cpp
    config_page_test.cpp  config_portal_test.cpp  config_backup_test.cpp  config_store_test.cpp
    web_auth_test.cpp  web_i18n_test.cpp  ota_update_test.cpp  release_check_test.cpp
    version_test.cpp  screenshot_test.cpp
  test_platform/               ← współbieżność, perf, WiFi, czas, wspólne jądro (shared/)
    main.cpp
    core_link_test.cpp  perf_stats_test.cpp  wifi_manager_test.cpp  time_sync_test.cpp
    sun_times_test.cpp
```

Przyporządkowanie 41 → 6 jest pełne (3 + 6 + 2 + 15 + 10 + 5). Nowe testy ze spec 05 / 06 / 08
trafiają do: `lookup_cache_test.cpp` → `test_enrichment`, `opensky_auth_test.cpp` →
`test_opensky`, `device_alerts_test.cpp` → `test_admin`. Grupy odpowiadają kontekstom ze
spec 12, więc po reorganizacji kodu testy zostają na miejscu; przenoszą się tylko pojedyncze
testy funkcji, które zmieniają kontekst (np. testy odległości z `table_view_test.cpp`
do `shared`).

### 3.1 Jak działa suite z wieloma plikami

- Każdy plik `*_test.cpp` ma testy jako `static void test_…()` i **jedną** funkcję publiczną
  `void run_<moduł>_tests()`, która woła `RUN_TEST(...)` w swoich granicach.
- `main.cpp` suite'u: `UNITY_BEGIN()`, po kolei `run_…_tests()`, `UNITY_END()`. To jedyne
  miejsce z `setUp()` / `tearDown()`.
- `setUp()` woła `resetAllForTesting()` z `test/support/test_support.h`. Ta funkcja czyści
  **cały** stan globalny modułów (`aircraftLookupClearCacheForTesting()`,
  `routeLookupClearCacheForTesting()`, `airportLookupClearCacheForTesting()`, stan stubów
  z `fixtures.h`, `i18nSetLanguage(Language::PL)`). Dzisiejsze `setUp()` z poszczególnych
  plików, które coś robią (np. `test_poll_pipeline`), stają się częścią tej funkcji albo
  lokalnym `reset…()` wołanym na początku testów danego pliku.
- Nagłówek każdego pliku: jedna linijka, co testuje i która sekcja CLAUDE.md / spec go opisuje.
- `platformio.ini`, `[env:native]`: `build_flags += -I test/support`.

### 3.2 Uruchamianie

- Wszystko: `pio test -e native` (6 binarek zamiast 41).
- Jedna grupa: `pio test -e native -f test_opensky`.
- Jeden moduł: `run_<moduł>_tests()` można wywołać selektywnie przez
  `-DTEST_MODULE=opensky_client` (w `main.cpp`: `if (RUN_MODULE(opensky_client)) …`). Opisane w
  `test/README.md`; nie jest wymagane do akceptacji. *Zmiana przy implementacji:* miało być
  `TEST_ONLY`, ale `unity.h` sam definiuje makro `TEST_ONLY()` (znacznik dla generatora
  runnerów), więc `#ifdef TEST_ONLY` było zawsze prawdziwe i suite uruchamiał 0 testów.
- `tools/screenshots.py`: `-f test_config_page` → `-f test_admin` (renderowanie z
  `SCREENSHOT_OUT` przenosi się razem z `config_page_test.cpp`). Do tego referencje w
  CLAUDE.md (sekcje „Testing”, „Commands”, „New versions from GitHub”).

## 4. Asercje i helpery (`test/support/`)

| Dziś | Po zmianie | Co widać przy błędzie |
|---|---|---|
| `TEST_ASSERT_TRUE(x.kind == Kind::Never)` | `ASSERT_ENUM_EQ(Kind::Never, x.kind)` | „Expected 2 Was 0” + nazwa wyrażenia |
| `TEST_ASSERT_TRUE(contains(html, "foo"))` | `ASSERT_CONTAINS(html, "foo")` | szukany tekst + pierwsze 200 znaków |
| `TEST_ASSERT_FALSE(contains(...))` | `ASSERT_NOT_CONTAINS(html, "foo")` | znaleziony tekst z otoczeniem ±60 znaków |
| `TEST_ASSERT_TRUE(s == "abc")` dla `std::string` | `ASSERT_STR_EQ("abc", s)` | oba stringi |
| `contains(json, "\"x\": false")` | `assertJsonBool(doc, "general.update_check", false)` | ścieżka + faktyczna wartość |

- `ASSERT_ENUM_EQ` = `TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e), static_cast<int>(a), #a)`.
- `ASSERT_CONTAINS` / `ASSERT_NOT_CONTAINS` budują komunikat i wołają `TEST_FAIL_MESSAGE`.
- `fixtures.h`: `makeAircraft(hex, callsign, lat, lon, {ground, alt, speed, track})`,
  `makeRow(...)`, `makeConfig()` (= `defaultConfig()` + Warszawa + `home_configured`),
  `stubPorts()` z `test_poll_pipeline` (rejestr wywołań do sprawdzania „ile fetchy”).
- `TEST_ASSERT_TRUE(warunek)` zostaje dla prawdziwych warunków logicznych (zakresy, `&&`), nie dla
  porównań jednej wartości.

## 5. Flagi kompilacji

`[env:native]`:

```ini
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -I test/support
    -Wall -Wextra -Werror
```

Ostrzeżenia, które wyjdą w `src/`, poprawić w tym samym kroku (nie wyłączać `-Wno-…` bez
komentarza dlaczego). W CI (`firmware.yml`, Ubuntu) dodatkowe środowisko `[env:native-asan]`
(`extends = env:native`, `-fsanitize=address,undefined -fno-omit-frame-pointer`, też w
`build_unflags`/`link_flags`) uruchamiane po zwykłych testach. Lokalnie na Windowsie (MinGW)
niewymagane.

## 6. Plan (każdy krok = osobny commit, testy zielone po każdym)

1. `test/support/` + flagi `-I`; jeden suite próbny: `test_enrichment` (6 modułów). Sprawdzić
   liczbę `RUN_TEST` przed i po (skrypt w §7).
2. Pozostałe pięć suite'ów, przeniesienie mechaniczne (`git mv` + `run_…_tests()`), bez
   zmiany treści testów.
3. Zamiana asercji na helpery z §4, plik po pliku (osobny commit na suite).
4. Testy JSON w `config_backup_test.cpp` na `json_assert.h`.
5. `-Wall -Wextra -Werror` + poprawki ostrzeżeń; `native-asan` w CI.
6. Aktualizacja `test/README.md`, CLAUDE.md, `tools/screenshots.py`.

## 7. Kryteria akceptacji

1. `pio test -e native` uruchamia **6** suite'ów, wszystkie PASSED; czas pełnego przebiegu
   mniejszy niż połowa dzisiejszych ~2 min (zmierzyć i wpisać tutaj).
2. Liczba `RUN_TEST` po migracji ≥ 725 (`grep -rh "RUN_TEST" test | wc -l` przed i po) i żaden
   test nie zniknął (porównanie list nazw `test_…`).
3. Zero `TEST_ASSERT_TRUE(... == ...)` dla enumów i stringów
   (`grep -rnE "TEST_ASSERT_TRUE\([^&|]*==" test` = pusto) i zero lokalnych kopii `contains()`.
4. Celowo zepsuta asercja w każdym z helperów z §4 daje komunikat z wartościami (sprawdzone ręcznie
   raz, opisane w `test/README.md`).
5. `python tools/screenshots.py` działa z nowym `-f test_admin`.
6. CI zielone, w tym `native-asan`.

## 8. Poza zakresem (osobne zadania)

- Nowe testy dla dziś nietestowanej logiki: parsowanie formularza w `handleSave`, automaty z
  `main.cpp` (pytanie o aktualizację, ekran OTA, handshake screenshotu), test obciążeniowy
  `LatestBox` na `std::thread` + TSan. Ta specyfikacja tylko porządkuje i poprawia to, co już jest.
- Przeniesienie `src/` do podkatalogów — [spec 12](12-code-reorganization-ddd.md).
- Raport pokrycia (`gcov`).

## 9. Wynik (2026-10-25)

1. 6 suite'ów, wszystkie PASSED. Pełny przebieg na Windowsie: **34 s** od czystego buildu,
   **~27 s** przyrostowo (przed: **179 s**, w tym 3 z 41 suite'ów ERRORED przez WinError 4551).
   4551 nadal się zdarza (średnio ~1 suite na przebieg), ale ponowienie to jeden suite.
2. `RUN_TEST`: **727** przed i po (`grep -rh` liczy linie, a `test_version.cpp` był dla grepa
   plikiem binarnym — stąd „725”; `grep -rah` daje 727); listy nazw identyczne.
3. `grep -rnE "TEST_ASSERT_TRUE\([^&|]*==" test --include='*.cpp'` = pusto (całe `test/`
   znajduje tylko przykłady „przed” w tabeli `test/README.md`); lokalnych `contains()` brak.
   Zamienionych asercji: 374 (skryptem + ~15 ręcznie). `std::vector` porównywane przez
   `TEST_ASSERT_EQUAL_UINT8_ARRAY`.
4. Każdy helper zepsuty celowo raz — komunikaty w tabeli `test/README.md` to faktyczne wyjście.
5. `python tools/screenshots.py` z `-f test_admin`: renderuje, PNG bajtowo identyczne z
   poprzednimi.
6. CI: **do potwierdzenia po pushu.** Lokalnie ASan się nie da (MinGW, brak WSL); UBSan w trybie
   trap (`-fsanitize=undefined -fsanitize-trap=all`) — wszystkie 6 suite'ów zielone.

Uwagi z implementacji:
- Unity bierze nazwę pliku z `UNITY_BEGIN()` (czyli `main.cpp`), więc każdy `run_…_tests()`
  zaczyna się od `UnitySetTestFile(__FILE__)` — inaczej błąd pokazywał `main.cpp:<linia z innego
  pliku>`.
- `-Wall -Wextra -Werror`: `src/` był czysty; tylko nieużywane parametry stubów w testach.
- `-fsanitize*` SCons przekazuje też do linkera, więc `native-asan` nie potrzebuje
  `link_flags` ani skryptu.
- `fixtures.h` zastąpił duplikaty tam, gdzie były identyczne (`plane()` w poll_pipeline,
  `displayRow()` w table_view, początek `sample()` w config_backup); pozostałe buildery wierszy
  ustawiają celowo inne pola dla swoich testów i zostały.
