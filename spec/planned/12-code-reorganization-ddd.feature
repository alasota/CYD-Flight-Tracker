Feature: Reorganizacja kodu według DDD — etap 1 „Szkielet”: katalogi kontekstów i warstw, include'y z pełną ścieżką, raport zależności
  Pliki z płaskiego src/ trafiają (git mv, bez dzielenia) do katalogów kontekstów i warstw z §4;
  każdy #include "…" podaje ścieżkę od src/ (flaga -I src), środowisko native nie kompiluje
  korzeni kompozycji ani adapterów, a tools/check_layers.py raportuje naruszenia tabeli z §5
  (tylko raport, CI nie blokuje do etapu 7). Zachowanie urządzenia się nie zmienia: testy
  zielone, liczba RUN_TEST nie maleje, firmware ma ten sam rozmiar ± 1 KB.
  Etapy 2–7 (§8) dostaną swoje scenariusze w tym pliku, gdy przyjdzie ich kolej.

  # ---- etap 1: układ katalogów i include'y

  @test=tools/test_check_layers.py:LayoutTest.test_every_src_file_sits_in_a_context_and_layer
  Scenario: Każdy plik z src/ leży w katalogu kontekstu i warstwy z §4
    Given drzewo src/ po przeniesieniu
    When skrypt klasyfikuje każdy plik .h / .hpp / .c / .cpp
    Then każdy należy do shared/, platform/, app/ albo <kontekst>/<warstwa>/ (airspace, enrichment, opensky, display, admin × domain, app, adapters)
    And w samym src/ nie ma żadnego pliku

  @test=tools/test_rewrite_includes.py:MoveTableTest.test_mixed_files_land_with_their_pure_half
  Scenario Outline: Plik z czystą i sprzętową połową trafia tam, gdzie będzie jego czysta część
    Given plik <plik> ma dziś część czystą i część pod #ifdef ARDUINO
    When tabela przeniesień etapu 1 wskazuje jego nowe miejsce
    Then plik leży w <katalog>, a podział na adapter czeka na etap 3

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
  Scenario: Tabela przeniesień zgadza się z drzewem po przeniesieniu
    Given tabela przeniesień etapu 1 w tools/rewrite_includes.py
    When porównuję ją z src/
    Then każdy plik docelowy istnieje, a żadna stara ścieżka z płaskiego src/ już nie istnieje

  @test=tools/test_check_layers.py:LayoutTest.test_includes_in_src_and_test_use_full_paths
  Scenario: Include'y w src/ i test/ podają pełną ścieżkę od src/
    Given wszystkie dyrektywy #include "…" w src/ i test/
    When skrypt rozwiązuje każdą ścieżkę
    Then każda wskazuje plik względem src/, include/, test/support/ albo (tylko w test/) katalogu samego testu
    And żaden plik z src/ nie dołącza sąsiada krótką nazwą

  @test=tools/test_check_layers.py:BuildConfigTest.test_native_filter_excludes_roots_and_adapters
  Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej części platform/
    Given platformio.ini
    When czytam build_src_filter środowiska native
    Then filtr to +<*> -<app/> -<*/adapters/> -<platform/*_esp32.cpp>

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
  Scenario: Generator fontów pisze do nowego katalogu i dołącza nagłówek pełną ścieżką
    Given tools/subset_efont.py po przeniesieniu fontów
    When sprawdzam, dokąd pisze i jaki #include wstawia do wygenerowanego pliku
    Then pisze do istniejącego src/display/domain/theme/fonts/lcars_efont.c
    And wstawia #include "display/domain/theme/fonts/lcars_efont.h" — ten sam co w pliku w repozytorium

  # ---- etap 1: tools/rewrite_includes.py

  @test=tools/test_rewrite_includes.py:RewriteTest.test_short_include_becomes_full_path
  Scenario: Krótki include przeniesionego nagłówka dostaje pełną ścieżkę
    Given plik z linią #include "table_view.h", a nagłówek leży w src/display/domain/screens/
    When skrypt przepisuje include'y
    Then linia brzmi #include "display/domain/screens/table_view.h"

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
    Given src/a/domain/x.h i src/b/domain/x.h oraz plik z #include "x.h"
    When skrypt buduje indeks nagłówków
    Then kończy się błędem z obiema ścieżkami i nie zmienia żadnego pliku

  @test=tools/test_rewrite_includes.py:RewriteTest.test_trailing_comment_and_crlf_preserved
  Scenario: Komentarz za include'em i końce linii CRLF zostają
    Given plik z końcami linii CRLF i linią #include "core_link.h"  // Guarded<T>
    When skrypt przepisuje include'y
    Then linia brzmi #include "platform/core_link.h"  // Guarded<T>, a plik ma dalej CRLF

  # ---- etap 1: tools/check_layers.py (tryb raportu)

  @test=tools/test_check_layers.py:RuleTableTest.test_allowed_includes_are_not_reported
  Scenario Outline: Include dozwolony tabelą z §5 nie jest zgłaszany
    Given plik w <kto> z #include "<cel>"
    When skrypt sprawdza zależności
    Then nic nie zgłasza

    Examples:
      | kto                       | cel                                          |
      | src/airspace/domain/      | shared/sun_times.h                           |
      | src/opensky/domain/       | airspace/domain/flight_phase.h               |
      | src/display/domain/       | enrichment/domain/aircraft_names.h           |
      | src/display/domain/       | admin/domain/device_alerts.h                 |
      | src/admin/domain/         | display/domain/backlight.h                   |
      | src/airspace/app/         | enrichment/domain/aircraft_lookup.h          |
      | src/display/app/          | platform/core_link.h                         |
      | src/platform/             | display/domain/screens/table_view.h          |
      | src/enrichment/adapters/  | enrichment/domain/lookup_store.h             |
      | src/app/                  | enrichment/adapters/hexdb_http.h             |
      | test/test_display/        | display/domain/theme/lcars_theme.h           |

  @test=tools/test_check_layers.py:RuleTableTest.test_forbidden_includes_are_reported_with_file_and_line
  Scenario Outline: Include zakazany tabelą z §5 jest zgłaszany z plikiem, linią i parą warstw
    Given plik <kto>x.cpp z #include "<cel>" w linii 3
    When skrypt sprawdza zależności
    Then zgłasza "<kto>x.cpp:3: <para>"

    Examples:
      | kto                        | cel                                  | para                                      |
      | src/display/domain/        | opensky/domain/opensky_quota.h       | display/domain -> opensky/domain          |
      | src/enrichment/domain/     | opensky/domain/opensky_auth.h        | enrichment/domain -> opensky/domain       |
      | src/airspace/domain/       | display/domain/screens/table_view.h  | airspace/domain -> display/domain         |
      | src/opensky/domain/        | enrichment/adapters/hexdb_http.h     | opensky/domain -> enrichment/adapters     |
      | src/admin/domain/          | admin/app/config_page.h              | admin/domain -> admin/app                 |
      | src/airspace/app/          | enrichment/app/x.h                   | airspace/app -> enrichment/app            |
      | src/display/domain/        | app/net_task.h                       | display/domain -> app                     |
      | src/display/domain/        | LGFX_CYD.hpp                         | display/domain -> display/adapters        |
      | src/shared/                | platform/core_link.h                 | shared -> platform                        |

  @test=tools/test_check_layers.py:RuleTableTest.test_test_files_may_not_include_adapters_or_roots
  Scenario: Test nie dołącza adapterów ani korzeni kompozycji
    Given test/test_enrichment/x_test.cpp z #include "enrichment/adapters/hexdb_http.h" i #include "app/net_task.h"
    When skrypt sprawdza zależności
    Then zgłasza oba include'y jako test -> enrichment/adapters i test -> app

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
  Scenario Outline: domain/ i app/ nie dołączają SDK, także pod #ifdef ARDUINO
    Given plik <kto>x.cpp z #include <<sdk>> wewnątrz #ifdef ARDUINO
    When skrypt sprawdza zależności
    Then zgłasza ten include jako <warstwa> -> sdk

    Examples:
      | kto                    | sdk              | warstwa           |
      | src/display/domain/    | Arduino.h        | display/domain    |
      | src/opensky/domain/    | HTTPClient.h     | opensky/domain    |
      | src/enrichment/domain/ | Preferences.h    | enrichment/domain |
      | src/admin/app/         | WebServer.h      | admin/app         |
      | src/display/domain/    | LovyanGFX.hpp    | display/domain    |

  @test=tools/test_check_layers.py:SdkIncludeTest.test_standard_and_arduinojson_headers_allowed_everywhere
  Scenario: Nagłówki standardowe i ArduinoJson wolno dołączać wszędzie
    Given pliki w shared/, airspace/domain/ i test/ z #include <vector>, <cstdint>, <atomic>, <ArduinoJson.h> i <unity.h>
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

  # ---- etap 1: płytka i rozmiar

  @manual
  Scenario: Firmware po przeniesieniu ma ten sam rozmiar ± 1 KB
    Given pio run -e cyd zbudowane na main przed etapem 1 (rozmiar firmware.bin i linia „Flash: used”)
    When buduję pio run -e cyd na gałęzi etapu 1
    Then firmware.bin różni się o najwyżej 1024 bajty, a lista bibliotek w logu budowania jest ta sama
    And krok CI „Check the OTA firmware marker” przechodzi (wersja dalej trafia do version.cpp)

  @manual
  Scenario: Płytka z firmware'em etapu 1 działa jak przedtem
    Given firmware z gałęzi wgrany przez USB (pio run -e cyd -t upload), za zgodą właściciela
    When płytka startuje i łączy się z WiFi
    Then trzy ekrany (LOTY, TERAZ, RADAR) pokazują dane, dotyk przełącza ekrany, nagłówek i pasek zakładek jak przedtem
    And panel cyd-sky.local otwiera wszystkie zakładki, stopka pokazuje wersję z gałęzi, /perf.json i /screenshot.bmp odpowiadają
    And wgranie tego samego firmware'u przez pio run -e cyd-ota -t upload kończy się sukcesem i płytka wraca po restarcie
