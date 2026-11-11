Feature: Reorganizacja kodu według DDD — etapy 1–8: katalogi kontekstów i warstw (core / services / adapters, korzenie w main/), include'y z pełną ścieżką, raport zależności, źle położone typy w swoich kontekstach, podział czyste / sprzętowe, przepływy UI i formularz ustawień w services/ i core/ z testami, ustawienia per kontekst i zasoby panelu WWW jako pliki, nazwy ze słownika (§3) w kodzie, a na koniec raport warstw bez zgłoszeń i blokujący CI
  Etap 1 przeniósł pliki z płaskiego src/ (git mv, bez dzielenia) do katalogów kontekstów i warstw
  z §4; każdy #include "…" podaje ścieżkę od src/ (flaga -I src), środowisko native nie kompiluje
  korzeni kompozycji ani adapterów, a tools/check_layers.py raportuje naruszenia tabeli z §5
  (tylko raport, CI nie blokuje do etapu 8). Etap 2 zmienia nazwy warstw: domain/ → core/,
  app/ → services/, src/app/ → src/main/; z admin/app/ czyste config_page i web_i18n idą do
  admin/core/, a config_portal (trasy WebServer) do admin/adapters/. Etap 3 przenosi typy i
  funkcje leżące w złym kontekście (AircraftRow, OpenSkyHealth, odległość / namiar / kierunek
  świata, koszt w kredytach, UTF-8 / fitToWidth, „na ziemi”, wybór TERAZ, kolejność lotów) do
  kontekstów z §4, a ich testy idą za kodem do grupy nowego kontekstu. Etap 4 dzieli każdy plik
  z #ifdef ARDUINO na czystą połowę w core/ i sprzętową w adapters/ (nazwy z §4: opensky_http, settings_nvs,
  touch_xpt2046…; rysowanie w display/adapters/lgfx/x_draw.cpp), przenosi LGFX_CYD.hpp i touch_calibration.h z include/ do
  kontekstu display, a ostatnie czyste funkcje config_portal do admin/core/settings_form — wyjątek
  w filtrze native znika; config_portal (już tylko trasy WebServer) dostaje nazwę z §4.5: web_server. Zachowanie urządzenia się nie zmienia: testy zielone, liczba RUN_TEST
  nie maleje, firmware ma ten sam rozmiar ± 1 KB. Etap 5 wyciąga z main.cpp automaty stanów
  (dotyk i strony tabeli, pytanie o aktualizację, ekran OTA, ekran ostrzeżeń, reset hasła, zrzut
  ekranu, pomiar pętli UI) do display/services/, a z trasy POST /save logikę formularza do
  admin/core/settings_form — każdy z testami natywnymi; main.cpp ma mniej niż 250 linii, zachowanie
  urządzenia się nie zmienia. Etap 6 składa Config z małych obiektów wartości kontekstów (§7: Home,
  PhaseThresholds, OpenSkyCredentials, PollSettings, DisplaySettings, AutoCycleSettings,
  BacklightSettings, UpdateSettings, WebSettings) — moduły spoza admin/ dostają tylko swoje obiekty,
  klucze NVS i format kopii zapasowej się nie zmieniają — a CSS, JS i ikonę panelu WWW przenosi z
  literałów w config_page.cpp do plików w src/admin/assets/, z których tools/embed_assets.py generuje
  admin/core/web_assets.h; strona panelu jest bajt w bajt taka sama. Etap 7 zmienia nazwy na te ze
  słownika (§3: BoundingBox → ScanArea, OpenSkyHealth → FeedHealth, DataSnapshot → AirspaceSnapshot,
  LookupPorts → EnrichmentPort, AircraftRow → TrackedFlight, Aircraft → AircraftState, Config →
  Settings z funkcjami i plikiem config_store → settings, configPortal*() → webServer*()), po jednej
  na commit, narzędziem tools/rename_symbols.py, które zmienia tylko identyfikatory w kodzie — napisy
  (klucze NVS, klucze JSON, trasy WWW) i kod wynikowy zostają te same. Scenariusze etapów 3–6 używają
  już nazw po etapie 7. Etap 8 usuwa ostatnie naruszenia tabeli z §5 (typy i stałe do kontekstów z §4,
  składanie danych z kilku kontekstów w main/), dzieli net_task.cpp, config_page.cpp i web_server.cpp
  (§10 pkt 3) i włącza python tools/check_layers.py --strict w CI.
  Wyniki płytki z etapów 1–7 są w specyfikacji (§8 „Wynik etapu N”).

  # ---- etap 1 (aktualne po etapie 2): układ katalogów i include'y

  @test=tools/test_check_layers.py:LayoutTest.test_every_src_file_sits_in_a_context_and_layer
  Scenario: Każdy plik z src/ leży w katalogu kontekstu i warstwy z §4
    Given drzewo src/ po przeniesieniu
    When skrypt klasyfikuje każdy plik .h / .hpp / .c / .cpp
    Then każdy należy do shared/, platform/, main/ albo <kontekst>/<warstwa>/ (airspace, enrichment, opensky, display, admin × core, services, adapters)
    And w samym src/ nie ma żadnego pliku

  @test=tools/test_check_layers.py:LayoutTest.test_includes_in_src_and_test_use_full_paths
  Scenario: Include'y w src/ i test/ podają pełną ścieżkę od src/
    Given wszystkie dyrektywy #include "…" w src/ i test/
    When skrypt rozwiązuje każdą ścieżkę
    Then każda wskazuje plik względem src/, include/, test/support/ albo (tylko w test/) katalogu samego testu
    And żaden plik z src/ nie dołącza sąsiada krótką nazwą

  @test=tools/test_check_layers.py:BuildConfigTest.test_src_is_on_the_include_path_of_every_env
  Scenario: src/ jest na ścieżce include'ów firmware'u i testów
    Given platformio.ini
    When czytam build_flags środowisk cyd i native (cyd-ota i native-asan je dziedziczą)
    Then oba mają -I src

  @test=tools/test_check_layers.py:LayoutTest.test_run_test_count_not_below_stage_0_baseline
  Scenario: Przeniesienie nie gubi testów
    Given liczba wywołań RUN_TEST( w test/ zmierzona przed etapem 1
    When liczę je po przeniesieniu
    Then nowa liczba jest nie mniejsza

  @test=tools/test_subset_efont.py:OutputTest.test_writes_fonts_into_their_new_directory_with_full_path_include
  Scenario: Generator fontów pisze do katalogu fontów w display/core/ i dołącza nagłówek pełną ścieżką
    Given tools/subset_efont.py po przeniesieniu fontów
    When sprawdzam, dokąd pisze i jaki #include wstawia do wygenerowanego pliku
    Then pisze do istniejącego src/display/core/theme/fonts/lcars_efont.c
    And wstawia #include "display/core/theme/fonts/lcars_efont.h" — ten sam co w pliku w repozytorium

  # ---- etap 1: tools/rewrite_includes.py

  # ---- etap 1 (nazwy warstw po etapie 2): tools/check_layers.py (tryb raportu)

  @test=tools/test_check_layers.py:RuleTableTest.test_allowed_includes_are_not_reported
  Scenario Outline: Include dozwolony tabelą z §5 nie jest zgłaszany
    Given plik w <kto> z #include "<cel>"
    When skrypt sprawdza zależności
    Then nic nie zgłasza

    Examples:
      | kto                       | cel                                          |
      | src/airspace/core/        | shared/sun_times.h                           |
      | src/opensky/core/         | airspace/core/flight_phase.h                 |
      | src/display/core/         | enrichment/core/aircraft_names.h             |
      | src/display/core/         | admin/core/device_alerts.h                   |
      | src/admin/core/           | display/core/backlight.h                     |
      | src/admin/core/           | admin/core/web_i18n.h                        |
      | src/airspace/services/    | enrichment/core/aircraft_lookup.h            |
      | src/display/services/     | platform/core_link.h                         |
      | src/platform/             | display/core/screens/table_view.h            |
      | src/enrichment/adapters/  | enrichment/core/lookup_store.h               |
      | src/admin/adapters/       | admin/core/config_page.h                     |
      | src/main/                 | enrichment/adapters/hexdb_http.h             |
      | test/test_display/        | display/core/theme/lcars_theme.h             |

  @test=tools/test_check_layers.py:RuleTableTest.test_forbidden_includes_are_reported_with_file_and_line
  Scenario Outline: Include zakazany tabelą z §5 jest zgłaszany z plikiem, linią i parą warstw
    Given plik <kto>x.cpp z #include "<cel>" w linii 3
    When skrypt sprawdza zależności
    Then zgłasza "<kto>x.cpp:3: <para>"

    Examples:
      | kto                        | cel                                  | para                                      |
      | src/display/core/          | opensky/core/opensky_quota.h         | display/core -> opensky/core              |
      | src/enrichment/core/       | opensky/core/opensky_auth.h          | enrichment/core -> opensky/core           |
      | src/airspace/core/         | display/core/screens/table_view.h    | airspace/core -> display/core             |
      | src/opensky/core/          | enrichment/adapters/hexdb_http.h     | opensky/core -> enrichment/adapters       |
      | src/admin/core/            | admin/adapters/web_server.h          | admin/core -> admin/adapters              |
      | src/admin/core/            | admin/services/x.h                   | admin/core -> admin/services              |
      | src/airspace/services/     | enrichment/services/x.h              | airspace/services -> enrichment/services  |
      | src/admin/adapters/        | display/services/screenshot.h        | admin/adapters -> display/services        |
      | src/display/core/          | main/net_task.h                      | display/core -> main                      |
      | src/display/core/          | display/adapters/lgfx/LGFX_CYD.hpp   | display/core -> display/adapters          |
      | src/shared/                | platform/core_link.h                 | shared -> platform                        |

  @test=tools/test_check_layers.py:RuleTableTest.test_test_files_may_not_include_adapters_or_roots
  Scenario: Test nie dołącza adapterów ani korzeni kompozycji
    Given test/test_enrichment/x_test.cpp z #include "enrichment/adapters/hexdb_http.h" i #include "main/net_task.h"
    When skrypt sprawdza zależności
    Then zgłasza oba include'y jako test -> enrichment/adapters i test -> main

  @test=tools/test_check_layers.py:RuleTableTest.test_file_outside_any_context_is_reported
  Scenario: Plik poza katalogami kontekstów jest zgłaszany
    Given plik src/stray.cpp albo src/display/stray.cpp (bez warstwy)
    When skrypt klasyfikuje pliki
    Then zgłasza go jako leżący poza kontekstem i warstwą

  @test=tools/test_check_layers.py:SdkIncludeTest.test_sdk_include_in_platform_only_inside_ifdef_arduino
  Scenario: platform/ dołącza SDK tylko pod #ifdef ARDUINO
    Given src/platform/x.cpp z #include <freertos/queue.h> raz wewnątrz #ifdef ARDUINO … #endif, raz poza nim
    When skrypt sprawdza zależności
    Then zgłasza tylko include poza #ifdef ARDUINO

  @test=tools/test_check_layers.py:SdkIncludeTest.test_sdk_include_in_domain_is_reported_even_inside_ifdef
  Scenario Outline: core/ i services/ nie dołączają SDK, także pod #ifdef ARDUINO
    Given plik <kto>x.cpp z #include <<sdk>> wewnątrz #ifdef ARDUINO
    When skrypt sprawdza zależności
    Then zgłasza ten include jako <warstwa> -> sdk

    Examples:
      | kto                      | sdk              | warstwa             |
      | src/display/core/        | Arduino.h        | display/core        |
      | src/opensky/core/        | HTTPClient.h     | opensky/core        |
      | src/enrichment/core/     | Preferences.h    | enrichment/core     |
      | src/admin/services/      | WebServer.h      | admin/services      |
      | src/display/core/        | LovyanGFX.hpp    | display/core        |

  @test=tools/test_check_layers.py:SdkIncludeTest.test_standard_and_arduinojson_headers_allowed_everywhere
  Scenario: Nagłówki standardowe i ArduinoJson wolno dołączać wszędzie
    Given pliki w shared/, airspace/core/ i test/ z #include <vector>, <cstdint>, <atomic>, <ArduinoJson.h> i <unity.h>
    When skrypt sprawdza zależności
    Then nic nie zgłasza

  @test=tools/test_check_layers.py:CliTest.test_report_mode_exits_zero_with_violations
  Scenario: W trybie raportu naruszenia są wypisane, ale wynik to 0
    Given drzewo z dwoma zakazanymi include'ami
    When uruchamiam python tools/check_layers.py bez --strict
    Then wypisuje oba jako plik:linia: para warstw i podsumowanie z ich liczbą
    And kończy się kodem 0

  @test=tools/test_check_layers.py:CliTest.test_strict_mode_exits_one_with_violations
  Scenario: Z --strict naruszenie kończy się kodem 1, czyste drzewo kodem 0
    Given drzewo z jednym zakazanym include'em, i drugie bez żadnego
    When uruchamiam python tools/check_layers.py --strict na każdym
    Then pierwsze kończy się kodem 1, drugie kodem 0

  # (zmienione w etapie 8: krok blokuje CI)
  @test=tools/test_check_layers.py:WorkflowTest.test_firmware_workflow_blocks_on_layers_before_native_tests
  Scenario: CI uruchamia sprawdzenie warstw przed testami natywnymi i blokuje build, a testy narzędzi idą razem
    Given .github/workflows/firmware.yml
    When czytam jego kroki
    Then python tools/check_layers.py --strict jest w kroku przed pio test -e native
    And krok nie ma continue-on-error
    And tools/test_check_layers.py i tools/test_subset_efont.py są uruchamiane razem z innymi testami narzędzi

  # ---- etap 1: wydanie etapu nie przenosi specyfikacji do done/

  @test=tools/test_release_plan.py:SpecsToMoveTest.test_staged_spec_stays_planned_until_its_last_stage
  Scenario: Specyfikacja wdrażana etapami zostaje w planned/ po scaleniu jednego etapu
    Given spec/planned/12-….md ze statusem „etap 1 z 7” i scalona gałąź spec/12-…
    When /release ustala, które specyfikacje przenieść do done/
    Then 12 nie jest na liście
    But ta sama specyfikacja ze statusem „zrobione” jest

  # ---- etap 2: nazwy warstw (core / services / adapters, korzenie w main/)

  @test=tools/test_check_layers.py:LayoutTest.test_old_layer_folders_are_gone
  Scenario: W src/ nie ma już katalogów warstw o starych nazwach
    Given drzewo src/ po etapie 2
    When szukam katalogów src/<kontekst>/domain/, src/<kontekst>/app/ i src/app/
    Then żaden nie istnieje
    And src/main/ zawiera main.cpp, net_task.h i net_task.cpp

  @test=tools/test_check_layers.py:RuleTableTest.test_old_layer_names_are_outside_any_layer
  Scenario: Plik w katalogu o starej nazwie warstwy jest zgłaszany jako leżący poza warstwą
    Given pliki src/display/domain/x.h, src/admin/app/x.cpp i src/app/main.cpp
    When skrypt klasyfikuje pliki i sprawdza zależności
    Then każdy jest zgłoszony jako leżący poza kontekstem i warstwą
    But src/display/core/x.h, src/admin/services/x.cpp i src/main/main.cpp należą do display/core, admin/services i main

  # (zmienione w etapie 4: wyjątek +<admin/adapters/config_portal.cpp> zniknął razem z czystymi funkcjami config_portal, a plik nazywa się teraz web_server.cpp)
  @test=tools/test_check_layers.py:BuildConfigTest.test_native_filter_excludes_roots_and_adapters
  Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej części platform/
    Given platformio.ini
    When czytam build_src_filter środowiska native
    Then filtr to +<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp>
    And filtr nie przywraca żadnego pliku z adapters/ (od etapu 4 trasy WebServer są w admin/adapters/web_server.cpp bez czystych funkcji, a testy tych funkcji w admin/core/settings_form)

  # (zmienione w etapie 3: limit to stan po ostatnim wydanym etapie — 51 po etapie 2, 47 po etapie 3; w etapie 4: 14; w etapie 6: 12; w etapie 8: 0)
  @test=tools/test_check_layers.py:LayoutTest.test_layer_findings_do_not_exceed_the_stage_limit
  Scenario: Liczba naruszeń w raporcie warstw nie rośnie ponad stan po ostatnim etapie
    Given limit naruszeń zapisany w teście: 0 (stan po etapie 8; po etapie 2 było 51, po etapie 3 47, po etapach 4 i 5 14, po etapach 6 i 7 12)
    When uruchamiam check_layers na drzewie repozytorium
    Then łączna liczba naruszeń jest nie większa niż 0
    And nowe naruszenie powyżej limitu kończy test błędem, a lista naruszeń jest w komunikacie

  @test=tools/test_check_layers.py:LayoutTest.test_no_old_layer_paths_left_in_code_and_build_files
  Scenario: W kodzie, testach, skryptach i plikach budowania nie zostały ścieżki ze starymi nazwami warstw
    Given src/, test/, include/, platformio.ini, .github/workflows/ i tools/*.py (bez tabeli etapu 1 i testów narzędzi)
    When szukam <kontekst>/domain/, <kontekst>/app/, src/app/ i -<app/>
    Then nie ma żadnego trafienia, także w komentarzach

  @manual
  Scenario: Etap 2 nie zmienia testów ani firmware'u
    Given na main przed etapem 2: liczba przypadków w każdej z 6 grup pio test -e native, liczba RUN_TEST( oraz rozmiar firmware.bin, linie „Flash: used” / „RAM: used” i lista bibliotek z pio run -e cyd
    When mierzę to samo na gałęzi etapu 2 (po usunięciu .pio/build/native i .pio/build/cyd)
    Then liczby przypadków i RUN_TEST( są te same, wszystkie grupy zielone
    And firmware.bin różni się o najwyżej 1024 bajty, a lista bibliotek jest ta sama
    And python tools/check_layers.py zgłasza dokładnie 51 naruszeń: 55 z etapu 1 pod nowymi nazwami, minus 9 (4 × include web_i18n.h / config_page.h wewnątrz admin/core, 4 × SDK i 1 × platform w config_portal.cpp, teraz adapterze), plus 5 (config_page.cpp -> admin/adapters, config_portal_test.cpp: test -> admin/adapters, 3 × include innego kontekstu w config_portal.cpp: display/core × 2, opensky/core)
    And python tools/rewrite_includes.py --check kończy się kodem 0, a krok CI „Check the OTA firmware marker” przechodzi

  @manual
  Scenario: Płytka z firmware'em etapu 2 działa jak przedtem
    Given firmware z gałęzi wgrany przez USB (pio run -e cyd -t upload, zgoda właściciela na ten przebieg) i otwarty pio device monitor -b 115200
    When płytka startuje i łączy się z WiFi
    Then log szeregowy nie pokazuje paniki ani restartu, a płytka odpowiada pod cyd-sky.local
    And /screenshot.bmp?screen=0, 1 i 2 pokazują ekrany LOTY, TERAZ i RADAR z nagłówkiem i paskiem zakładek jak przedtem
    And stopka panelu ustawień pokazuje wersję z gałęzi
    And /perf.json po ok. 2 minutach: przerwa pętli UI i wolne przejścia jak po etapie 1 (maks. 74 ms, 0), sterta i największy blok nie mniejsze o więcej niż kilka KB (146 KB / 59 KB), bez restartu

  # ---- etap 3: źle położone typy i funkcje w kontekstach z §4 (testy idą za kodem)

  @test=tools/test_check_layers.py:LayoutTest.test_moved_symbols_are_declared_in_their_new_home
  Scenario Outline: Przeniesiony typ lub funkcja ma jedną deklarację, w pliku swojego kontekstu
    Given drzewo src/ po etapie 3
    When szukam w nagłówkach src/ deklaracji <symbol>
    Then jest dokładnie jedna i leży w src/<plik>
    And dawny plik (<dawniej>) już jej nie ma

    Examples:
      | symbol                 | plik                                   | dawniej                                   |
      | TrackedFlight          | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | buildEnrichedRecords   | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | annotateDistances      | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | classifyPhases         | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | sortRowsForDisplay     | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | sortRowsByDistance     | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | splitFeaturedAndRest   | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | pickNowAircraft        | airspace/core/now_pick.h               | display/core/screens/flight_screen.h      |
      | FeedHealth             | airspace/core/feed_health.h            | display/core/screens/status_bar.h         |
      | AircraftState          | airspace/core/aircraft_state.h         | opensky/core/opensky_client.h             |
      | inferOnGround          | airspace/core/aircraft_state.h         | opensky/core/opensky_client.h             |
      | computeDistanceBearing | shared/geo.h                           | display/core/screens/table_view.h         |
      | compassPoint           | shared/geo.h                           | display/core/theme/lcars_theme.h          |
      | openSkyCreditCost      | opensky/core/scan_area.h               | admin/adapters/config_portal.h            |
      | bboxAreaSqDeg          | opensky/core/scan_area.h               | admin/adapters/config_portal.h            |
      | utf8Length             | display/core/text/utf8.h               | display/core/theme/lcars_theme.h          |
      | fitToWidth             | display/core/text/fit_to_width.h       | display/core/theme/lcars_theme.h          |
      | isFontSafeText         | display/core/text/font_set.h           | display/core/theme/lcars_theme.h          |

  @test=tools/test_check_layers.py:LayoutTest.test_moved_tests_follow_their_code
  Scenario Outline: Testy przeniesionego kodu leżą w grupie jego kontekstu, bez kopii w starym pliku
    Given test/ po etapie 3 i lista 49 testów przeniesionych w etapie 3 (zapisana w teście narzędzia)
    When szukam definicji testu <test>
    Then jest dokładnie jedna, w test/<plik>, i ten sam plik wywołuje ją przez RUN_TEST(
    And w <dawniej> nie ma ani definicji, ani RUN_TEST( tego testu

    Examples:
      | test                                                   | plik                                    | dawniej                                  |
      | test_distance_one_degree_north_at_equator              | test_platform/geo_test.cpp              | test_display/table_view_test.cpp         |
      | test_compass_points                                    | test_platform/geo_test.cpp              | test_display/lcars_theme_test.cpp        |
      | test_credit_cost_boundary_exactly_25_sq_deg            | test_opensky/scan_area_test.cpp         | test_admin/config_portal_test.cpp        |
      | test_bbox_area_matches_radius                          | test_opensky/scan_area_test.cpp         | test_admin/config_portal_test.cpp        |
      | test_infer_on_ground_rules                             | test_airspace/aircraft_state_test.cpp   | test_opensky/opensky_client_test.cpp     |
      | test_build_enriched_records_found_true                 | test_airspace/tracked_flight_test.cpp   | test_display/table_view_test.cpp         |
      | test_annotate_fills_distance_for_positioned_aircraft   | test_airspace/tracked_flight_test.cpp   | test_display/table_view_test.cpp         |
      | test_classify_phases_no_distance_forces_none           | test_airspace/tracked_flight_test.cpp   | test_display/table_view_test.cpp         |
      | test_sort_puts_rows_without_distance_last              | test_airspace/flight_ordering_test.cpp  | test_display/table_view_test.cpp         |
      | test_display_order_puts_airborne_before_ground_traffic | test_airspace/flight_ordering_test.cpp  | test_display/table_view_test.cpp         |
      | test_split_featured_and_rest_empty                     | test_airspace/flight_ordering_test.cpp  | test_display/table_view_test.cpp         |
      | test_pick_prefers_soonest_overhead_not_nearest         | test_airspace/now_pick_test.cpp         | test_display/flight_screen_test.cpp      |
      | test_utf8_length_counts_characters_not_bytes           | test_display/utf8_test.cpp              | test_display/lcars_theme_test.cpp        |
      | test_fit_never_splits_a_utf8_character                 | test_display/fit_to_width_test.cpp      | test_display/lcars_theme_test.cpp        |
      | test_font_safe_accepts_ascii_and_polish_only           | test_display/font_set_test.cpp          | test_display/lcars_theme_test.cpp        |

  @test=tools/test_check_layers.py:LayoutTest.test_stage_3_removes_the_misplaced_type_findings
  Scenario Outline: Raport warstw nie zgłasza już include'ów, które były potrzebne tylko dla przeniesionych typów
    Given drzewo repozytorium po etapie 3
    When uruchamiam check_layers
    Then żadne zgłoszenie z src/<plik> nie wskazuje warstwy <cel>

    Examples:
      | plik                                      | cel             |
      | airspace/services/poll_pipeline.cpp       | display/core    |
      | airspace/services/poll_pipeline.h         | opensky/core    |
      | display/core/screens/table_view.h         | opensky/core    |
      | admin/core/config_page.cpp                | admin/adapters  |
      | admin/core/device_alerts.cpp              | admin/adapters  |

  @test=tools/test_check_layers.py:LayoutTest.test_core_link_includes_no_screen_headers
  Scenario: core_link nie dołącza już widoków dla typów, które przenosi między rdzeniami
    Given src/platform/core_link.h po etapie 3
    When czytam jego dyrektywy #include
    Then żadna nie wskazuje pliku w display/core/screens/
    And TrackedFlight i FeedHealth przychodzą z airspace/core/tracked_flight.h i airspace/core/feed_health.h

  @test=test_platform/geo_test.cpp:test_distance_london_to_paris_sanity_check
  @test=test_platform/geo_test.cpp:test_distance_zero_when_home_equals_aircraft
  Scenario: Odległość i namiar od domu liczy wspólne jądro (shared/geo)
    Given dom w Londynie (51.5074, -0.1278)
    When liczę odległość i namiar do Paryża (48.8566, 2.3522), a potem do samego domu
    Then do Paryża wychodzi ok. 344 km (± 5) w kierunku między 90° a 180°
    And do domu wychodzi 0 km i namiar 0°

  @test=test_platform/geo_test.cpp:test_compass_points
  Scenario Outline: Kierunek świata dla namiaru liczy wspólne jądro (shared/geo)
    Given namiar <namiar>°
    When pytam o kierunek świata
    Then dostaję <kierunek>

    Examples:
      | namiar | kierunek |
      | 0      | N        |
      | 22     | N        |
      | 23     | NE       |
      | 135    | SE       |
      | 350    | N        |
      | 360    | N        |
      | -90    | W        |

  @test=test_opensky/scan_area_test.cpp:test_credit_cost_just_below_and_above_each_boundary
  @test=test_opensky/scan_area_test.cpp:test_credit_cost_boundary_exactly_25_sq_deg
  Scenario Outline: Koszt zapytania OpenSky w kredytach liczy kontekst opensky (scan_area), nie panel WWW
    Given zasięg ±<promień>° wokół domu
    When liczę koszt jednego zapytania /states/all
    Then wynosi <kredyty> (pole ≤ 25 / ≤ 100 / ≤ 400 / powyżej 400 stopni kwadratowych)

    Examples:
      | promień | kredyty |
      | 2.4     | 1       |
      | 2.5     | 1       |
      | 2.6     | 2       |
      | 5.1     | 3       |
      | 9.9     | 3       |
      | 10.1    | 4       |

  @test=test_airspace/aircraft_state_test.cpp:test_infer_on_ground_rules
  @test=test_opensky/opensky_client_test.cpp:test_flag_false_but_no_altitude_and_no_speed_is_on_ground
  Scenario: Regułę „na ziemi” zna dziedzina (airspace), a parser OpenSky z niej korzysta
    Given samolot bez flagi on_ground, na 900 m
    When leci 100 m/s, a potem pełznie 10 m/s
    Then najpierw jest w powietrzu, potem na ziemi
    And stan z /states/all bez wysokości i prędkości, z flagą false, parser dalej oznacza jako „na ziemi”

  @test=test_airspace/tracked_flight_test.cpp:test_build_enriched_records_found_false_when_lookup_missing
  @test=test_airspace/tracked_flight_test.cpp:test_annotate_leaves_no_position_aircraft_unset
  Scenario: Lot (TrackedFlight) składa się w airspace bez znajomości ekranu
    Given stan samolotu bez wpisu w danych hexdb.io i drugi bez pozycji
    When buduję wiersze lotów i liczę odległości od domu
    Then pierwszy ma linię i typ jako nieznalezione (wyświetlane „--”), a nie znika
    And drugi nie ma odległości (has_distance = false, 0 km, 0°)

  @test=test_airspace/flight_ordering_test.cpp:test_display_order_puts_airborne_before_ground_traffic
  @test=test_airspace/flight_ordering_test.cpp:test_display_order_puts_positionless_last
  Scenario: Kolejność lotów ustala airspace: w powietrzu, potem na ziemi, potem bez pozycji
    Given loty w powietrzu, na ziemi i bez pozycji, w różnych odległościach
    When układam je do wyświetlenia
    Then najpierw są te w powietrzu od najbliższego, potem na ziemi od najbliższego, na końcu bez pozycji

  @test=test_airspace/now_pick_test.cpp:test_pick_prefers_soonest_overhead_not_nearest
  @test=test_airspace/now_pick_test.cpp:test_pick_empty
  Scenario: Wybór na ekran TERAZ robi airspace: najbliższy przelot nad domem, nie najbliższy samolot
    Given trzy samoloty od najbliższego: pierwszy minie dom o 12 km, drugi przeleci nad domem (1 km) za 40 s, trzeci (0,5 km) za 90 s
    When wybieram samolot na ekran TERAZ
    Then wybrany jest drugi, z przelotem za 40 s
    And przy pustej liście nie ma wyboru (indeks -1)

  @test=test_airspace/poll_pipeline_test.cpp:test_rows_ordered_airborne_ground_no_position
  @test=test_airspace/poll_pipeline_test.cpp:test_now_pick_points_at_the_sorted_row
  Scenario: Poll składa obraz nieba z airspace/core, bez nagłówków ekranów i OpenSky
    Given odpowiedź z samolotami w powietrzu, na ziemi i bez pozycji
    When poll_pipeline buduje obraz nieba
    Then wiersze są w kolejności do wyświetlenia, a wybór TERAZ wskazuje właściwy wiersz po sortowaniu

  @test=test_display/fit_to_width_test.cpp:test_fit_never_splits_a_utf8_character
  @test=test_display/fit_to_width_test.cpp:test_fit_trims_trailing_space_before_ellipsis
  Scenario: Przycinanie tekstu (display/core/text) nie rozcina polskiej litery i nie zostawia spacji przed „...”
    Given „Łódź Łódź” i miara 1 px na bajt, oraz „LOT Airlines” w 7 px
    When przycinam do każdej szerokości od 0 do długości tekstu
    Then każdy wynik jest poprawnym UTF-8 z zestawu fontów
    And „LOT Airlines” daje „LOT...”

  @test=test_display/utf8_test.cpp:test_utf8_length_counts_characters_not_bytes
  Scenario: Długość tekstu do układu (display/core/text) liczy znaki, nie bajty
    Given „SKĄD” (5 bajtów) i „Łódź” (7 bajtów)
    When liczę długość
    Then obie mają 4 znaki, a pusty tekst 0

  @test=test_display/font_set_test.cpp:test_font_safe_accepts_ascii_and_polish_only
  @test=test_display/fonts_test.cpp:test_every_allowed_character_has_a_glyph_in_every_font
  Scenario: Zestaw znaków fontów (display/core/text) to ASCII, polskie litery i kilka symboli
    Given „ZBLIŻA SIĘ”, „Kraków”, „Zürich”, „€”, tabulator i ucięta sekwencja UTF-8
    When sprawdzam, czy fonty je narysują
    Then pierwsze dwa tak, pozostałe nie
    And każdy dozwolony znak ma glif we wszystkich trzech fontach

  @test=test_display/status_bar_test.cpp:test_health_dot_colour
  @test=test_platform/core_link_test.cpp:test_diff_health_with_rows_is_header_only
  Scenario: Stan źródła danych (FeedHealth z airspace) dalej koloruje kropkę w nagłówku i odświeża tylko nagłówek
    Given stan źródła Ok, RateLimited, AuthError, NetworkError
    When nagłówek dobiera kolor kropki, a zmiana stanu przy widocznych lotach przechodzi przez diffNetState
    Then kolory są jak przed etapem (zielony, żółty, czerwony, czerwony)
    And zmiana stanu przy widocznych lotach odświeża tylko nagłówek

  @manual
  Scenario: Etap 3 nie zmienia liczby testów ani firmware'u
    Given na main przed etapem 3 (v1.11.1): przypadki w 6 grupach pio test -e native (admin 265, airspace 46, display 340, enrichment 80, opensky 67, platform 78 = 876), liczba RUN_TEST( (876) oraz rozmiar firmware.bin, linie „Flash: used” / „RAM: used” i lista bibliotek z pio run -e cyd
    When mierzę to samo na gałęzi etapu 3 (po usunięciu .pio/build/native i .pio/build/cyd)
    Then wszystkie grupy są zielone, a przypadków jest razem 877: admin 258, airspace 71, display 312, enrichment 80, opensky 73, platform 83 (49 testów przeszło do grup kontekstów nowego miejsca kodu, 1 nowy: granica znaku UTF-8, teraz publiczna funkcja)
    And RUN_TEST( jest 877, firmware.bin różni się o najwyżej 1024 bajty, a lista bibliotek jest ta sama
    And python tools/check_layers.py zgłasza dokładnie 47 naruszeń: 51 z etapu 2 minus 6 (poll_pipeline.cpp → display/core × 2, poll_pipeline.h → opensky/core, table_view.h → opensky/core, config_page.cpp i device_alerts.cpp → admin/adapters) plus 2 (tracked_flight.h → enrichment/core × 2: AircraftRow niesie AircraftInfo, RouteInfo i AirportInfo)
    And python tools/rewrite_includes.py --check kończy się kodem 0, python tools/check_comments.py --strict nic nie zgłasza, a filtr native wciąż przywraca config_portal.cpp (zostają w nim parseOpenSkyCredentialsJson i dwa walidatory — do etapu 4)

  @manual
  Scenario: Płytka z firmware'em etapu 3 działa jak przedtem
    Given firmware z gałęzi wgrany przez USB (pio run -e cyd -t upload, za zgodą właściciela) i otwarty pio device monitor -b 115200
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then log szeregowy nie pokazuje paniki ani restartu, a płytka odpowiada pod cyd-sky.local
    And /screenshot.bmp?screen=0 pokazuje tabelę LOTY z odległościami, chipem kierunku (np. „SE 6km”) i samolotami w powietrzu przed tymi na ziemi; screen=1 ekran TERAZ z odliczaniem; screen=2 RADAR z podsumowaniem
    And zakładka OpenSky w panelu pokazuje koszt w kredytach jak przedtem dla zapisanego promienia, a stopka wersję z gałęzi
    And /perf.json po ok. 3 minutach: wolnych przejść pętli UI 0, sterta i największy blok nie mniejsze o więcej niż kilka KB niż po etapie 2 (147 KB / 59 KB), bez restartu

  # ---- etap 4: podział czyste / sprzętowe (core/x + adapter o nazwie z §4, rysowanie w display/adapters/lgfx/x_draw)

  @test=tools/test_check_layers.py:LayoutTest.test_arduino_ifdef_only_in_platform_and_adapters
  Scenario: #ifdef ARDUINO zostaje tylko w platform/ i adapters/
    Given pliki .h, .hpp, .c i .cpp w src/ po etapie 4
    When szukam warunków #ifdef ARDUINO, #ifndef ARDUINO i #if defined(ARDUINO)
    Then każdy plik z takim warunkiem leży w src/platform/ albo w src/<kontekst>/adapters/
    And żaden plik w core/, services/, shared/ ani w korzeniach main/ go nie ma (także nagłówki, §10 pkt 2)

  @test=tools/test_check_layers.py:LayoutTest.test_core_and_services_never_name_lovyangfx
  Scenario: Typy LovyanGFX i funkcje rysujące są tylko w display/adapters/
    Given kod (bez komentarzy) plików w src/*/core/, src/*/services/ i src/shared/
    When szukam LGFX, LGFX_Sprite i lgfx::
    Then nie ma żadnego trafienia
    And każda deklaracja funkcji z parametrem LGFX & leży w nagłówku w src/display/adapters/

  @test=tools/test_check_layers.py:LayoutTest.test_hardware_functions_are_defined_in_adapters
  Scenario Outline: Sprzętowa połowa modułu jest zdefiniowana w adapterze, a czysta zostaje w core/
    Given drzewo src/ po etapie 4
    When szukam definicji funkcji <funkcja> w plikach .cpp
    Then jest dokładnie jedna, w src/<adapter>
    And src/<rdzeń> nadal istnieje, nie definiuje jej i nie ma w sobie #ifdef ARDUINO

    Examples:
      | funkcja                   | adapter                                         | rdzeń                                     |
      | drawStatusBar             | display/adapters/lgfx/status_bar_draw.cpp       | display/core/screens/status_bar.cpp       |
      | drawOtaScreen             | display/adapters/lgfx/service_screen_draw.cpp   | display/core/screens/service_screen.cpp   |
      | drawRadarView             | display/adapters/lgfx/radar_view_draw.cpp       | display/core/screens/radar_view.cpp       |
      | flightScreenReserveSprite | display/adapters/lgfx/flight_screen_draw.cpp    | display/core/screens/flight_screen.cpp    |
      | drawTablePage             | display/adapters/lgfx/table_view_draw.cpp       | display/core/screens/table_view.cpp       |
      | drawFeaturedPanel         | display/adapters/lgfx/featured_panel_draw.cpp   | display/core/screens/featured_panel.cpp   |
      | drawAircraftSummary       | display/adapters/lgfx/aircraft_summary_draw.cpp | display/core/screens/aircraft_summary.cpp |
      | drawBottomNav             | display/adapters/lgfx/lcars_theme_draw.cpp      | display/core/theme/lcars_theme.cpp        |
      | touchInputRead            | display/adapters/touch_xpt2046.cpp              | display/core/touch_input.cpp              |
      | backlightLoop             | display/adapters/backlight_ldr.cpp              | display/core/backlight.cpp                |
      | storeLoadAircraft         | enrichment/adapters/lookup_store_nvs.cpp        | enrichment/core/lookup_store.cpp          |
      | lookupAircraft            | enrichment/adapters/hexdb_sources.cpp           | enrichment/core/aircraft_lookup.cpp       |
      | lookupRoute               | enrichment/adapters/hexdb_sources.cpp           | enrichment/core/route_lookup.cpp          |
      | fetchAircraftStates       | opensky/adapters/opensky_http.cpp               | opensky/core/opensky_client.cpp           |
      | otaFlashSession           | admin/adapters/ota_flash.cpp                    | admin/core/firmware/ota_update.cpp        |
      | releaseCheckLoop          | admin/adapters/github_http.cpp                  | admin/core/firmware/release_check.cpp     |
      | loadSettings              | admin/adapters/settings_nvs.cpp                 | admin/core/settings.cpp                   |
      | deviceAlertInputs         | main/alert_sources.cpp                          | admin/core/device_alerts.cpp              |

  @test=tools/test_check_layers.py:LayoutTest.test_stage_4_symbols_are_declared_in_their_new_home
  Scenario Outline: Typ lub funkcja wyniesiona przy podziale ma jedną deklarację, w swoim nowym nagłówku
    Given nagłówki w src/ po etapie 4
    When szukam deklaracji <symbol> (typu albo funkcji; deklaracja zapowiadająca się nie liczy)
    Then jest dokładnie jedna, w src/<nowy_nagłówek>
    And src/<stary_nagłówek> jej nie ma

    Examples:
      | symbol                      | nowy_nagłówek                            | stary_nagłówek                    |
      | parseOpenSkyCredentialsJson | admin/core/settings_form.h               | admin/adapters/web_server.h       |
      | CredentialsFile             | admin/core/settings_form.h               | admin/adapters/web_server.h       |
      | isValidFloatString          | admin/core/settings_form.h               | admin/adapters/web_server.h       |
      | isValidUnsignedIntString    | admin/core/settings_form.h               | admin/adapters/web_server.h       |
      | OtaObserver                 | admin/core/firmware/ota_update.h         | admin/adapters/web_server.h       |
      | TouchPoint                  | display/adapters/touch_xpt2046.h         | display/core/touch_input.h        |
      | touchInputRead              | display/adapters/touch_xpt2046.h         | display/core/touch_input.h        |
      | gfxTextMeasure              | display/adapters/lgfx/lcars_theme_draw.h | display/core/theme/lcars_theme.h  |
      | drawPhaseIcon               | display/adapters/lgfx/lcars_theme_draw.h | display/core/theme/lcars_theme.h  |
      | drawTablePage               | display/adapters/lgfx/table_view_draw.h  | display/core/screens/table_view.h |
      | drawStatusBar               | display/adapters/lgfx/status_bar_draw.h  | display/core/screens/status_bar.h |
      | backlightBegin              | display/adapters/backlight_ldr.h         | display/core/backlight.h          |
      | formatDate                  | shared/clock.h                           | platform/time_sync.h              |
      | minuteOf                    | shared/clock.h                           | platform/time_sync.h              |

  @test=tools/test_check_layers.py:LayoutTest.test_stage_4_moved_tests_follow_their_code
  Scenario Outline: Testy czystych funkcji idą za nimi do pliku testów nowego modułu
    Given katalog test/ po etapie 4
    When szukam definicji i wywołania RUN_TEST testu <test>
    Then oba są w test/<nowy_plik>, a test/<stary_plik> już nie istnieje

    Examples:
      | test                                               | nowy_plik                         | stary_plik                        |
      | test_parse_credentials_json_valid                  | test_admin/settings_form_test.cpp | test_admin/config_portal_test.cpp |
      | test_parse_credentials_json_rejects_oversized_body | test_admin/settings_form_test.cpp | test_admin/config_portal_test.cpp |
      | test_valid_float_string_rejects_garbage            | test_admin/settings_form_test.cpp | test_admin/config_portal_test.cpp |
      | test_valid_unsigned_int_string_rejects_non_digits  | test_admin/settings_form_test.cpp | test_admin/config_portal_test.cpp |
      | test_format_date_leap_year_after_feb29             | test_platform/clock_test.cpp      | test_platform/time_sync_test.cpp  |
      | test_format_time_pads_single_digits                | test_platform/clock_test.cpp      | test_platform/time_sync_test.cpp  |
      | test_minute_of_changes_at_minute_boundary          | test_platform/clock_test.cpp      | test_platform/time_sync_test.cpp  |

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_net_modules_include_the_core0_modules
  Scenario: Nowe adaptery rdzenia 0 są sprawdzane jak moduły, z których wyszły
    Given lista modułów rdzenia 0 w tools/check_comments.py (NET_MODULES)
    When czytam ją po etapie 4
    Then zawiera web_server, opensky_http, hexdb_sources, lookup_store_nvs, settings_nvs, github_http, ota_flash i alert_sources
    And nie zawiera już config_portal (pliku o tej nazwie nie ma)

  @test=tools/test_check_layers.py:LayoutTest.test_board_config_headers_live_in_the_display_context
  Scenario: Konfiguracja LovyanGFX i kalibracja dotyku leżą w kontekście display, nie w include/
    Given drzewo repozytorium po etapie 4
    When szukam nagłówków projektu poza src/ i test/
    Then include/ nie ma żadnego pliku .h ani .hpp
    And klasa LGFX jest w src/display/adapters/lgfx/LGFX_CYD.hpp, a stałe kalibracji dotyku w src/display/core/touch_calibration.h
    And LGFX_CYD.hpp dołącza "display/core/touch_calibration.h" — ten sam plik, który czyta czyste mapRawTouch()

  @test=tools/test_check_layers.py:ClassifyTest.test_include_dir_headers_are_outside_any_layer
  Scenario: Nagłówek w include/ nie należy już do żadnej warstwy
    Given ścieżki include/LGFX_CYD.hpp i include/x.h
    When skrypt klasyfikuje pliki
    Then żadna nie należy do kontekstu ani warstwy (check_layers nie ma już wyjątków dla include/)
    But src/display/adapters/lgfx/LGFX_CYD.hpp należy do display/adapters, a src/display/core/touch_calibration.h do display/core

  @test=tools/test_check_layers.py:LayoutTest.test_stage_4_removes_sdk_and_adapter_findings_from_core
  Scenario Outline: Raport warstw nie zgłasza już SDK, adapterów ani platform/ dołączanych z core/ i z testów
    Given drzewo repozytorium po etapie 4
    When uruchamiam check_layers
    Then żadne zgłoszenie z warstwy <z> nie wskazuje warstwy <cel>

    Examples:
      | z               | cel                 |
      | display/core    | display/adapters    |
      | display/core    | sdk                 |
      | display/core    | platform            |
      | admin/core      | sdk                 |
      | admin/core      | admin/adapters      |
      | admin/core      | platform            |
      | enrichment/core | sdk                 |
      | enrichment/core | enrichment/adapters |
      | opensky/core    | sdk                 |
      | opensky/core    | platform            |
      | opensky/core    | admin/core          |
      | test            | admin/adapters      |

  @test=test_admin/settings_form_test.cpp:test_valid_float_string_rejects_garbage
  @test=test_admin/settings_form_test.cpp:test_parse_credentials_json_rejects_oversized_body
  Scenario: Walidacja liczb z formularza i plik poświadczeń OpenSky są czystą logiką admin/core, testowaną bez adaptera
    Given admin/core/settings_form z isValidFloatString, isValidUnsignedIntString i parseOpenSkyCredentialsJson
    When test_admin sprawdza je, dołączając tylko admin/core/settings_form.h
    Then "", "abc", "12.3.4", "12abc", "12 34" i " " nie są poprawną liczbą
    And plik poświadczeń dłuższy niż 4096 bajtów jest odrzucony bez parsowania (ok = false)

  @test=test_opensky/opensky_client_test.cpp:test_extract_aircraft_from_states_matches_parse_states_response
  @test=test_opensky/opensky_client_test.cpp:test_parse_states_response_valid
  Scenario: Odczyt /states/all na płytce i w testach mapuje pola tą samą funkcją
    Given tablica "states" z dwoma wektorami stanu, w tym callsign ze spacjami na końcu
    When adapter OpenSky (strumień HTTP) zamienia ją na samoloty przez extractAircraftFromStates(), a test przez parseStatesResponse()
    Then obie drogi dają te same rekordy: icao24, callsign bez spacji, pozycja, wysokość, „na ziemi”, prędkość, kurs i prędkość pionowa

  @test=test_platform/clock_test.cpp:test_format_date_leap_year_after_feb29
  @test=test_platform/clock_test.cpp:test_minute_of_changes_at_minute_boundary
  Scenario: Datę i godzinę formatuje wspólne jądro (shared/clock), a synchronizacja NTP zostaje w platform/
    Given epoka już przesunięta do czasu lokalnego (na płytce: timeSyncNowLocal() z platform/time_sync)
    When nagłówek i panel ustawień formatują ją funkcjami formatDate(), formatTime() i minuteOf() z shared/clock
    Then 1583020800 to „01.03.2020”, a minuteOf() zmienia się dokładnie na granicy minuty

  @manual
  Scenario: Etap 4 nie zmienia liczby testów ani firmware'u
    Given na gałęzi przed etapem 4 (koniec etapu 3, 4c58782): przypadki w 6 grupach pio test -e native (admin 258, airspace 71, display 312, enrichment 80, opensky 73, platform 83 = 877), RUN_TEST( 877, firmware.bin 1 345 520 B (9320f6f zbudowany z -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables; zmienione w kroku 11b, wcześniej 1 477 568 B), „Flash: used” / „RAM: used” (62 044 B) i lista bibliotek z pio run -e cyd
    When mierzę to samo po każdym kroku i na końcu etapu (po usunięciu .pio/build/native i .pio/build/cyd)
    Then wszystkie grupy są zielone, a przypadków jest razem 878: admin 258, airspace 71, display 312, enrichment 80, opensky 74, platform 83 (testy config_portal i time_sync przeszły do settings_form i clock w tych samych grupach, 1 nowy: wspólna funkcja mapująca stany OpenSky)
    And RUN_TEST( jest 878, firmware.bin na końcu (z tymi flagami) różni się od 1 345 520 B (zmienione w kroku 11b) o najwyżej 1024 bajty, RAM bez zmian, lista bibliotek ta sama
    And grep -rl "ifdef ARDUINO" src --include=*.cpp zwraca tylko pliki w src/platform/ i src/*/adapters/
    And python tools/check_layers.py zgłasza dokładnie 14 naruszeń: 47 z etapu 3 minus 38 (SDK, adaptery i platform/ dołączane z core/, LGFX_CYD.hpp w nagłówkach ekranów, config_portal_test → admin/adapters, release_check.h → config_portal.h) plus 5 (service_screen_draw.cpp, backlight_ldr.cpp i opensky_http.cpp → admin/core, alert_sources.cpp → display/core i opensky/core)
    And python tools/rewrite_includes.py --check kończy się kodem 0, python tools/check_comments.py --strict nic nie zgłasza, python tools/check_bdd.py 0 problemów

  @manual
  Scenario: Płytka po każdym kroku etapu 4 działa jak przedtem, z aktualizacją OTA i zapisem ustawień
    Given firmware z każdego kroku wgrany przez USB (COM3, pio run -e cyd -t upload, za zgodą właściciela) i pio device monitor -b 115200 przez 2–3 minuty
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then log szeregowy nie pokazuje paniki ani restartu poza startem, a płytka odpowiada pod cyd-sky.local
    And /screenshot.bmp?screen=0, 1 i 2 pokazują LOTY, TERAZ z odliczaniem i RADAR jak po etapie 3: polskie litery, ikony faz, strzałka kierunku, zakładki, nagłówek z datą, zegarem i kropką zdrowia
    And /perf.json: wolnych przejść pętli UI 0, sterta i największy blok w zakresie z etapu 3 (140–146 KB / 53–70 KB), bez restartu
    And krok, który rusza dotyk albo jasność, sprawdza je na płytce: zakładki przełączają ekrany, strona tabeli się zmienia, /light.json podaje odczyt czujnika i jasność
    And na końcu etapu aktualizacja OTA przez WiFi (POST /update z firmware.bin gałęzi) pokazuje ekran aktualizacji na wyświetlaczu, kończy się HTTP 200, a nowy obraz zostaje oznaczony jako ważny
    And zapis ustawień w panelu (zmiana np. interwału auto-cycle i z powrotem) przetrwa restart, szerokość „abc” jest odrzucona z komunikatem, a plik poświadczeń z błędnym JSON-em nie zmienia zapisanych poświadczeń
    And „Sprawdź teraz” na karcie Firmware łączy się z GitHubem (zweryfikowany TLS) bez błędu pamięci

  # ---- etap 5: warstwa services/ (§6) — automaty stanów z main.cpp i formularz z POST /save, z testami

  @test=tools/test_check_layers.py:LayoutTest.test_main_cpp_is_under_250_lines
  Scenario: main.cpp ma mniej niż 250 linii (§8 pkt 5, §10 pkt 3)
    Given src/main/main.cpp po etapie 5
    When liczę jego linie
    Then jest ich mniej niż 250 (przed etapem 909)

  @test=tools/test_check_layers.py:LayoutTest.test_main_cpp_keeps_no_flow_state
  Scenario: main.cpp nie trzyma stanu automatów w zmiennych static
    Given kod (bez komentarzy) src/main/main.cpp
    When szukam deklaracji static zmiennych typów prostych (bool, int, long, uint*_t, int*_t, size_t, std::string) — w zasięgu pliku i w funkcjach
    Then nie ma żadnej poza static constexpr / static const i numerem ostatniej odebranej migawki NetState (netSeen)
    And stan ekranów, timerów i pytań leży w obiektach typów z src/display/services/

  @test=tools/test_check_layers.py:LayoutTest.test_stage_5_flows_left_main_cpp
  Scenario Outline: Automat z main.cpp ma nowy dom w display/services/ i nie jest już zdefiniowany w main.cpp
    Given drzewo src/ po etapie 5
    When szukam definicji funkcji <dziś> w src/main/main.cpp i deklaracji <nowy_symbol> w nagłówkach
    Then main.cpp nie definiuje <dziś>
    And <nowy_symbol> ma dokładnie jedną deklarację, w src/<nowy_nagłówek>

    Examples:
      | dziś                  | nowy_symbol          | nowy_nagłówek                                |
      | handleTap             | routeTap             | display/services/ui_flow.h                   |
      | clampPage             | TablePager           | display/services/ui_flow.h                   |
      | maybeShowUpdatePrompt | UpdatePromptFlow     | display/services/update_prompt_flow.h        |
      | serviceUpdatePrompt   | UpdatePromptFlow     | display/services/update_prompt_flow.h        |
      | serviceOtaScreen      | OtaScreenFlow        | display/services/ota_screen_flow.h           |
      | serviceAlertScreen    | AlertScreenFlow      | display/services/alert_screen_flow.h         |
      | offerWebPasswordReset | PasswordResetFlow    | display/services/password_reset_flow.h       |
      | screenshotPrepare     | screenshotPrepareTarget | display/services/screenshot_bridge.h      |
      | serviceScreenshot     | screenshotUiPass     | display/services/screenshot_bridge.h         |
      | perfLoopTick          | UiPerfMeter          | display/services/ui_perf.h                   |

  @test=tools/test_check_layers.py:LayoutTest.test_web_server_save_route_has_no_form_logic
  Scenario: Trasa POST /save tylko zbiera pola, zapisuje i odpowiada — logika formularza jest w admin/core/settings_form
    Given funkcja handleSave() w src/admin/adapters/web_server.cpp
    When czytam jej ciało
    Then ma najwyżej 30 linii i nie woła isValidFloatString, isValidUnsignedIntString, decidePasswordChange, parseClockMinutes, nightModeFromCode, languageFromCode, strtof, strtoul ani atoi
    And woła applySettingsForm() z src/admin/core/settings_form.h

  @test=tools/test_check_layers.py:LayoutTest.test_stage_5_services_have_their_tests
  Scenario Outline: Każdy nowy moduł z §6 ma plik testów w grupie swojego kontekstu, zarejestrowany w main.cpp grupy
    Given drzewo src/ i test/ po etapie 5
    When szukam src/<moduł>.h, test/<plik_testów> i wywołania run_<nazwa>_tests() w main.cpp grupy
    Then wszystkie trzy istnieją, a plik testów ma co najmniej 3 wywołania RUN_TEST

    Examples:
      | moduł                                  | plik_testów                                  | nazwa               |
      | display/services/ui_flow               | test_display/ui_flow_test.cpp                | ui_flow             |
      | display/services/update_prompt_flow    | test_display/update_prompt_flow_test.cpp     | update_prompt_flow  |
      | display/services/ota_screen_flow       | test_display/ota_screen_flow_test.cpp        | ota_screen_flow     |
      | display/services/alert_screen_flow     | test_display/alert_screen_flow_test.cpp      | alert_screen_flow   |
      | display/services/password_reset_flow   | test_display/password_reset_flow_test.cpp    | password_reset_flow |
      | display/services/screenshot_bridge     | test_display/screenshot_bridge_test.cpp      | screenshot_bridge   |
      | display/services/ui_perf               | test_display/ui_perf_test.cpp                | ui_perf             |
      | admin/core/settings_form               | test_admin/settings_form_test.cpp            | settings_form       |

  @test=tools/test_check_layers.py:LayoutTest.test_stage_5_moved_tests_follow_their_code
  Scenario Outline: Testy mostu zrzutu ekranu idą za kodem do test_display, testy BMP zostają w test_admin
    Given katalog test/ po etapie 5
    When szukam definicji i wywołania RUN_TEST testu <test>
    Then oba są w test/<nowy_plik>, a w test/<stary_plik> nie ma ani definicji, ani wywołania

    Examples:
      | test                                                       | nowy_plik                               | stary_plik                         |
      | test_bridge_normal_capture_fills_every_band                | test_display/screenshot_bridge_test.cpp | test_admin/screenshot_test.cpp     |
      | test_bridge_concurrent_random_timeouts_never_touch_freed_buffers | test_display/screenshot_bridge_test.cpp | test_admin/screenshot_test.cpp |
      | test_bridge_idle_rule_across_millis_rollover               | test_display/screenshot_bridge_test.cpp | test_admin/screenshot_test.cpp     |
      | test_screen_returns_to_previous_after_capture              | test_display/screenshot_bridge_test.cpp | test_admin/screenshot_test.cpp     |
      | test_return_not_triggered_without_prepare                  | test_display/screenshot_bridge_test.cpp | test_admin/screenshot_test.cpp     |
      | test_header_fields                                         | test_admin/screenshot_test.cpp          | test_display/screenshot_bridge_test.cpp |

  # ---- etap 5: ui_flow — dotyk, strony tabeli, zmiany ekranu, zegar, auto-cycle, stan źródła (dziś handleTap, clampPage, onScreenChanged, updateShownHealth, część loop())

  @test=test_display/ui_flow_test.cpp:test_route_tap_bottom_nav_jumps_to_its_screen
  Scenario Outline: Dotknięcie zakładki na dole przełącza na jej ekran z każdego ekranu
    Given aktywny ekran <ekran> i ekran 320 × 240
    When routeTap() dostaje dotknięcie w (<x>, 225)
    Then wynik to JumpTo z celem <cel>

    Examples:
      | ekran | x   | cel |
      | 0     | 50  | 0   |
      | 0     | 160 | 1   |
      | 1     | 300 | 2   |
      | 2     | 10  | 0   |

  @test=test_display/ui_flow_test.cpp:test_route_tap_alert_badge_comes_first
  Scenario: Dotknięcie „!” w nagłówku otwiera ekran ostrzeżeń, zanim zadziała cokolwiek innego
    Given dom ustawiony, nagłówek z narysowaną odznaką „!” i ekran LOTY na drugiej stronie tabeli
    When routeTap() dostaje dotknięcie w strefie odznaki (także 8 px za nią i do y = 24)
    Then wynik to OpenAlerts, a nie zmiana strony ani ekranu

  @test=test_display/ui_flow_test.cpp:test_route_tap_badge_zone_without_badge_or_home_falls_through
  Scenario: Strefa odznaki bez odznaki albo bez ustawionego domu nie otwiera ostrzeżeń
    Given nagłówek bez odznaki (alert_x < 0) albo dom jeszcze nieustawiony
    When routeTap() dostaje dotknięcie w tym samym miejscu
    Then wynik nie jest OpenAlerts

  @test=test_display/ui_flow_test.cpp:test_route_tap_pages_table_within_bounds
  Scenario Outline: Pas pierwszego i ostatniego wiersza tabeli zmienia stronę, ale nie poza pierwszą i ostatnią
    Given ekran LOTY, <stron> stron(y) tabeli i aktywna strona <strona>
    When routeTap() dostaje dotknięcie w pasie <pas> (górny: od y = 116, wysokość wiersza; dolny: tuż nad paskiem zakładek)
    Then wynik to <wynik>

    Examples:
      | stron | strona | pas    | wynik    |
      | 2     | 0      | górny  | None     |
      | 2     | 1      | górny  | PagePrev |
      | 2     | 0      | dolny  | PageNext |
      | 2     | 1      | dolny  | None     |
      | 0     | 0      | dolny  | None     |
      | 1     | 0      | dolny  | None     |

  @test=test_display/ui_flow_test.cpp:test_route_tap_content_of_flight_and_radar_does_nothing
  Scenario: Na ekranach TERAZ i RADAR dotknięcie treści nic nie robi
    Given aktywny ekran TERAZ albo RADAR
    When routeTap() dostaje dotknięcie w pasie stronicowania tabeli albo na środku ekranu
    Then wynik to None

  @test=test_display/ui_flow_test.cpp:test_table_pager_clamps_after_shorter_list
  Scenario: Nowy poll z krótszą listą cofa stronę tabeli na ostatnią istniejącą
    Given TablePager na stronie 3
    When nowy poll daje 2 strony, a potem pustą listę
    Then strona to najpierw 1, potem 0

  @test=test_display/ui_flow_test.cpp:test_screen_change_resets_page_and_auto_cycle_timer
  Scenario: Każda zmiana ekranu zeruje stronę tabeli i timer auto-cycle i zleca przerysowanie
    Given strona tabeli 2 i ostatnia zmiana ekranu 40 s temu
    When ekran zmienia się z dotyku, z auto-cycle albo ze zrzutu ekranu
    Then strona to 0, timer auto-cycle liczy od teraz, a zdarzenia przerysowania mają screenChanged

  @test=test_display/ui_flow_test.cpp:test_screen_change_persists_only_a_new_screen
  Scenario: Zapis ostatniego ekranu: dotyk od razu, auto-cycle najwyżej co 10 min, zrzut nigdy, ten sam ekran wcale
    Given ostatni zapisany ekran 0, zapisany 1 min temu
    When ekran zmienia się na 1 z dotyku, potem na 2 z auto-cycle, potem na 0 ze zrzutu, a po 10 min na 2 z auto-cycle
    Then zapis jest zlecony dla dotyku i dla auto-cycle po 10 min, nie dla auto-cycle po 1 min ani dla zrzutu
    And powrót na już zapisany ekran nie zleca zapisu

  @test=test_display/ui_flow_test.cpp:test_minute_watch_first_sync_and_rollover
  Scenario: Zegar w nagłówku odświeża się przy pierwszej synchronizacji NTP i przy zmianie minuty, nie częściej
    Given zegar jeszcze niezsynchronizowany (minuta -1) po pierwszym przejściu
    When przychodzi synchronizacja, potem przejścia w tej samej minucie, potem w następnej
    Then minuteChanged jest ustawione przy synchronizacji i przy nowej minucie, a w tej samej minucie nie

  @test=test_display/ui_flow_test.cpp:test_auto_cycle_never_on_dark_screen_or_without_home
  Scenario: Auto-cycle nie przełącza wygaszonego ekranu, ekranu bez ustawionego domu ani gdy jest wyłączony
    Given minął interwał auto-cycle i nic nie wstrzymuje przełączenia
    When auto-cycle jest wyłączony, dom nieustawiony albo ekran wygaszony nocą
    Then przełączenia nie ma
    But z włączonym auto-cycle, ustawionym domem i świecącym ekranem przełączenie jest

  @test=test_display/ui_flow_test.cpp:test_shown_health_network_error_after_silence_marks_empty_screen
  Scenario: Po 60 s ciszy rdzenia 0 nagłówek pokazuje błąd sieci, a pusty ekran mówi dlaczego
    Given stan źródła Ok i ostatnie bicie rdzenia 0 sprzed 60 001 ms
    When UI liczy stan pokazywany w nagłówku
    Then to NetworkError ze zdarzeniem healthChanged
    And przy pustej liście lotów jest też newPoll (treść pokazuje powód), a przy niepustej nie

  # ---- etap 5: update_prompt_flow — pytanie „nowa wersja — zainstalować?” (dziś maybeShowUpdatePrompt, serviceUpdatePrompt, 7 zmiennych static)

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_opens_once_per_boot
  Scenario: Pytanie o nową wersję pojawia się najwyżej raz na uruchomienie
    Given brama shouldShowUpdatePrompt() przepuszcza (wersja dostępna, nie pominięta, ekran świeci)
    When pytanie się otwiera, zostaje zamknięte i brama znów przepuszcza
    Then drugi raz się nie otwiera (alreadyPromptedThisBoot)

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_ignores_tap_before_release_or_arm_time
  Scenario: Palec, który był na ekranie przy pojawieniu się pytania, albo dotknięcie przed upływem 300 ms nie odpowiada
    Given pytanie otwarte przy dotkniętym ekranie
    When palec trzyma dalej na TAK, a potem — po puszczeniu — dotyka TAK po 299 ms od otwarcia
    Then nie ma odpowiedzi
    But dotknięcie TAK po puszczeniu i po 300 ms jest odpowiedzią

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_yes_installs_no_skips_with_full_redraw
  Scenario Outline: TAK wysyła InstallTag, NIE wysyła SkipTag z tagiem pytania, a ekrany wracają w całości
    Given otwarte pytanie o v1.3.0, palec puszczony, minęło 300 ms
    When dotknięcie trafia w przycisk <przycisk>
    Then pytanie się zamyka, komenda do rdzenia 0 to <komenda> z tagiem v1.3.0, a zdarzenia przerysowania mają firstDraw

    Examples:
      | przycisk | komenda    |
      | TAK      | InstallTag |
      | NIE      | SkipTag    |

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_times_out_at_exactly_30_s
  Scenario: Bez odpowiedzi pytanie znika dokładnie po 30 s, bez komendy
    Given otwarte pytanie
    When mija 29 999 ms, a potem 30 000 ms bez dotknięcia
    Then po 29 999 ms jest nadal otwarte, po 30 000 ms zamknięte z firstDraw i bez komendy (zapyta po następnym uruchomieniu)

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_timer_redraws_every_250_ms
  Scenario: Pasek czasu pytania odświeża się co 250 ms i UI budzi się na ten moment
    Given pytanie otwarte w chwili 0
    When przejścia przychodzą w 249, 250 i 499 ms
    Then pasek jest rysowany w 250 ms z pozostałym czasem 29 750 ms, w 249 i 499 ms nie
    And czas do następnego budzenia po 250 ms to 250 ms

  @test=test_display/update_prompt_flow_test.cpp:test_prompt_closed_by_panel_install_without_redraw
  Scenario: Instalacja zlecona z panelu w trakcie pytania zamyka je bez przerysowania ekranów (ekran OTA przejmuje)
    Given otwarte pytanie
    When NetState zgłasza installPending
    Then pytanie się zamyka bez komendy i bez firstDraw

  # ---- etap 5: ota_screen_flow — ekran aktualizacji rysowany z NetState::ota (dziś serviceOtaScreen)

  @test=test_display/ota_screen_flow_test.cpp:test_ota_first_progress_draws_screen_then_only_bar
  Scenario: Pierwszy postęp aktualizacji rysuje ekran OTA w pełnej jasności, kolejne tylko pasek postępu
    Given OtaProgress Running, written 0, nowa sekwencja
    When przychodzi ta sekwencja, a potem kolejna z written 400 000 z 1 300 000
    Then pierwsze przejście zleca pełną jasność, ekran OTA i pasek; drugie tylko pasek z 400 000 / 1 300 000

  @test=test_display/ota_screen_flow_test.cpp:test_ota_owns_display_while_running_done_or_restarting
  Scenario Outline: Ekran OTA trzyma wyświetlacz w trakcie, po sukcesie i przed restartem, a zamyka ekran ostrzeżeń
    Given OtaProgress w fazie <faza> i restartPending <restart>
    When UI wykonuje przejście
    Then ekran OTA trzyma wyświetlacz: <trzyma>, a gdy trzyma, ekran ostrzeżeń jest zamknięty

    Examples:
      | faza    | restart | trzyma |
      | Running | false   | tak    |
      | Done    | false   | tak    |
      | Idle    | true    | tak    |
      | Idle    | false   | nie    |

  @test=test_display/ota_screen_flow_test.cpp:test_ota_failure_shown_5_s_then_back_to_screens
  Scenario: Nieudana aktualizacja zostaje na ekranie dokładnie 5 s, potem wracają zwykłe ekrany
    Given przejście z nową sekwencją w fazie Failed w chwili t (nieparzystej, np. 10 001)
    When przychodzą przejścia w t + 4 999 i t + 5 000 ms
    Then w t rysuje się wynik „nieudana”, w t + 4 999 ekran OTA dalej trzyma wyświetlacz i jasność, w t + 5 000 już nie, z firstDraw

  @test=test_display/ota_screen_flow_test.cpp:test_ota_failure_timer_survives_millis_wrap
  Scenario: 5 s po błędzie liczy się poprawnie także przez przekręcenie millis()
    Given błąd aktualizacji w chwili 0xFFFFF001 (nieparzystej — znacznik czasu ma ustawiony najniższy bit, żeby 0 znaczyło „brak błędu”, jak dziś)
    When mijają 4 999, a potem 5 000 ms
    Then ekran wraca po 5 000 ms, nie wcześniej i nie później

  @test=test_display/ota_screen_flow_test.cpp:test_ota_retry_after_failure_redraws_screen
  Scenario: Ponowna próba po błędzie rysuje ekran OTA od nowa
    Given błąd aktualizacji pokazany i 5 s minęło
    When przychodzi nowa sekwencja Running z written 0
    Then przejście zleca znów ekran OTA (z pełną jasnością) i pasek

  @test=test_display/ota_screen_flow_test.cpp:test_ota_same_seq_draws_nothing
  Scenario: Bez nowej sekwencji postępu ekran OTA niczego nie rysuje
    Given ekran OTA już narysowany dla sekwencji 7 w fazie Running
    When kolejne przejścia widzą tę samą sekwencję 7
    Then nic nie jest rysowane, a ekran OTA dalej trzyma wyświetlacz

  # ---- etap 5: alert_screen_flow — ekran ostrzeżeń za „!” (dziś openAlertMessage, closeAlertMessage, serviceAlertScreen)

  @test=test_display/alert_screen_flow_test.cpp:test_alert_flow_redraws_list_when_alerts_change
  Scenario: Zmiana listy ostrzeżeń przy otwartym ekranie przerysowuje listę, a odliczanie biegnie dalej
    Given ekran ostrzeżeń otwarty w chwili 0 z jednym ostrzeżeniem
    When po 3 s przychodzi drugie ostrzeżenie
    Then przejście zleca narysowanie całej listy z 12 s na przycisku ZAMKNIJ, nie od nowa 15 s

  @test=test_display/alert_screen_flow_test.cpp:test_alert_flow_close_returns_with_full_redraw
  Scenario: Zamknięcie ekranu ostrzeżeń wraca do tego samego ekranu i strony z pełnym przerysowaniem i świeżym auto-cycle
    Given ekran ostrzeżeń otwarty nad ekranem LOTY, strona 1
    When upływa 15 s albo pada świeże dotknięcie ZAMKNIJ
    Then ekran się zamyka, zdarzenia mają firstDraw, timer auto-cycle liczy od teraz, a ekran i strona są te same
    And wynik każe połknąć bieżący dotyk (swallowTouch) — ani dotknięcia przy otwartym ekranie, ani to, które go zamknęło, nie są dotknięciami dla ekranu pod spodem

  @test=test_display/alert_screen_flow_test.cpp:test_alert_flow_closed_by_ota_screen
  Scenario: Ekran aktualizacji zamyka ekran ostrzeżeń bez przerysowania (ekran OTA ma pierwszeństwo)
    Given otwarty ekran ostrzeżeń
    When ekran OTA przejmuje wyświetlacz
    Then ekran ostrzeżeń jest zamknięty i nie zleca rysowania

  # ---- etap 5: password_reset_flow — reset zapomnianego hasła panelu przy starcie (dziś offerWebPasswordReset)

  @test=test_display/password_reset_flow_test.cpp:test_reset_offered_only_with_password_and_finger
  Scenario Outline: Reset hasła jest proponowany tylko z ustawionym hasłem i palcem na ekranie przy starcie
    Given hasło panelu <hasło> i ekran przy starcie <dotyk>
    When setup() pyta, czy zaproponować reset
    Then odpowiedź to <propozycja>

    Examples:
      | hasło      | dotyk       | propozycja |
      | ustawione  | dotknięty   | tak        |
      | ustawione  | niedotknięty| nie        |
      | puste      | dotknięty   | nie        |

  @test=test_display/password_reset_flow_test.cpp:test_reset_hold_5_s_then_confirm_after_release
  Scenario: Przytrzymanie palca 5 s prowadzi do pytania TAK / NIE dopiero po puszczeniu palca
    Given reset rozpoczęty w chwili 1 000 z palcem na ekranie
    When palec trzyma do 6 000 ms i dłużej, a potem puszcza
    Then do 5 999 ms rysuje się pasek trzymania, w 6 000 ms trzymanie się kończy
    And pytanie TAK / NIE pojawia się dopiero, gdy od ostatniego odczytu dotyku minęło 300 ms, nie wcześniej i nie w trakcie trzymania

  @test=test_display/password_reset_flow_test.cpp:test_reset_release_over_300_ms_cancels_glitch_does_not
  Scenario: Puszczenie palca na dłużej niż 300 ms przerywa reset, krótszy zanik odczytu nie
    Given reset rozpoczęty z palcem na ekranie
    When odczyt dotyku znika na 300 ms i wraca, a później znika na 301 ms
    Then po zaniku 300 ms trzymanie trwa dalej, po 301 ms reset się kończy czystym ekranem, bez zmiany hasła

  @test=test_display/password_reset_flow_test.cpp:test_reset_confirm_yes_clears_once_no_and_timeout_keep
  Scenario Outline: TAK czyści hasło dokładnie raz, NIE i brak odpowiedzi przez 15 s je zostawiają
    Given pytanie TAK / NIE po przytrzymaniu
    When <co_się_dzieje>
    Then czyszczenie hasła jest zlecone <ile> raz(y), a reset się kończy <ekran>

    Examples:
      | co_się_dzieje                               | ile | ekran                                            |
      | świeże dotknięcie TAK                        | 1   | ekranem „hasło usunięte” na 1,5 s, potem czystym |
      | świeże dotknięcie NIE                        | 0   | czystym ekranem                                  |
      | brak dotknięcia przez 15 000 ms              | 0   | czystym ekranem                                  |
      | dotknięcie poza przyciskami, potem nic przez 15 000 ms | 0 | czystym ekranem                        |

  # ---- etap 5: screenshot_bridge — zrzut ekranu, strona UI (dziś shot::, screenshotPrepare, serviceScreenshot)

  @test=test_display/screenshot_bridge_test.cpp:test_screenshot_prepare_target_screens_and_demos
  Scenario Outline: ?screen=N wybiera ekran albo jeden z dwóch ekranów demo
    Given 3 ekrany (LOTY, TERAZ, RADAR)
    When zlecenie zrzutu prosi o ekran <n>
    Then przygotowanie to <cel>

    Examples:
      | n  | cel                                          |
      | -1 | bieżący ekran, bez przełączenia              |
      | 0  | ekran 0                                      |
      | 2  | ekran 2                                      |
      | 3  | demo pytania o aktualizację                  |
      | 4  | demo ekranu ostrzeżeń                        |

  @test=test_display/screenshot_bridge_test.cpp:test_screenshot_ui_pass_returns_to_previous_screen
  Scenario: Przejście UI obsługuje zrzut tą samą funkcją na płytce i w testach i wraca na poprzedni ekran
    Given ekran LOTY i zlecenie zrzutu ekranu RADAR z dwoma pasami
    When przejścia UI wołają screenshotUiPass() z mostem na std::mutex (test) — na płytce na SpinLock, z tą samą funkcją
    Then pierwsze przejście woła przygotowanie z ekranem 2 i zgłasza trwający zrzut, kolejne wołają odczyt pasów od dolnego wiersza pasa
    And po end() pierwsze przejście bez zrzutu zwraca ekran 0 do przywrócenia (dokładnie raz), a następne już nic

  # ---- etap 5: ui_perf — przerwy pętli UI do /perf.json (dziś perfLoopTick)

  @test=test_display/ui_perf_test.cpp:test_ui_perf_gap_subtracts_planned_sleep
  Scenario: Przerwa pętli UI to czas od poprzedniego przejścia minus zaplanowany sen
    Given przejście w chwili 1 000 ms, które zaplanowało sen 20 ms
    When następne przejście zaczyna się w 1 035 ms, a kolejne (sen 1 000 ms) wcześniej niż po 1 000 ms
    Then przerwy to 15 ms i 0 ms (nigdy ujemna), a pierwsze przejście po starcie nie jest liczone

  @test=test_display/ui_perf_test.cpp:test_ui_perf_slow_pass_from_250_ms
  Scenario: Przerwa od 250 ms liczy się jako wolne przejście, 249 ms nie
    Given licznik wolnych przejść 0
    When przychodzą przerwy 249 ms i 250 ms
    Then wolnych przejść jest 1, a największa przerwa od startu to 250 ms

  @test=test_display/ui_perf_test.cpp:test_ui_perf_window_closes_every_60_s
  Scenario: Okno statystyk zamyka się co 60 s i oddaje UiPerfShare rdzeniowi 0
    Given pierwsze przejście w chwili 1 000 (także w 0xFFFF0000, tuż przed przekręceniem millis())
    When przejścia przychodzą do 60 999 ms, a potem w 61 000 ms (60 000 ms po pierwszym)
    Then okno zamyka się dopiero 60 000 ms po pierwszym przejściu, UiPerfShare ma liczbę przejść, maksimum i przedziały okna, a następne okno zaczyna się od zera

  # ---- etap 5: settings_form — formularz ustawień (dziś handleSave w web_server)

  @test=test_admin/settings_form_test.cpp:test_form_valid_submit_sets_every_field_and_home_configured
  Scenario: Poprawny formularz ustawia wszystkie pola i oznacza dom jako ustawiony
    Given zapisane ustawienia i formularz z każdym polem (szerokość, długość, zasięg, odświeżanie, auto-cycle, jasność, noc, języki, poświadczenia)
    When applySettingsForm() go stosuje
    Then każde pole ma wartość z formularza, home_configured = true, brak odrzuconych pól, komunikat „Zapisano.”

  @test=test_admin/settings_form_test.cpp:test_form_rejects_malformed_number_and_keeps_old_value
  Scenario Outline: Błędna wartość pola zostaje odrzucona, a pole zachowuje poprzednią wartość
    Given zapisane ustawienia
    When formularz ma <pole> = "<wartość>"
    Then pole w ustawieniach się nie zmienia, odrzucone pola to [<etykieta>], komunikat to SavedWithInvalid

    Examples:
      | pole                | wartość  | etykieta          |
      | lat                 | abc      | Latitude          |
      | lon                 | 12.3.4   | Longitude         |
      | radius              |          | Radius            |
      | poll_interval       | -5       | PollInterval      |
      | auto_cycle_interval | 10s      | AutoCycleInterval |
      | near_airport_km     | 1e       | NearAirport       |
      | climb_rate_mps      | x        | ClimbRate         |
      | bl_min              | 12345    | BrightnessMin     |
      | night_wake_s        | 3.5      | NightWake         |
      | night_from          | 25:00    | NightFrom         |

  @test=test_admin/settings_form_test.cpp:test_form_rejected_labels_in_form_order_in_panel_language
  Scenario: Kilka błędnych pól jest wymienionych w kolejności formularza, w języku panelu
    Given formularz z błędną szerokością, długością i hasłami, które się różnią
    When rejectedFieldsText() składa listę po polsku i po angielsku
    Then to „Szerokość geograficzna, Długość geograficzna, hasło panelu (wpisane hasła się różnią)” i „Latitude, Longitude, …” — w tej kolejności, rozdzielone „, ”

  @test=test_admin/settings_form_test.cpp:test_form_missing_checkbox_unchecked_missing_text_unchanged
  Scenario: Brak pola wyboru znaczy „odznaczone”, a brak pola tekstowego „bez zmian”
    Given zapisane ustawienia z włączonym auto-cycle, sprawdzaniem aktualizacji i pauzą nocą
    When formularz nie ma auto_cycle, update_check, night_pause ani lat
    Then te trzy opcje są wyłączone, a szerokość geograficzna się nie zmienia

  @test=test_admin/settings_form_test.cpp:test_form_empty_secret_keeps_saved_clear_removes_both
  Scenario: Puste pole sekretu OpenSky zachowuje zapisany sekret, a „wyczyść” usuwa id i sekret
    Given zapisane client id i sekret OpenSky
    When formularz ma nowe client_id i puste client_secret, a drugi formularz ma clear_credentials
    Then po pierwszym id jest nowe, a sekret ten sam; po drugim oba są puste

  @test=test_admin/settings_form_test.cpp:test_form_password_outcomes_and_messages
  Scenario Outline: Hasło panelu z formularza: komunikat i zmiana
    Given zapisane hasło panelu "stare-haslo"
    When formularz ma new_password "<nowe>", new_password2 "<powtórzone>" i clear_password <wyczyść>
    Then hasło to "<wynik>", a komunikat / odrzucone pole to <komunikat>

    Examples:
      | nowe         | powtórzone   | wyczyść | wynik        | komunikat        |
      |              |              | nie     | stare-haslo  | Saved            |
      | nowe-haslo-1 | nowe-haslo-1 | nie     | nowe-haslo-1 | PasswordChanged  |
      | nowe-haslo-1 | inne-haslo-2 | nie     | stare-haslo  | PasswordMismatch |
      | krotkie      | krotkie      | nie     | stare-haslo  | PasswordTooShort |
      | nowe-haslo-1 | nowe-haslo-1 | tak     |              | PasswordCleared  |

  @test=test_admin/settings_form_test.cpp:test_form_brightness_out_of_range_is_clamped
  Scenario: Liczby jasności i nocy poza zakresem są przycinane, a nie odrzucane
    Given formularz z bl_max 150, bl_min 0, night_pct 101 i night_wake_s 5
    When applySettingsForm() go stosuje
    Then jasność maks. to 100, min. 5, w nocy 100, wybudzenie 10 s — i żadne z tych pól nie jest odrzucone

  @test=test_admin/settings_form_test.cpp:test_form_args_first_value_wins
  Scenario: Pole wysłane dwa razy liczy się w pierwszej wersji, jak w WebServer::arg()
    Given FormArgs z lat = "52.1" i potem lat = "abc"
    When applySettingsForm() go stosuje
    Then szerokość to 52.1 i nic nie jest odrzucone

  # ---- etap 5: rozmiar, liczby testów, płytka

  @manual
  Scenario: Etap 5 nie zmienia zachowania, dodaje testy, a firmware rośnie najwyżej o ustalony budżet
    Given na gałęzi przed etapem 5 (v1.11.2, 8a82b39): przypadki w 6 grupach pio test -e native (admin 258, airspace 71, display 312, enrichment 80, opensky 74, platform 83 = 878), RUN_TEST( 878, firmware.bin 1 346 544 B, „Flash: used” 1 339 961, „RAM: used” 62 044 B, lista bibliotek, wc -l src/main/main.cpp = 909, src/admin/adapters/web_server.cpp = 834, check_layers 14
    When mierzę to samo po każdym kroku i na końcu etapu (po usunięciu .pio/build/native i .pio/build/cyd)
    Then wszystkie grupy są zielone, przypadków jest 878 + nowe testy z tests.md (19 testów mostu zrzutu przeszło z test_admin do test_display), RUN_TEST( rośnie o tyle samo
    And firmware.bin na końcu jest większy od 1 346 544 B najwyżej o budżet z planu (domyślnie 4 096 B), RAM statyczny najwyżej o 256 B, lista bibliotek ta sama
    And python tools/check_layers.py zgłasza dalej dokładnie te same 14 naruszeń, python tools/rewrite_includes.py --check kończy się kodem 0, python tools/check_comments.py --strict nic nie zgłasza, python tools/check_bdd.py 0 problemów

  @manual
  Scenario: Płytka po każdym kroku etapu 5 działa jak przedtem: dotyk, ekrany serwisowe, zrzuty, formularz
    Given firmware z każdego kroku wgrany (USB COM3 albo pio run -e cyd-ota -t upload, za zgodą właściciela) i pio device monitor -b 115200 przez 2–3 minuty
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then log szeregowy nie pokazuje paniki ani restartu poza startem, /screenshot.bmp?screen=0, 1, 2 pokazują LOTY, TERAZ z odliczaniem i RADAR jak po etapie 4, a ekran po zrzucie wraca na poprzedni
    And /screenshot.bmp?screen=3 i 4 pokazują demo pytania o aktualizację i ekranu ostrzeżeń, a potem wraca zwykły ekran
    And /perf.json po ponad 60 s ma okno UI z liczbą przejść, wolnych przejść 0, sterta i największy blok w zakresie etapu 4 (140–148 KB / 53–70 KB)
    And właściciel sprawdza dotyk: zakładki przełączają ekrany, pasy tabeli zmieniają stronę, „!” (gdy jest) otwiera ekran ostrzeżeń, który zamyka ZAMKNIJ albo 15 s; auto-cycle (gdy włączony) przełącza ekrany, a ostatni ekran wraca po restarcie
    And aktualizacja OTA przez WiFi pokazuje ekran aktualizacji z paskiem i kończy się nowym obrazem oznaczonym jako ważny; wysłanie pliku, który nie jest firmware'em, pokazuje „aktualizacja nieudana” przez 5 s i wraca do ekranów
    And z ustawionym hasłem panelu palec na ekranie przy włączeniu daje ekran trzymania, po 5 s pytanie TAK / NIE; NIE zostawia hasło
    And zapis formularza bez zmian zachowuje wartości, szerokość „abc” i długość „x” są odrzucone z komunikatem wymieniającym oba pola w tej kolejności, a zmiana interwału auto-cycle przetrwa restart

  # ---- etap 6: ustawienia per kontekst (§7) — Settings (do etapu 7 Config w config_store.h) składa się z obiektów wartości kontekstów

  @test=tools/test_check_layers.py:LayoutTest.test_config_is_built_from_context_objects
  Scenario Outline: Settings składa się z obiektów wartości kontekstów, a każdy obiekt leży w swoim kontekście
    Given src/admin/core/settings.h po etapie 7
    When czytam pola struct Settings i miejsce deklaracji ich typów
    Then Settings ma pole <pole> typu <obiekt> z nagłówka <nagłówek>, a w nim pola <pola>
    And Settings nie ma żadnego innego pola (żadnego z 28 dawnych pól płaskich)

    Examples:
      | pole       | obiekt             | nagłówek                              | pola                                                                                                                                                    |
      | home       | Home               | airspace/core/home.h                  | lat, lon, radius_deg, configured                                                                                                                        |
      | phase      | PhaseThresholds    | airspace/core/flight_phase.h          | near_airport_km, climb_rate_threshold_mps                                                                                                               |
      | opensky    | OpenSkyCredentials | opensky/core/opensky_settings.h       | client_id, client_secret                                                                                                                                |
      | poll       | PollSettings       | opensky/core/opensky_settings.h       | interval_s                                                                                                                                              |
      | display    | DisplaySettings    | display/core/display_settings.h       | language, last_screen                                                                                                                                   |
      | auto_cycle | AutoCycleSettings  | display/core/display_settings.h       | enabled, interval_s                                                                                                                                     |
      | backlight  | BacklightSettings  | display/core/backlight.h              | brightness_auto, brightness_fixed_pct, brightness_min_pct, brightness_max_pct, night_mode, night_from_min, night_to_min, night_pct, night_wake_s, night_pause_polling |
      | update     | UpdateSettings     | admin/core/settings.h                 | check_enabled, show_prerelease, skipped_tag                                                                                                             |
      | web        | WebSettings        | admin/core/settings.h                 | language, password                                                                                                                                      |

  @test=test_admin/settings_test.cpp:test_default_config_keeps_every_stage_5_default
  Scenario: Wartości domyślne ustawień są te same co przed etapem
    Given defaultSettings() po etapie 7
    When porównuję każde pole każdego obiektu z wartością domyślną z v1.11.3
    Then dom to (0, 0), zasięg 2,5°, nieskonfigurowany; poll co 15 s; auto-cycle wyłączony, co 15 s; lotnisko 15 km, wznoszenie 1,5 m/s; ekran i panel po polsku, ostatni ekran 0
    And sprawdzanie aktualizacji włączone, wersje rozwojowe wyłączone, bez pominiętej wersji i hasła; jasność automatyczna 10–100 %, stała 80 %, noc od zachodu do wschodu słońca (kDefaultNightMode = Sun; okno zegarowe 23:00–06:30), w nocy 10 %, wybudzenie 60 s, bez pauzy pollingu

  @test=test_admin/settings_test.cpp:test_sanitize_config_clamps_all_fields
  @test=test_admin/settings_test.cpp:test_sanitize_config_leaves_in_range_values_untouched
  Scenario: sanitizeSettings() przycina te same pola do tych samych granic, teraz w obiektach kontekstów
    Given ustawienia z wartościami poza zakresem w każdym obiekcie (szerokość 200°, zasięg 0, poll 1 s, auto-cycle 0 s, lotnisko 0 km, jasność 0 / 200 %, noc 2000 min, ekran 7, język 9)
    When sanitizeSettings() je porządkuje
    Then każde pole ma tę samą wartość co przed etapem (testy sprzed etapu, zmieniona tylko ścieżka pola, np. cfg.backlight.night_pct zamiast cfg.night_pct)
    And ustawienia w zakresie zostają nietknięte

  @test=tools/test_check_layers.py:LayoutTest.test_settings_nvs_keys_and_types_unchanged
  Scenario: Klucze NVS i ich typy są te same co przed etapem
    Given src/admin/adapters/settings_nvs.cpp po etapie 6
    When zbieram każde prefs.get…("klucz") i prefs.put…("klucz") z loadSettings(), saveSettings() i zapisów jednego klucza
    Then to dokładnie 28 kluczy z v1.11.3 (home_lat, home_lon, radius_deg, poll_s, os_id, os_secret, last_screen, auto_cyc, auto_cyc_s, near_km, climb_mps, home_cfg, lang, wlang, wpass, updchk, updpre, updskip, bl_auto, bl_fix, bl_min, bl_max, nt_mode, nt_from, nt_to, nt_pct, nt_wake, nt_poll) w przestrzeni "cydsky"
    And każdy klucz jest czytany i zapisywany tym samym typem co przed etapem (Float, UInt, String, Int, Bool, UChar, UShort)

  @test=test_admin/config_backup_test.cpp:test_backup_json_is_the_same_as_before_stage_6
  Scenario: Kopia zapasowa ma bajt w bajt ten sam JSON co przed etapem
    Given ustawienia z niedomyślną wartością w każdym polu, które trafia do kopii, i hasłem panelu
    When settingsToBackupJson() tworzy kopię z sekretami i bez nich
    Then tekst JSON jest równy zapisanemu w teście wzorcowi z v1.11.3 (grupy location, opensky, general, display, security; te same nazwy kluczy, kolejność i formatowanie)
    And pominięta wersja (updskip) nie trafia do kopii

  @test=test_admin/config_backup_test.cpp:test_restore_of_a_stage_5_backup_fills_every_setting
  Scenario: Kopia zapasowa zrobiona przed etapem 6 odtwarza każde ustawienie
    Given plik kopii zapisany w teście tak, jak wygenerował go v1.11.3 (z sekretami)
    When restoreSettingsFromBackupJson() go wczytuje
    Then status to Ok, 25 zastosowanych pól, brak błędnych, a każde pole każdego obiektu kontekstu ma wartość z pliku (dom oznaczony jako skonfigurowany)

  @test=test_admin/settings_form_test.cpp:test_form_valid_submit_sets_every_field_and_home_configured
  Scenario: Formularz ustawień zapisuje każde pole w jego obiekcie kontekstu, jak przed etapem
    Given zapisane ustawienia i formularz z każdym polem
    When applySettingsForm() go stosuje
    Then każde pole ma wartość z formularza (cfg.home.lat, cfg.poll.interval_s, cfg.backlight.night_from_min…), cfg.home.configured = true, komunikat „Zapisano.”

  @test=tools/test_check_layers.py:LayoutTest.test_only_admin_roots_and_core_link_include_settings
  Scenario: Poza admin/ ustawienia dołączają tylko core_link i korzenie kompozycji (§10 pkt 4)
    Given src/ po etapie 6
    When szukam #include "admin/core/settings.h"
    Then dołączają go tylko pliki z src/admin/, src/main/ i src/platform/core_link.h (NetState niesie ustawienia, §7)
    And nie dołącza go już opensky/adapters/opensky_http.cpp ani display/adapters/backlight_ldr.cpp

  @test=tools/test_check_layers.py:LayoutTest.test_config_type_named_only_in_admin_roots_and_core_link
  Scenario: Moduły innych kontekstów nie znają typu Settings, nawet z deklaracji zapowiadającej
    Given kod (bez komentarzy) z src/airspace/, src/enrichment/, src/opensky/, src/display/ i src/shared/
    When szukam słowa Settings (i dawnego Config)
    Then nie ma żadnego trafienia (w etapie 6 zniknęły struct Config; z opensky_client.h i backlight.h)

  @test=tools/test_check_layers.py:LayoutTest.test_context_functions_take_their_settings_objects
  Scenario Outline: Funkcje innych kontekstów dostają tylko swoje obiekty ustawień
    Given nagłówek <nagłówek> po etapie 6
    When czytam deklarację <funkcja>
    Then jej parametry ustawień to <parametry>, bez Settings

    Examples:
      | nagłówek                              | funkcja                | parametry                                                         |
      | opensky/core/opensky_client.h         | fetchAircraftStates    | const OpenSkyCredentials &, const Home &                          |
      | opensky/core/opensky_client.h         | openSkyClientPoll      | const OpenSkyCredentials &, const Home &, const PollSettings &    |
      | opensky/core/opensky_client.h         | openSkyAuthStatus      | const OpenSkyCredentials &                                        |
      | opensky/core/opensky_client.h         | openSkyMsUntilNextPoll | const PollSettings &                                              |
      | display/core/backlight.h              | backlightHandleTouch   | const BacklightSettings &                                         |
      | display/core/backlight.h              | backlightStatus        | const BacklightSettings &                                         |
      | display/adapters/backlight_ldr.h      | backlightLoop          | const BacklightSettings &, const Home &                           |
      | airspace/services/poll_pipeline.h     | PipelineParams         | Home home, PhaseThresholds phase                                  |
      | admin/core/firmware/release_check.h   | releaseCheckBegin      | const UpdateSettings &                                            |
      | admin/core/config_page.h              | pausedMinutesPerDay    | const BacklightSettings &                                         |

  @test=tools/test_check_layers.py:LayoutTest.test_display_reads_only_its_settings_from_net_state
  Scenario: Ekran czyta z NetState tylko obiekty ustawień, które go dotyczą
    Given kod z src/display/ po etapie 6
    When zbieram każde settings->X z NetState (w etapie 6: cfg->X i cfg().X)
    Then X należy do home, display, auto_cycle, backlight i update (pominięta wersja dla pytania o aktualizację)
    But nigdy opensky, poll, phase ani web

  @test=test_platform/core_link_test.cpp:test_diff_language_change
  @test=test_platform/core_link_test.cpp:test_diff_home_configured_is_first_draw
  @test=test_platform/core_link_test.cpp:test_diff_radius_or_home_moves_content
  @test=test_platform/core_link_test.cpp:test_diff_unrelated_config_change_is_nothing
  Scenario Outline: Zapis ustawień daje te same zdarzenia przerysowania co przed etapem
    Given NetState z ustawieniami i drugi z nowym wskaźnikiem ustawień, w którym zmieniło się <pole>
    When diffNetState() porównuje oba
    Then zdarzenie to <zdarzenie>

    Examples:
      | pole                  | zdarzenie        |
      | display.language      | languageChanged  |
      | home.configured       | firstDraw        |
      | home.radius_deg       | newPoll          |
      | home.lat              | newPoll          |
      | poll.interval_s       | brak             |

  @test=tools/test_check_layers.py:LayoutTest.test_stage_6_removes_the_settings_findings_of_adapters
  Scenario: Adaptery OpenSky i podświetlenia nie sięgają już po ustawienia z admin/core
    Given raport check_layers po etapie 6
    When szukam naruszeń z opensky/adapters/opensky_http.cpp i display/adapters/backlight_ldr.cpp
    Then nie ma ani opensky_http.cpp -> admin/core, ani backlight_ldr.cpp -> admin/core (settings.h, w etapie 6 config_store.h)
    And backlight_ldr.cpp i opensky_http.cpp nie mają też żadnego nowego naruszenia (Home dochodzi przez nagłówki display/core i opensky/core)

  # ---- etap 6: zasoby panelu WWW jako pliki (src/admin/assets/ → tools/embed_assets.py → admin/core/web_assets.h)

  @test=tools/test_embed_assets.py:EmbedTextTest.test_css_comments_indentation_and_line_breaks_are_dropped
  Scenario: CSS z pliku traci komentarze, wcięcia i końce linii, a reszta zostaje bez zmian
    Given style.css z komentarzem /* … */ w osobnej linii, wciętymi regułami i pustą linią
    When embed_assets.py składa z niego tekst
    Then tekst to linie bez wcięć i spacji na końcu, sklejone bez separatora, bez komentarza i pustej linii
    And spacje wewnątrz linii (np. font:15px/1.5 system-ui) zostają

  @test=tools/test_embed_assets.py:EmbedTextTest.test_js_comment_lines_are_dropped_and_code_kept
  Scenario: JS z pliku traci linie komentarza, ale nie // w napisie
    Given app.js z linią // komentarz, linią kodu z 'http://…' w napisie i wciętym blokiem
    When embed_assets.py składa z niego tekst
    Then linii komentarza nie ma, napis z // jest cały, linie kodu są sklejone bez wcięć

  @test=tools/test_embed_assets.py:EmbedTextTest.test_lines_that_would_glue_two_tokens_are_an_error
  Scenario Outline: Linia, której sklejenie z następną skleiłoby dwa słowa, przerywa generowanie
    Given plik <plik>, w którym linia kończy się na „<koniec>”, a następna zaczyna od „<początek>”
    When embed_assets.py składa z niego tekst
    Then skrypt kończy się błędem z nazwą pliku i numerem linii, a nagłówek nie jest zapisany

    Examples:
      | plik      | koniec   | początek |
      | app.js    | return   | x        |
      | app.js    | var a=1  | b=2      |
      | style.css | margin:0 | auto     |

  @test=tools/test_embed_assets.py:EmbedSvgTest.test_svg_becomes_a_single_quoted_data_uri
  Scenario: Ikona SVG staje się adresem data: z apostrofami i %23 zamiast #
    Given favicon.svg w kilku liniach, z atrybutami w cudzysłowach i kolorami #1d1f24, #ff9966
    When embed_assets.py składa z niego adres
    Then adres zaczyna się od data:image/svg+xml,<svg, nie ma odstępów między znacznikami, cudzysłowy to apostrofy, a # to %23

  @test=tools/test_embed_assets.py:CLiteralTest.test_quotes_and_backslashes_escaped_utf8_kept
  Scenario: Tekst w nagłówku C jest literałem, który kompilator odczyta z powrotem dokładnie
    Given tekst z ", \, apostrofem i znakami UTF-8 ≈ · …
    When embed_assets.py zapisuje go jako literał C
    Then " i \ są poprzedzone \, apostrof i znaki UTF-8 zostają jak są, a odczytanie literału daje tekst wejściowy

  @test=tools/test_embed_assets.py:CliTest.test_check_reports_a_stale_header_without_writing
  @test=tools/test_embed_assets.py:CliTest.test_unchanged_header_is_not_rewritten
  Scenario: Tryb --check zgłasza nieaktualny nagłówek, a zwykły przebieg nie przepisuje aktualnego
    Given katalog z zasobami i nagłówek wygenerowany ze starszej wersji style.css
    When uruchamiam embed_assets.py --check, a potem embed_assets.py dwa razy
    Then --check kończy się kodem 1 i nazywa nieaktualny plik, nie zmieniając go
    And pierwszy zwykły przebieg zapisuje nagłówek, drugi go nie dotyka (czas modyfikacji bez zmian — kompilacja nie rusza)

  @test=tools/test_embed_assets.py:RepoTest.test_checked_in_header_matches_the_assets
  Scenario: Nagłówek w repozytorium odpowiada plikom z src/admin/assets/
    Given src/admin/assets/ (style.css, app.js, restart.js, check_pending.js, favicon.svg) i src/admin/core/web_assets.h z repozytorium
    When embed_assets.py generuje nagłówek w pamięci (końce linii CRLF / LF bez znaczenia)
    Then jest równy plikowi z repozytorium

  @test=tools/test_embed_assets.py:RepoTest.test_cyd_and_native_generate_the_header_before_the_build
  @test=tools/test_embed_assets.py:RepoTest.test_ci_runs_the_embed_assets_tests
  Scenario: Kompilacja firmware'u i testów natywnych odświeża nagłówek, a CI sprawdza go testem narzędzi
    Given platformio.ini i .github/workflows/firmware.yml
    When czytam extra_scripts środowisk cyd i native oraz krok testów narzędzi
    Then oba środowiska mają pre:tools/embed_assets.py (cyd-ota i native-asan dziedziczą), cyd obok pre:tools/pio_version.py
    And krok testów narzędzi uruchamia python tools/test_embed_assets.py

  @test=tools/test_embed_assets.py:RepoTest.test_only_config_page_includes_the_assets
  @test=tools/test_embed_assets.py:RepoTest.test_config_page_keeps_no_css_or_js_literals
  Scenario: Zasoby są w firmware raz, a config_page.cpp nie ma już własnego CSS ani JS
    Given src/ po etapie 6
    When szukam #include "admin/core/web_assets.h" i literałów CSS / JS w config_page.cpp
    Then nagłówek dołącza w src/ tylko admin/core/config_page.cpp
    And w config_page.cpp nie ma reguł CSS (selektor{właściwość:) ani funkcji JS (function(), tylko znaczniki <style>, <script> i blok T={…} z tekstami web_i18n

  @test=test_admin/config_page_test.cpp:test_page_inlines_style_script_and_icon_from_assets
  Scenario: Strona ustawień wstawia styl, skrypt i ikonę z zasobów w te same miejsca co przed etapem
    Given model strony z demo i odpowiednio restarting / updateCheckPending
    When renderConfigPage() składa stronę
    Then w <head> jest <link rel="icon" … href="kWebFaviconUri"> i <style>kWebStyleCss</style>
    And przed </script></body></html> jest kWebAppJs, a po nim kWebRestartJs (restarting) albo kWebCheckPendingJs (updateCheckPending) albo nic

  @test=test_admin/config_page_test.cpp:test_page_script_texts_come_from_web_i18n
  Scenario Outline: Teksty skryptu panelu dalej przychodzą z web_i18n w języku panelu, a pliki zasobów nie mają tekstów
    Given panel w języku <język>
    When renderConfigPage() składa stronę
    Then blok T={…} przed skryptem ma teksty webTr(…, <język>) (dziennie, limit, ponad limit, wysyłanie, …)
    And żaden z tych tekstów, w żadnym języku, nie występuje w kWebAppJs, kWebRestartJs ani kWebCheckPendingJs

    Examples:
      | język |
      | PL    |
      | EN    |

  # ---- etap 6: rozmiar, liczby testów, płytka

  @manual
  Scenario: Etap 6 nie zmienia zachowania ani strony panelu, a firmware ma ten sam rozmiar ± 1 KB
    Given na gałęzi przed etapem 6 (v1.11.3, c0a10e3): przypadki w 6 grupach pio test -e native (admin 251, airspace 71, display 408, enrichment 80, opensky 74, platform 83 = 967), RUN_TEST( 967, firmware.bin 1 348 752 B, „Flash: used” 1 342 169, „RAM: used” 62 036 B, lista bibliotek, check_layers 14, strony panelu z SCREENSHOT_OUT (pio test -e native -f test_admin) i PNG z python tools/screenshots.py
    When mierzę to samo po każdym kroku i na końcu etapu (po usunięciu .pio/build/native i .pio/build/cyd)
    Then wszystkie grupy są zielone, przypadków jest 967 + nowe testy z tests.md, RUN_TEST( rośnie o tyle samo
    And firmware.bin różni się od 1 348 752 B najwyżej o 1 024 B, RAM statyczny nie rośnie, lista bibliotek ta sama
    And pliki HTML stron panelu z SCREENSHOT_OUT są bajt w bajt takie same jak przed etapem, a PNG z tools/screenshots.py nie różnią się
    And python tools/check_layers.py zgłasza 12 naruszeń — listę sprzed etapu bez opensky_http.cpp -> admin/core i backlight_ldr.cpp -> admin/core; python tools/rewrite_includes.py --check kończy się kodem 0, python tools/check_comments.py --strict nic nie zgłasza, python tools/check_bdd.py 0 problemów

  @manual
  Scenario: Płytka po kroku etapu 6 ma te same ustawienia i działa jak przedtem; panel wygląda i działa tak samo
    Given GET /backup.json (bez sekretów) z płytki na v1.11.3 przed wgraniem i firmware z kroku wgrany (USB COM3 albo pio run -e cyd-ota -t upload, za zgodą właściciela), pio device monitor -b 115200 przez 2–3 minuty
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then GET /backup.json po wgraniu różni się od tego sprzed wgrania tylko polem firmware (NVS czytane tymi samymi kluczami)
    And log szeregowy nie pokazuje paniki ani restartu poza startem, poll idzie z kontem OpenSky (pigułka „Konto” w panelu), /screenshot.bmp?screen=0, 1, 2 pokazują LOTY, TERAZ i RADAR jak po etapie 5, /perf.json w zakresie etapu 5
    And zapis formularza bez zmian zachowuje wartości; zmiana zasięgu przerysowuje pierścienie RADARU, zmiana języka ekranu przerysowuje cały ekran, oba wracają po przywróceniu
    And /light.json pokazuje ten sam tryb nocy i jasność co przed wgraniem; „Sprawdź teraz” na karcie Firmware odpowiada; aktualizacja OTA przez WiFi kończy się obrazem oznaczonym jako ważny
    And w przeglądarce panel ma ten sam wygląd (ikona karty, ciemny motyw), zakładki się przełączają, koszt w kredytach liczy się na żywo przy zmianie zasięgu, linia „Teraz” na karcie Ogólne odświeża się co 2 s, a wysyłanie firmware'u pokazuje pasek postępu

  # ---- etap 7: nazwy ze słownika (§3) — tools/rename_symbols.py, jedna zmiana nazwy na commit

  @test=tools/test_check_layers.py:LayoutTest.test_settings_module_is_admin_core_settings
  Scenario: Moduł ustawień nazywa się settings, a jego testy settings_test.cpp
    Given src/ i test/ po etapie 7
    When szukam plików i include'ów modułu ustawień
    Then są src/admin/core/settings.h i settings.cpp (git mv z config_store.*, tabela MOVES_STAGE_7 w rewrite_includes.py), a żaden plik ani #include nie nazywa się już config_store
    And testy są w test/test_admin/settings_test.cpp z run_settings_tests(), a -DTEST_MODULE=settings uruchamia tylko je

  @test=tools/test_check_layers.py:LayoutTest.test_net_state_carries_settings
  Scenario: NetState niesie ustawienia pod nazwą settings
    Given src/platform/core_link.h po etapie 7
    When czytam struct NetState
    Then ma pola std::shared_ptr<const Settings> settings i settingsSeq
    And nie ma już pól cfg ani cfgSeq

  @test=tools/test_check_layers.py:LayoutTest.test_web_server_functions_are_named_after_the_module
  Scenario: Funkcje serwera WWW nazywają się jak jego moduł
    Given src/admin/adapters/web_server.h i jego wywołania w src/main/
    When zbieram nazwy zadeklarowanych funkcji
    Then wszystkie zaczynają się od webServer (Begin, Loop, IsActive, SaveCount, SetScreenshotSource, SetOtaObserver, SetPerfSource, RestartPending)
    And w src/ nie ma żadnego configPortal…(), a src/platform/wifi_manager.cpp dalej woła setConfigPortalBlocking() z WiFiManagera

  @test=tools/test_check_comments.py:RenamedSymbolsTest.test_old_glossary_name_is_reported_with_its_new_name
  @test=tools/test_check_comments.py:RepoTest.test_renamed_symbols_absent_from_src_and_test
  Scenario: Dawna nazwa złożona, która wraca do kodu albo komentarza, jest zgłaszana
    Given plik w src/ z komentarzem „// one AircraftRow per aircraft” i kodem DataSnapshot snap;
    When python tools/check_comments.py go sprawdza
    Then zgłasza oba miejsca jako plik:linia z nową nazwą („AircraftRow was renamed TrackedFlight in spec 12 stage 7”)
    And repozytorium po etapie 7 nie ma żadnego takiego zgłoszenia (tekst w cudzysłowie, np. nagłówek dokumentacji, się nie liczy)

  @test=tools/test_check_layers.py:LayoutTest.test_settings_nvs_keys_and_types_unchanged
  @test=test_admin/config_backup_test.cpp:test_backup_json_is_the_same_as_before_stage_6
  @test=test_admin/config_backup_test.cpp:test_restore_of_a_stage_5_backup_fills_every_setting
  Scenario: Zmiany nazw nie zmieniają kluczy NVS ani kopii zapasowej
    Given ustawienia po zmianie nazwy Config → Settings i funkcji ustawień
    When zapisuję je i robię z nich kopię zapasową
    Then klucze NVS i ich typy są te same 28 co w v1.11.3
    And JSON kopii jest bajt w bajt równy wzorcowi sprzed etapu 6, a kopia sprzed etapu 6 odtwarza każde ustawienie

  # ---- etap 7: rozmiar, liczby testów, płytka

  @manual
  Scenario: Etap 7 nie zmienia kodu wynikowego ani liczby testów
    Given na gałęzi przed etapem 7 (9e3fc9a): przypadki w 6 grupach pio test -e native, RUN_TEST(, firmware.elf i firmware.bin zbudowane z FW_VERSION=v0.0.0-stage7 po usunięciu .pio/build/cyd, rozmiary sekcji (xtensa-esp32-elf-size -A), „Flash: used” / „RAM: used”, lista bibliotek, lista 12 naruszeń check_layers i 5 stron panelu z SCREENSHOT_OUT
    When mierzę to samo po każdym commicie zmiany nazwy (ten sam FW_VERSION)
    Then wszystkie grupy są zielone, liczba przypadków i RUN_TEST( jest ta sama co przed etapem (nazwy funkcji testowych bez zmian)
    And rozmiary sekcji z size -A są te same, a firmware.bin różni się najwyżej skrótami obrazu (SHA-256 ELF w opisie aplikacji, suma kontrolna, SHA-256 obrazu); przy zmianie nazwy plików config_store → settings dopuszczalna różnica z kolejności linkowania, w granicy ± 1 KB; RAM statyczny bez zmian
    And strony panelu z SCREENSHOT_OUT są bajt w bajt takie same, check_layers zgłasza te same 12 naruszeń, rewrite_includes.py --check, embed_assets.py --check i check_comments.py --strict są czyste, check_bdd.py 0 problemów

  @manual
  Scenario: Płytka z firmware'em etapu 7 działa jak przedtem i ma te same ustawienia
    Given z płytki przed wgraniem: GET /backup.json bez sekretów, GET / (HTML panelu), /perf.json, /light.json i /screenshot.bmp?screen=0, 1, 2; potem obraz końcowy etapu 7 wgrany przez USB (pio run -e cyd -t upload, za zgodą właściciela) i pio device monitor -b 115200 przez 3 minuty
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then log szeregowy nie pokazuje paniki ani restartu poza startem, a /backup.json różni się od tego sprzed wgrania tylko polem firmware
    And GET / różni się tylko wersją i wartościami na żywo; ekrany 0–2 pokazują LOTY, TERAZ i RADAR jak przed wgraniem; /perf.json w zakresie etapu 6 (przerwa pętli UI ok. 100 ms, 0 wolnych przejść, największy blok ≥ 50 KB); /light.json ten sam tryb i jasność
    And zapis formularza bez zmian daje tę samą kopię zapasową, a aktualizacja OTA przez WiFi (curl -F firmware=@… /update) kończy się restartem i obrazem oznaczonym jako ważny

  # ---- etap 8: egzekwowanie — ostatnie naruszenia tabeli z §5 i blokada w CI

  @test=tools/test_check_layers.py:LayoutTest.test_stage_8_symbols_are_declared_in_their_new_home
  Scenario Outline: Typ lub stała, przez które zależności szły w złą stronę, leżą w swoim kontekście
    Given nagłówki w src/ po etapie 8
    When szukam deklaracji <symbol>
    Then jest dokładnie jedna, w src/<nowy_nagłówek>
    And src/<stary_nagłówek> jej nie ma

    Examples:
      | symbol                 | nowy_nagłówek                        | stary_nagłówek                      |
      | Language               | shared/language.h                    | display/core/i18n.h                 |
      | AirspaceSnapshot       | airspace/core/airspace_snapshot.h    | platform/core_link.h                |
      | ScanArea               | opensky/core/scan_area.h             | opensky/core/opensky_client.h       |
      | computeScanArea        | opensky/core/scan_area.h             | opensky/core/opensky_client.h       |
      | kUpdatePromptTimeoutMs | display/core/screens/service_screen.h | admin/core/firmware/release_check.h |
      | AircraftInfo           | shared/flight_info.h                 | enrichment/core/aircraft_lookup.h   |
      | RouteInfo              | shared/flight_info.h                 | enrichment/core/route_lookup.h      |
      | AirportInfo            | shared/flight_info.h                 | enrichment/core/route_lookup.h      |

  @test=tools/test_check_layers.py:LayoutTest.test_screenshot_module_is_in_admin_core
  Scenario: Pomocnicy /screenshot.bmp (BMP, ?screen=) należą do admin/core, bo używa ich panel WWW
    Given src/ i test/ po etapie 8
    When szukam plików i include'ów modułu screenshot
    Then są src/admin/core/screenshot.h i screenshot.cpp, a w src/display/services/ zostaje tylko screenshot_bridge
    And żaden plik z src/ ani test/ nie dołącza display/services/screenshot.h

  @test=tools/test_check_layers.py:LayoutTest.test_web_server_includes_only_its_own_context
  Scenario: Serwer WWW nie sięga po dane innych kontekstów
    Given src/admin/adapters/web_server.cpp po etapie 8
    When czytam jego dyrektywy #include "…"
    Then każda wskazuje admin/, shared/ albo platform/
    And kredyty OpenSky, stan logowania i podświetlenie przychodzą przez panelLiveInputs() z admin/core/config_page.h

  @test=tools/test_check_layers.py:LayoutTest.test_foreign_device_inputs_are_wired_in_the_composition_root
  Scenario: Dane z urządzenia z kilku kontekstów składa korzeń kompozycji
    Given drzewo src/ po etapie 8
    When szukam definicji deviceAlertInputs() i panelLiveInputs()
    Then obie są w src/main/alert_sources.cpp
    And src/admin/adapters/alert_sources.cpp już nie istnieje

  @test=tools/test_check_layers.py:LayoutTest.test_layers_are_decoupled_in_the_named_headers
  Scenario Outline: Plik, który łamał tabelę z §5, dołącza już tylko dozwolone warstwy
    Given src/<plik> po etapie 8
    When czytam jego dyrektywy #include "…"
    Then żadna nie wskazuje <zakazany_cel>

    Examples:
      | plik                                              | zakazany_cel    |
      | enrichment/core/place_names.h                     | display/core    |
      | opensky/core/opensky_quota.h                      | display/core    |
      | airspace/services/poll_pipeline.h                 | platform        |
      | airspace/core/tracked_flight.h                    | enrichment/core |
      | display/adapters/lgfx/service_screen_draw.cpp     | admin/core      |

  @test=test_enrichment/place_names_test.cpp:test_place_name_prefers_city_table
  @test=test_enrichment/place_names_test.cpp:test_city_table_polish_and_english
  @test=test_opensky/opensky_quota_test.cpp:test_format_wait
  Scenario: Tekst zależny od języka działa tak samo z Language ze shared/
    Given miasto „Monachium” / „Munich” w tabeli IATA i oczekiwanie 6 godz. 57 min
    When proszę o nazwę miejsca i o czas oczekiwania po polsku i po angielsku
    Then wyniki są takie same jak przed etapem 8

  @test=test_airspace/poll_pipeline_test.cpp:test_now_pick_points_at_the_sorted_row
  @test=test_platform/core_link_test.cpp:test_diff_health_with_rows_is_header_only
  Scenario: Obraz nieba z airspace/core dalej przechodzi przez NetState
    Given AirspaceSnapshot zbudowany przez poll_pipeline
    When NetState przenosi go do interfejsu
    Then wybór TERAZ wskazuje właściwy wiersz, a zmiana stanu przy tych samych lotach odświeża tylko nagłówek

  @test=tools/test_check_layers.py:LayoutTest.test_source_files_stay_under_the_section_10_limits
  Scenario Outline: Pliki w src/ mieszczą się w limitach z §10 pkt 3
    Given <pliki> po etapie 8, licząc linie z pustymi włącznie (jak wc -l)
    When liczę ich linie
    Then każdy ma <limit> linii

    Examples:
      | pliki                 | limit           |
      | src/main/main.cpp     | mniej niż 250   |
      | src/main/net_task.cpp | mniej niż 300   |
      | każdy plik w src/     | najwyżej 600    |

  @test=tools/test_check_layers.py:LayoutTest.test_net_task_parts_live_in_the_composition_root
  Scenario: Pomiar, porty hexdb.io i oznaczanie obrazu jako ważnego są w osobnych modułach korzenia kompozycji
    Given src/main/ po etapie 8
    When szukam definicji productionPorts(), funkcji pomiaru net_perf i funkcji wycofania net_rollback
    Then są w src/main/net_enrichment.cpp, src/main/net_perf.cpp i src/main/net_rollback.cpp, a net_task.cpp ich nie definiuje
    And python tools/check_comments.py traktuje te moduły jak kod rdzenia 0 (NET_MODULES)

  @test=tools/test_check_layers.py:LayoutTest.test_config_page_is_split_by_tab
  Scenario: Strona ustawień jest podzielona według zakładek
    Given src/admin/core/ po etapie 8
    When szukam kodu pięciu zakładek i klasy Writer
    Then zakładki są w config_page_tabs.cpp, po jednej funkcji na zakładkę, a Writer w config_page_writer.h
    And web_assets.h dołącza dalej tylko config_page.cpp

  @test=tools/test_check_layers.py:LayoutTest.test_web_server_routes_keep_their_guards
  Scenario: Po podziale serwera WWW każda trasa ma dalej swoje zabezpieczenia
    Given src/admin/adapters/web_server.cpp i web_server_firmware.cpp po etapie 8
    When czytam obsługę tras POST i wywołania zwrotne wysyłania plików
    Then każda obsługa POST zaczyna się od requireSameOrigin(), przed authorize()
    And każde wywołanie zwrotne wysyłania zaczyna się od if (!isMultipartRequest()) return;
    And każda trasa jest zarejestrowana przez secured<>

  @test=tools/test_check_layers.py:LayoutTest.test_strict_layer_check_passes_on_the_repository
  Scenario: python tools/check_layers.py --strict przechodzi na repozytorium
    Given drzewo repozytorium po etapie 8
    When uruchamiam python tools/check_layers.py --strict
    Then kończy się kodem 0 i wypisuje „0 layer finding(s) in total”
    But include dopisany wbrew tabeli z §5 (scenariusz „Z --strict naruszenie kończy się kodem 1”) wywraca build

  @test=tools/test_check_layers.py:LayoutTest.test_architecture_doc_carries_the_rules_and_claude_md_points_to_it
  Scenario: Reguły i słownik są w docs/architecture.md, a CLAUDE.md tylko do nich odsyła
    Given docs/architecture.md i CLAUDE.md po etapie 8
    When czytam „Source layout” i „Code conventions”
    Then docs/architecture.md ma konteksty, tabelę zależności z §5 i słownik z §3
    And sekcja „Code conventions” w CLAUDE.md odsyła do docs/architecture.md "Source layout" i nie ma już listy modułów
    And nagłówek „Code conventions” zostaje (cytują go komentarze), a CLAUDE.md ma mniej niż 250 linii

  @test=tools/test_check_layers.py:LayoutTest.test_one_off_migration_tools_are_gone
  Scenario: Jednorazowe narzędzia migracji są usunięte
    Given tools/ i .github/workflows/firmware.yml po etapie 8
    When szukam rewrite_includes i rename_symbols
    Then plików narzędzi ani ich testów nie ma, a CI ich nie uruchamia
    And tools/check_layers.py, tools/check_comments.py (z RENAMED_SYMBOLS) i tools/embed_assets.py zostają

  # ---- etap 8: rozmiar, liczby testów, płytka

  @manual
  Scenario: Etap 8 nie zmienia liczby testów ani zachowania, a firmware mieści się w granicy
    Given na gałęzi przed etapem 8 (591fac0): przypadki w 6 grupach pio test -e native (admin 257, airspace 71, display 412, enrichment 80, opensky 74, platform 84 = 978), RUN_TEST( (978), firmware.bin i sekcje z size -A zbudowane z FW_VERSION=v0.0.0-stage8 po usunięciu .pio/build/cyd, „Flash: used” / „RAM: used”, lista bibliotek i 5 stron panelu z SCREENSHOT_OUT
    When mierzę to samo po każdym commicie etapu (ten sam FW_VERSION)
    Then wszystkie grupy są zielone, a przypadków i RUN_TEST( jest 981 (978 sprzed etapu, które tylko idą za kodem, + 3 nowe testy strzałki w tekstach wersji po uploadzie)
    And przy przeniesieniu samych typów i stałych sekcje są te same, a cały etap (z podziałem plików, który zmienia kolejność linkowania) dokłada do firmware.bin najwyżej 4 096 bajtów i zmienia RAM statyczny o najwyżej ± 64 bajty względem stanu przed etapem
    And strony panelu z SCREENSHOT_OUT są bajt w bajt takie same po każdym kroku podziału config_page, python tools/check_layers.py --strict kończy się kodem 0, a check_comments.py --strict, check_bdd.py i embed_assets.py --check są czyste

  @manual
  Scenario: Płytka z firmware'em etapu 8 działa jak przedtem
    Given z płytki przed wgraniem: /backup.json bez sekretów, GET /, /perf.json, /light.json i /screenshot.bmp?screen=0, 1, 2; potem obraz końcowy etapu 8 wgrany przez USB (pio run -e cyd -t upload, za zgodą właściciela) i pio device monitor -b 115200 przez 3 minuty
    When płytka startuje, łączy się z WiFi i odbiera kilka pollów
    Then log szeregowy nie pokazuje paniki ani restartu poza startem, a /backup.json różni się tylko polem firmware
    And ekrany 0–2 pokazują LOTY, TERAZ i RADAR jak przed wgraniem, a zakładki OpenSky i Ogólne w panelu pokazują kredyty, stan logowania i jasność, które zwraca panelLiveInputs()
    And /light.json ma ten sam tryb i jasność, ostrzeżenia („!”) działają jak przed wgraniem, /perf.json w zakresie etapu 7 (przerwa pętli UI ok. 100 ms, 0 wolnych przejść, największy blok ≥ 50 KB)
    And aktualizacja OTA przez WiFi (curl -F firmware=@… /update) kończy się restartem i obrazem oznaczonym jako ważny, a pytanie o aktualizację (zrzut screen=3) ma ten sam pasek 30 s
    And §10 pkt 5: „Sprawdź teraz” odpowiada przez weryfikowany TLS, noc i ostrzeżenia działają jak przed wgraniem, wolny stos net w /perf.json nie jest mniejszy o więcej niż 1 KB od 9 388 B, a instalacja z GitHuba (wersja -dev tej gałęzi, za zgodą właściciela) kończy się obrazem oznaczonym jako ważny
