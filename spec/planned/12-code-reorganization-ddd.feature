Feature: Reorganizacja kodu według DDD — etapy 1–3: katalogi kontekstów i warstw (core / services / adapters, korzenie w main/), include'y z pełną ścieżką, raport zależności, źle położone typy w swoich kontekstach
  Etap 1 przeniósł pliki z płaskiego src/ (git mv, bez dzielenia) do katalogów kontekstów i warstw
  z §4; każdy #include "…" podaje ścieżkę od src/ (flaga -I src), środowisko native nie kompiluje
  korzeni kompozycji ani adapterów, a tools/check_layers.py raportuje naruszenia tabeli z §5
  (tylko raport, CI nie blokuje do etapu 8). Etap 2 zmienia nazwy warstw: domain/ → core/,
  app/ → services/, src/app/ → src/main/; z admin/app/ czyste config_page i web_i18n idą do
  admin/core/, a config_portal (trasy WebServer) do admin/adapters/. Etap 3 przenosi typy i
  funkcje leżące w złym kontekście (AircraftRow, OpenSkyHealth, odległość / namiar / kierunek
  świata, koszt w kredytach, UTF-8 / fitToWidth, „na ziemi”, wybór TERAZ, kolejność lotów) do
  kontekstów z §4, a ich testy idą za kodem do grupy nowego kontekstu. Zachowanie urządzenia się
  nie zmienia: testy zielone, liczba RUN_TEST nie maleje, firmware ma ten sam rozmiar ± 1 KB.
  Wyniki płytki z etapów 1–2 są w specyfikacji (§8 „Wynik etapu N”); etapy 4–8 dostaną swoje
  scenariusze w tym pliku, gdy przyjdzie ich kolej.

  # ---- etap 1 (aktualne po etapie 2): układ katalogów i include'y

  @test=tools/test_check_layers.py:LayoutTest.test_every_src_file_sits_in_a_context_and_layer
  Scenario: Każdy plik z src/ leży w katalogu kontekstu i warstwy z §4
    Given drzewo src/ po przeniesieniu
    When skrypt klasyfikuje każdy plik .h / .hpp / .c / .cpp
    Then każdy należy do shared/, platform/, main/ albo <kontekst>/<warstwa>/ (airspace, enrichment, opensky, display, admin × core, services, adapters)
    And w samym src/ nie ma żadnego pliku

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_mixed_files_land_with_their_pure_half
  Scenario Outline: Plik z czystą i sprzętową połową trafia tam, gdzie będzie jego czysta część
    Given plik <plik> ma dziś część czystą i część pod #ifdef ARDUINO
    When tabela przeniesień etapu 1 (z nazwami warstw sprzed etapu 2) wskazuje jego nowe miejsce
    Then plik leży w <katalog>, a podział na adapter czeka na etap 4

    Examples:
      | plik               | katalog                       |
      | opensky_client.cpp | opensky/domain/               |
      | lookup_store.cpp   | enrichment/domain/            |
      | table_view.cpp     | display/domain/screens/       |
      | backlight.cpp      | display/domain/               |
      | config_portal.cpp  | admin/app/                    |
      | release_check.cpp  | admin/domain/firmware/        |
      | time_sync.cpp      | platform/                     |

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_move_table_matches_the_tree
  Scenario: Tabele przeniesień etapów 1 i 2 zgadzają się z drzewem
    Given tabele przeniesień etapów 1 i 2 w tools/rewrite_includes.py
    When dla każdego z 94 plików sprzed etapu 1 składam jego ścieżkę: etap 1, potem etap 2
    Then każda ścieżka końcowa istnieje w src/
    And żadna ścieżka sprzed etapu 1 ani pośrednia (po etapie 1, przed etapem 2) już nie istnieje

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

  @test=tools/test_rewrite_includes.py:RewriteTest.test_short_include_becomes_full_path
  Scenario: Krótki include przeniesionego nagłówka dostaje pełną ścieżkę
    Given plik z linią #include "table_view.h", a nagłówek leży w src/display/core/screens/
    When skrypt przepisuje include'y
    Then linia brzmi #include "display/core/screens/table_view.h"

  @test=tools/test_rewrite_includes.py:RewriteTest.test_system_support_and_include_dir_headers_unchanged
  Scenario: Include'y systemowe, z test/support/, z include/ i lokalne nagłówki testu zostają bez zmian
    Given plik z #include <vector>, <ArduinoJson.h>, "test_support.h", "LGFX_CYD.hpp" i "github_fixtures.h"
    When skrypt przepisuje include'y
    Then żadna z tych linii się nie zmienia

  @test=tools/test_rewrite_includes.py:RewriteTest.test_second_run_changes_nothing
  Scenario: Drugie uruchomienie niczego nie zmienia
    Given drzewo, w którym include'y już mają pełne ścieżki
    When skrypt przepisuje include'y jeszcze raz
    Then żaden plik się nie zmienia, a tryb --check kończy się kodem 0

  @test=tools/test_rewrite_includes.py:RewriteTest.test_duplicate_header_name_is_an_error
  Scenario: Dwa nagłówki o tej samej nazwie w src/ to błąd, a nie zgadywanie
    Given src/a/core/x.h i src/b/core/x.h oraz plik z #include "x.h"
    When skrypt buduje indeks nagłówków
    Then kończy się błędem z obiema ścieżkami i nie zmienia żadnego pliku

  @test=tools/test_rewrite_includes.py:RewriteTest.test_trailing_comment_and_crlf_preserved
  Scenario: Komentarz za include'em i końce linii CRLF zostają
    Given plik z końcami linii CRLF i linią #include "core_link.h"  // Guarded<T>
    When skrypt przepisuje include'y
    Then linia brzmi #include "platform/core_link.h"  // Guarded<T>, a plik ma dalej CRLF

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
      | src/admin/core/            | admin/adapters/config_portal.h       | admin/core -> admin/adapters              |
      | src/admin/core/            | admin/services/x.h                   | admin/core -> admin/services              |
      | src/airspace/services/     | enrichment/services/x.h              | airspace/services -> enrichment/services  |
      | src/admin/adapters/        | display/services/screenshot.h        | admin/adapters -> display/services        |
      | src/display/core/          | main/net_task.h                      | display/core -> main                      |
      | src/display/core/          | LGFX_CYD.hpp                         | display/core -> display/adapters          |
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

  @test=tools/test_check_layers.py:WorkflowTest.test_firmware_workflow_reports_layers_before_native_tests
  Scenario: CI uruchamia raport warstw przed testami natywnymi, bez blokowania, i testy narzędzi
    Given .github/workflows/firmware.yml
    When czytam jego kroki
    Then python tools/check_layers.py (bez --strict) jest w kroku przed pio test -e native
    And tools/test_check_layers.py, tools/test_rewrite_includes.py i tools/test_subset_efont.py są uruchamiane razem z innymi testami narzędzi

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

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_stage_2_renames_the_layer_folder
  Scenario Outline: Etap 2 zmienia tylko nazwę katalogu warstwy, nie plik
    Given plik <po_etapie_1> (ścieżka od src/ po etapie 1)
    When tabela przeniesień etapu 2 wskazuje jego nowe miejsce
    Then plik leży w <po_etapie_2>, z tą samą nazwą i tym samym podkatalogiem

    Examples:
      | po_etapie_1                                | po_etapie_2                              |
      | display/domain/screens/table_view.cpp      | display/core/screens/table_view.cpp      |
      | display/domain/theme/fonts/lcars_efont.c   | display/core/theme/fonts/lcars_efont.c   |
      | enrichment/domain/lookup_cache.h           | enrichment/core/lookup_cache.h           |
      | admin/domain/firmware/version.cpp          | admin/core/firmware/version.cpp          |
      | airspace/app/poll_pipeline.cpp             | airspace/services/poll_pipeline.cpp      |
      | display/app/screenshot.cpp                 | display/services/screenshot.cpp          |
      | app/main.cpp                               | main/main.cpp                            |
      | app/net_task.h                             | main/net_task.h                          |

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_admin_app_splits_into_core_and_adapters
  Scenario Outline: admin/app/ rozdziela się: czyste moduły do admin/core/, trasy WebServer do admin/adapters/
    Given plik <plik> z admin/app/ po etapie 1
    When tabela przeniesień etapu 2 wskazuje jego nowe miejsce
    Then plik leży w <katalog>

    Examples:
      | plik              | katalog          |
      | config_page.cpp   | admin/core/      |
      | config_page.h     | admin/core/      |
      | web_i18n.cpp      | admin/core/      |
      | web_i18n.h        | admin/core/      |
      | config_portal.cpp | admin/adapters/  |
      | config_portal.h   | admin/adapters/  |

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_stage_2_leaves_shared_platform_and_adapters_alone
  Scenario: Etap 2 nie rusza shared/, platform/ ani plików, które już leżą w adapters/
    Given tabela przeniesień etapu 2
    When sprawdzam jej wpisy
    Then ma 80 wpisów, każdy kluczem jest ścieżka docelowa etapu 1
    And żaden klucz nie leży w shared/, platform/ ani <kontekst>/adapters/
    And żaden wpis nie zmienia nazwy pliku

  @test=tools/test_rewrite_includes.py:RewriteTest.test_old_layer_path_becomes_the_new_full_path
  Scenario: Include ze starą nazwą warstwy dostaje nową pełną ścieżkę
    Given drzewo po git mv etapu 2 z plikiem, w którym są linie #include "display/domain/i18n.h"  // Language i #include "app/net_task.h"
    When uruchamiam python tools/rewrite_includes.py --check, potem bez --check, potem znów --check
    Then pierwsze --check kończy się kodem 1 i niczego nie zapisuje
    And po przepisaniu linie brzmią #include "display/core/i18n.h"  // Language i #include "main/net_task.h"
    And drugie --check kończy się kodem 0

  @test=tools/test_check_layers.py:RuleTableTest.test_old_layer_names_are_outside_any_layer
  Scenario: Plik w katalogu o starej nazwie warstwy jest zgłaszany jako leżący poza warstwą
    Given pliki src/display/domain/x.h, src/admin/app/x.cpp i src/app/main.cpp
    When skrypt klasyfikuje pliki i sprawdza zależności
    Then każdy jest zgłoszony jako leżący poza kontekstem i warstwą
    But src/display/core/x.h, src/admin/services/x.cpp i src/main/main.cpp należą do display/core, admin/services i main

  @test=tools/test_check_layers.py:BuildConfigTest.test_native_filter_excludes_roots_and_adapters
  Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej części platform/ — poza config_portal do etapu 4
    Given platformio.ini
    When czytam build_src_filter środowiska native
    Then filtr to +<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp> +<admin/adapters/config_portal.cpp>
    And config_portal.cpp jest jedynym plikiem z adapters/, który filtr przywraca (jego czyste funkcje mają testy w test_admin, a część WebServer jest pod #ifdef ARDUINO)

  # (zmienione w etapie 3: limit to stan po ostatnim wydanym etapie — 51 po etapie 2, 47 po etapie 3)
  @test=tools/test_check_layers.py:LayoutTest.test_layer_findings_do_not_exceed_the_stage_limit
  Scenario: Liczba naruszeń w raporcie warstw nie rośnie ponad stan po ostatnim etapie
    Given limit naruszeń zapisany w teście: 47 (stan po etapie 3; po etapie 2 było 51)
    When uruchamiam check_layers na drzewie repozytorium
    Then łączna liczba naruszeń jest nie większa niż 47
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
      | AircraftRow            | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | buildEnrichedRecords   | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | annotateDistances      | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | classifyPhases         | airspace/core/tracked_flight.h         | display/core/screens/table_view.h         |
      | sortRowsForDisplay     | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | sortRowsByDistance     | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | splitFeaturedAndRest   | airspace/core/flight_ordering.h        | display/core/screens/table_view.h         |
      | pickNowAircraft        | airspace/core/now_pick.h               | display/core/screens/flight_screen.h      |
      | OpenSkyHealth          | airspace/core/feed_health.h            | display/core/screens/status_bar.h         |
      | Aircraft               | airspace/core/aircraft_state.h         | opensky/core/opensky_client.h             |
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
    And AircraftRow i OpenSkyHealth przychodzą z airspace/core/tracked_flight.h i airspace/core/feed_health.h

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
  Scenario: Lot (AircraftRow) składa się w airspace bez znajomości ekranu
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
  Scenario: Stan źródła danych (OpenSkyHealth z airspace) dalej koloruje kropkę w nagłówku i odświeża tylko nagłówek
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
