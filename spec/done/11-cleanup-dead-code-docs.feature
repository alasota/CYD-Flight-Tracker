Feature: Porządki — bez martwego kodu, bez komentarzy o pracy main.cpp, którą robi net_task, CLAUDE.md krótszy niż 250 linii
  Nieużywane symbole z §1 znikają z src/ i test/ (poza LCARS_ORANGE, który został jako
  THEME_WARNING), komentarze opisują obecny podział na rdzenie, a CLAUDE.md zawiera tylko zasady
  pracy — opis ekranów, źródeł danych, architektury i historii przechodzi do docs/. Na ekranie
  nic się nie zmienia, firmware nie rośnie. Pilnuje tego tools/check_comments.py (w CI tylko
  ostrzega) i jego testy, które sprawdzają też samo repozytorium.

  # ---- §1 martwy kod

  @test=tools/test_check_comments.py:RepoTest.test_removed_symbols_absent_from_src_and_test
  Scenario: Żaden usunięty symbol nie występuje w src/ ani w test/
    Given lista usuniętych symboli z §1 spec 11 (viewToggleButtonBounds, drawElbowFrame, elbowArcPoint, phaseIcon, LCARS_MAGENTA, RefreshNow, …)
    When skrypt przeszukuje wszystkie pliki w src/ i test/
    Then nie znajduje żadnego z nich

  @test=tools/test_check_comments.py:RemovedSymbolsTest.test_removed_symbol_in_source_is_reported
  Scenario: Usunięty symbol, który wrócił do kodu, jest zgłaszany z plikiem i linią
    Given plik src/foo.cpp z linią wywołującą drawElbowFrame(gfx, 0, 0, 10, 10, 4, 2, 0)
    When skrypt sprawdza usunięte symbole
    Then zgłasza "src/foo.cpp:1" i nazwę drawElbowFrame

  @test=tools/test_check_comments.py:RemovedSymbolsTest.test_longer_name_with_removed_prefix_is_not_reported
  Scenario: Dłuższa nazwa zaczynająca się od usuniętego symbolu nie jest zgłaszana
    Given plik z wywołaniami phaseIconRows(kind), phaseIconKind(phase, onGround) i drawPanelBorder()
    When skrypt sprawdza usunięte symbole
    Then nic nie zgłasza

  @test=tools/test_check_comments.py:RemovedSymbolsTest.test_lcars_orange_is_not_reported
  Scenario: LCARS_ORANGE nie jest zgłaszany jako usunięty
    Given plik z linią "constexpr uint16_t THEME_WARNING = LCARS_ORANGE;"
    When skrypt sprawdza usunięte symbole
    Then nic nie zgłasza

  @test=test_display/lcars_theme_test.cpp:test_lcars_orange_stays_as_warning_colour
  Scenario: Pomarańczowy LCARS zostaje jako kolor ostrzeżenia „!”
    Given paleta po usunięciu nieużywanych kolorów
    When sprawdzam rolę THEME_WARNING
    Then THEME_WARNING to LCARS_ORANGE = 0xFC00

  @test=test_platform/core_link_test.cpp:test_default_net_command_is_none
  Scenario: Domyślnie utworzona komenda sieciowa nic nie zleca
    Given komenda NetCommand utworzona bez podania typu
    When net_task sprawdza jej typ
    Then typ to NetCommandType::None
    And tag jest pusty

  @test=test_display/flight_screen_test.cpp:test_countdown_default_color_is_theme_role
  Scenario: Domyślny kolor odliczania to rola z motywu, nie stała ze starej palety
    Given CountdownDisplay utworzony bez obliczeń
    When sprawdzam jego kolor
    Then to THEME_CONTENT_SECONDARY

  @test=test_display/aircraft_summary_test.cpp:test_fit_identity_has_no_phase_marker
  Scenario: Linia identyfikacji nie zawiera znaku fazy lotu — ikona jest rysowana osobno
    Given lot LOT281, LOT Polish Airlines, B738 w fazie startu
    When ekran składa linię identyfikacji na 200 px
    Then linia to dokładnie "LOT281  LOT Polish Airlines  B738"

  # ---- §2 nieaktualne komentarze

  @test=tools/test_check_comments.py:RepoTest.test_no_net_module_comment_mentions_main_cpp
  Scenario: Żaden komentarz w module rdzenia sieciowego nie przypisuje pracy main.cpp
    Given moduły działające w net_task (config_portal, opensky_client, aircraft_lookup, route_lookup, lookup_budget, poll_pipeline, release_check, net_task, …)
    When skrypt czyta komentarze w ich plikach .h i .cpp
    Then żaden nie wspomina main.cpp ani stosu loopTask

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_main_cpp_in_net_module_comment_is_reported
  Scenario: Komentarz „main.cpp reloads” w module sieciowym jest zgłaszany
    Given src/config_portal.cpp z komentarzem "// main.cpp reloads the config"
    When skrypt sprawdza komentarze
    Then zgłasza "src/config_portal.cpp:1"

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_looptask_in_net_module_comment_is_reported
  Scenario: Komentarz o 8 KB stosu loopTask w module sieciowym jest zgłaszany
    Given src/opensky_client.cpp z komentarzem "// loopTask's stack is only 8KB"
    When skrypt sprawdza komentarze
    Then zgłasza "src/opensky_client.cpp:1"

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_main_cpp_in_ui_module_comment_is_not_reported
  Scenario: main.cpp w komentarzu modułu UI nie jest zgłaszany
    Given src/redraw_policy.h z komentarzem "// main.cpp gathers the events each loop"
    When skrypt sprawdza komentarze
    Then nic nie zgłasza

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_main_cpp_outside_comment_is_ignored
  Scenario: main.cpp poza komentarzem (w napisie w kodzie) nie jest zgłaszany
    Given src/net_task.cpp z linią kodu const char *f = "main.cpp"; bez komentarza
    When skrypt sprawdza komentarze
    Then nic nie zgłasza

  @test=tools/test_check_comments.py:MainCppCommentsTest.test_block_comment_is_checked
  Scenario: Komentarz blokowy też jest sprawdzany
    Given src/route_lookup.h z komentarzem "/* budget kept by\n main.cpp */"
    When skrypt sprawdza komentarze
    Then zgłasza "src/route_lookup.h:2"

  @test=tools/test_check_comments.py:CliTest.test_findings_only_warn_by_default
  Scenario: Bez --strict skrypt tylko ostrzega
    Given drzewo plików z jednym zgłoszeniem
    When uruchamiam check_comments.py bez opcji
    Then wypisuje ostrzeżenie z plikiem i linią
    And kończy się kodem 0

  @test=tools/test_check_comments.py:CliTest.test_strict_mode_fails_on_findings
  Scenario: Z --strict zgłoszenie kończy skrypt błędem
    Given drzewo plików z jednym zgłoszeniem
    When uruchamiam check_comments.py --strict
    Then kończy się kodem różnym od 0

  # ---- §3 dokumentacja

  @test=tools/test_check_comments.py:DocRefsTest.test_reference_to_missing_section_is_reported
  Scenario: Odwołanie w komentarzu do sekcji, której nie ma w dokumencie, jest zgłaszane
    Given CLAUDE.md bez nagłówka "Screen 1"
    And komentarz "// see CLAUDE.md "Screen 1"" w src/table_view.h
    When skrypt sprawdza odwołania do dokumentacji
    Then zgłasza "src/table_view.h:1" i brakującą sekcję "Screen 1"

  @test=tools/test_check_comments.py:DocRefsTest.test_reference_matches_section_heading_prefix
  Scenario: Odwołanie pasuje do nagłówka, który się od niego zaczyna
    Given docs/screens.md z nagłówkiem "## Screen 1 — Flights"
    And komentarz "// docs/screens.md "Screen 1"" w src/table_view.h
    When skrypt sprawdza odwołania do dokumentacji
    Then nic nie zgłasza

  @test=tools/test_check_comments.py:DocRefsTest.test_old_review_notes_reference_is_reported
  Scenario: Stare odwołanie „CLAUDE.md review notes” jest zgłaszane
    Given komentarz "// see CLAUDE.md review notes 1.5" w src/config_portal.cpp
    When skrypt sprawdza odwołania do dokumentacji
    Then zgłasza "src/config_portal.cpp:1" ze wskazaniem docs/review-notes-2026.md

  @test=tools/test_check_comments.py:RepoTest.test_doc_references_in_src_and_test_resolve
  Scenario: Każde odwołanie do sekcji dokumentacji w src/ i test/ trafia w istniejący nagłówek
    Given CLAUDE.md i pliki docs/ po podziale
    When skrypt sprawdza odwołania w komentarzach src/ i test/
    Then wszystkie trafiają w istniejące nagłówki

  @test=tools/test_check_comments.py:ClaudeMdSizeTest.test_line_limit_boundary
  Scenario: Granica długości CLAUDE.md to 250 linii
    Given CLAUDE.md z 249 liniami, a potem z 250 liniami
    When skrypt sprawdza jego długość
    Then 249 linii przechodzi
    And 250 linii jest zgłaszane

  @test=tools/test_check_comments.py:RepoTest.test_claude_md_under_250_lines
  Scenario: CLAUDE.md w repozytorium ma mniej niż 250 linii
    Given CLAUDE.md po podziale dokumentacji
    When skrypt liczy jego linie
    Then jest ich mniej niż 250

  @test=tools/test_check_comments.py:RepoTest.test_moved_and_template_files
  Scenario: Notatki z przeglądu są w docs/, szablonowe README PlatformIO zniknęły
    Given repozytorium po porządkach
    When sprawdzam pliki
    Then docs/review-notes-2026.md istnieje, a review-notes.md w katalogu głównym nie
    And nie ma lib/README ani szablonu PlatformIO w include/README

  @test=tools/test_check_comments.py:RepoTest.test_rssi_to_bars_not_described_as_existing
  Scenario: rssiToBars() nie jest opisywane jako istniejąca funkcja
    Given CLAUDE.md, docs/screens.md i src/
    When szukam "rssiToBars"
    Then nie ma go nigdzie poza docs/history.md

  @manual
  Scenario: Każdy fragment usunięty z CLAUDE.md jest w docs/ albo opisany w commicie jako nieaktualny
    Given diff CLAUDE.md z gałęzi spec/11-cleanup-dead-code-docs
    When recenzent porównuje usunięte akapity z docs/screens.md, docs/data-sources.md, docs/architecture.md, docs/history.md i opisem commita
    Then każdy usunięty akapit jest w którymś z tych plików albo wymieniony w opisie commita jako nieaktualny

  # ---- §4 kryterium 1: build, rozmiar, brak zmian na ekranie

  @manual
  Scenario: Firmware buduje się i nie jest większy niż przed porządkami
    Given rozmiar z "pio run -e cyd" na 3269c16: Flash 1 470 425 B, firmware.bin 1 477 008 B
    When buduję gałąź poleceniem "pio run -e cyd"
    Then build kończy się SUCCESS
    And Flash i firmware.bin są nie większe niż przed porządkami

  @manual
  Scenario: Ekrany wyglądają tak samo jak przed porządkami
    Given płytka z wersją deweloperską tej gałęzi
    When oglądam ekrany LOTY, TERAZ, RADAR, pytanie o aktualizację (screen=3) i ekran „!” (screen=4)
    Then układ, kolory i teksty są takie jak w v1.9.0
