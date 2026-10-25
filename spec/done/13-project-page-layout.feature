Feature: Strona projektu krótsza i czytelniejsza — historia zmian po wersjach minor, zrzuty panelu w zakładkach, sekcja „Co jeszcze potrafi”
  Strona ma ≤ 9 500 px wysokości przy 1280 px (dziś ~12 400) bez utraty treści: historia zmian
  ma jeden wpis na wersję minor z opisem z ROADMAP.md, commity są zwinięte, zrzuty panelu są w
  przeglądarce z zakładkami, a karty „Nowość” stały się kafelkami z „Więcej”.

  # ---- §1.1 grupowanie wydań po wersji minor (build_site.py, czyste funkcje)

  @test=tools/test_build_site.py:ReleaseRangesTest.test_dev_tags_get_no_entry_of_their_own
  Scenario: Wersja deweloperska nie ma własnego wpisu, jej commity trafiają do następnego wydania
    Given tagi v1.6.4, v1.6.4-dev.11, v1.6.3
    When strona liczy zakresy commitów wydań
    Then są tylko zakresy v1.6.3 i v1.6.3..v1.6.4
    And tag v1.6.4-dev.11 nie ma własnego zakresu

  @test=tools/test_build_site.py:GroupByMinorTest.test_patches_and_dev_versions_in_one_group
  Scenario: Wydania patch tej samej wersji minor tworzą jedną grupę
    Given wydania v1.6.0, v1.6.1, v1.6.2, v1.6.3, v1.6.4 i v1.5.0, v1.5.1
    When strona grupuje je po wersji minor
    Then są dwie grupy: v1.6 z 5 wydaniami i v1.5 z 2 wydaniami
    And commity grupy to commity wszystkich jej wydań, od najnowszego

  @test=tools/test_build_site.py:GroupByMinorTest.test_group_header_range_date_and_count
  Scenario: Nagłówek grupy pokazuje zakres wydań, datę najnowszego i liczbę zmian
    Given grupa v1.6 z wydaniami v1.6.0 … v1.6.4, najnowsze z 25.10.2026, razem 9 commitów
    When strona rysuje historię zmian
    Then nagłówek grupy zawiera „v1.6”, „v1.6.0 – v1.6.4”, „25.10.2026” i „zmian: 9” / „9 change(s)”

  @test=tools/test_build_site.py:GroupByMinorTest.test_single_release_group_shows_only_its_number
  Scenario: Grupa z jednym wydaniem pokazuje tylko jego numer zamiast zakresu
    Given grupa v1.3 z jednym wydaniem v1.3.0
    When strona rysuje historię zmian
    Then nagłówek grupy zawiera „v1.3.0”
    And nie zawiera zakresu z „–”

  @test=tools/test_build_site.py:GroupByMinorTest.test_groups_newest_first_by_version_number
  Scenario: Grupy idą od najnowszej wersji, sortowane numerycznie (v1.10 przed v1.9)
    Given wydania v1.9.0, v1.9.1 i v1.10.0
    When strona grupuje je po wersji minor
    Then pierwsza jest grupa v1.10, druga v1.9

  @test=tools/test_build_site.py:GroupByMinorTest.test_release_and_diff_links
  Scenario: Linki grupy prowadzą do najnowszego wydania i do różnic od poprzedniej grupy
    Given grupy v1.6 (v1.6.0 … v1.6.4) i v1.5 (v1.5.0, v1.5.1)
    When strona rysuje historię zmian
    Then grupa v1.6 linkuje „wydanie” do releases/tag/v1.6.4
    And „różnice” do compare/v1.5.1...v1.6.4
    And najstarsza grupa nie ma linku „różnice”

  # ---- §1.2 opis z roadmapy

  @test=tools/test_build_site.py:RoadmapDoneTest.test_done_rows_grouped_by_minor_in_version_order
  Scenario: Pozycje „Done in vX.Y.Z” z ROADMAP.md trafiają do grupy vX.Y w kolejności wersji
    Given w tabeli „Done” są wiersze z werdyktem „Done in v1.4.3”, „Done in v1.4.0” i „Done in v1.4.2”
    When strona przypisuje je do grup
    Then grupa v1.4 ma 3 pozycje w kolejności v1.4.0, v1.4.2, v1.4.3
    And każda pozycja ma tekst kolumny „Item” po angielsku i po polsku

  @test=tools/test_build_site.py:RoadmapDoneTest.test_unreleased_vnext_row_in_no_group
  Scenario: Pozycja jeszcze niewydana („Done in vNEXT”) nie trafia do żadnej grupy
    Given w tabeli „Done” jest wiersz z werdyktem „Done in vNEXT: …”
    When strona przypisuje pozycje do grup
    Then ten wiersz nie należy do żadnej grupy

  @test=tools/test_build_site.py:RoadmapDoneTest.test_only_done_section_counts
  Scenario: Wiersze spoza sekcji „Done” nie są brane pod uwagę
    Given w sekcji „Planned” jest wiersz, którego werdykt zaczyna się od „Done in v1.7.0”
    When strona przypisuje pozycje do grup
    Then grupa v1.7 nie dostaje żadnej pozycji

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_group_lists_roadmap_items_first
  Scenario: Grupa z pozycjami z roadmapy pokazuje najpierw ich listę, bez werdyktu
    Given grupa v1.6 ma pozycję z roadmapy „Wrong OpenSky credentials: …” / „Złe dane logowania OpenSky: …”
    When strona rysuje historię zmian
    Then pierwsza lista w grupie zawiera tę pozycję jako parę .en / .pl, zwykłą czcionką
    And pozycja linkuje do #roadmap
    And tekst werdyktu z roadmapy nie pojawia się w historii zmian

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_group_without_roadmap_shows_only_commits
  Scenario: Grupa bez pozycji w roadmapie pokazuje tylko commity
    Given grupa v1.0 nie ma żadnej pozycji „Done in v1.0.x”
    When strona rysuje historię zmian
    Then grupa zawiera listę commitów i nie zawiera listy pozycji z roadmapy

  # ---- §1.3 commity schowane, „zza kulis” wyszarzone

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_commits_folded_with_limit
  Scenario: Commity grupy są w zwiniętym „Commity (N)” z limitem 15 i linkiem „…i jeszcze N”
    Given grupa z pozycjami z roadmapy i 20 commitami
    When strona rysuje historię zmian
    Then commity są w zagnieżdżonym <details> bez atrybutu open, z podpisem „Commits (20)” / „Commity (20)”
    And lista pokazuje 15 commitów i link „…i jeszcze 5” do porównania na GitHubie

  @test=tools/test_build_site.py:ChoreTest.test_is_chore_examples
  Scenario Outline: Commit „zza kulis” jest rozpoznawany po temacie
    Given temat commita „<temat>”
    When strona go klasyfikuje
    Then commit jest zza kulis: <zza_kulis>

    Examples:
      | temat                                                              | zza_kulis |
      | Tests: readable assertions in test_admin (spec 07 step 3)          | tak       |
      | Test reproducing a released spec left 'in progress' in the roadmap | tak       |
      | Docs: night mode in README                                         | tak       |
      | Plan and BDD for spec 06                                           | tak       |
      | Spec 13: project page — changelog grouped by minor version         | tak       |
      | Spec/plan: OpenSky login fallback                                  | tak       |
      | /release: stale docs trigger the release PR                        | tak       |
      | /fix: regression test first                                        | tak       |
      | Release plan: finds a released spec the docs still call unfinished | tak       |
      | Harness: spec → PR agents                                          | tak       |
      | Python bytecode out of the repo                                    | tak       |
      | fix                                                                | tak       |
      | Testing the touch calibration on the board                         | nie       |
      | Fix: table column overflow                                         | nie       |
      | OpenSky login failure: keep polling anonymously                    | nie       |

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_chore_commits_greyed_and_counted
  Scenario: Commity zza kulis są wyszarzone, nie ukryte, i liczą się w nagłówku
    Given grupa z 3 commitami, z czego 2 to „Tests: …” i „Docs: …”
    When strona rysuje historię zmian
    Then lista commitów ma 3 pozycje, dwie z klasą chore
    And nagłówek grupy mówi „zmian: 3”

  # ---- §1.4 ile otwarte

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_two_newest_groups_open_rest_one_line
  Scenario: Otwarte są dwie najnowsze grupy, starsze zwinięte do jednej linii
    Given cztery grupy: v1.6, v1.5 i v1.2 z pozycjami z roadmapy, v1.0 bez pozycji
    When strona rysuje historię zmian
    Then grupy v1.6 i v1.5 są otwarte, v1.2 i v1.0 zwinięte
    And zwinięta v1.2 pokazuje w podsumowaniu swoją pierwszą pozycję z roadmapy
    And zwinięta v1.0 pokazuje w podsumowaniu „zmian: N” / „N change(s)”

  @test=tools/test_build_site.py:ChangelogHtmlTest.test_text_is_escaped
  Scenario: Znaki specjalne w tematach commitów i pozycjach roadmapy są escapowane
    Given temat commita „Table <TYP> & route” i pozycja roadmapy z „<” i „&”
    When strona rysuje historię zmian
    Then w HTML są „&lt;TYP&gt;” i „&amp;”, a nie surowe znaki

  @manual
  Scenario: Strona buduje się z pełnej historii tagów repozytorium
    Given checkout z wszystkimi tagami v*
    When uruchamiam python tools/build_site.py --out _site
    Then historia zmian ma jeden wpis na wersję minor (v1.6 … v1.0)
    And wpisy v1.6, v1.5, v1.4, v1.3, v1.2, v1.1 pokazują pozycje z roadmapy (1, 1, 3, 1, 1, 1)
    And commity są zwinięte

  # ---- §4 roadmapa: tabela „Done” krótsza

  @test=tools/test_build_site.py:RoadmapHtmlTest.test_done_shows_version_not_verdict
  Scenario: Tabela „Done” pokazuje numer wersji z linkiem do historii zmian zamiast werdyktu
    Given wiersz „Done” z werdyktem „Done in v1.6.0: with a wrong secret the planes keep coming …”
    When strona rysuje roadmapę
    Then sekcja „Done” ma tylko kolumny #, Item i wersję
    And komórka wersji to „v1.6.0” z linkiem do #changelog
    And długiego werdyktu nie ma w HTML

  @test=tools/test_build_site.py:RoadmapHtmlTest.test_other_sections_keep_full_verdict
  Scenario: Sekcje „Planned” i „In progress” zachowują wszystkie kolumny i pełny werdykt
    Given wiersz „Planned” z pełnym werdyktem
    When strona rysuje roadmapę
    Then sekcja „Planned” ma kolumny Value, Effort, Risk, Rating i Verdict
    And werdykt jest w HTML w całości

  @test=tools/test_build_site.py:RoadmapHtmlTest.test_done_vnext_reads_next_release
  Scenario: Pozycja „Done in vNEXT” w tabeli „Done” mówi „następna wersja”
    Given wiersz „Done” z werdyktem „Done in vNEXT: …”
    When strona rysuje roadmapę
    Then komórka wersji to „the next release” / „następna wersja”

  # ---- §2, §3 treść strony (statyczne sprawdzenie site/index.html)

  @test=tools/test_build_site.py:SiteHtmlTest.test_features_section_and_menu_links
  Scenario: Sekcja „Co jeszcze potrafi” ma 4 kafelki, a menu górne linkuje do niej i do panelu
    Given site/index.html
    When strona jest sprawdzana
    Then istnieje sekcja #features po #screens i przed #install z 4 kafelkami
    And kafelek nocnego trybu ma id="night-mode"
    And menu górne ma linki #features i #panel
    And nie ma już id="dual-core" ani etykiet „New:” / „Nowość:”

  @test=tools/test_build_site.py:SiteHtmlTest.test_panel_tabs_point_to_existing_screenshots
  Scenario: Przeglądarka panelu ma 6 zakładek, każda z istniejącym zrzutem i podpisem
    Given site/index.html i docs/screenshots/
    When strona jest sprawdzana
    Then #panel ma role="tablist" z 6 elementami role="tab" w kolejności Lokalizacja, OpenSky, Ogólne, Kopia, Firmware, „!”
    And każda zakładka ma role="tabpanel" z obrazkiem web_location / web_opensky / web_general / web_backup / web_firmware / web_alert, który istnieje w docs/screenshots
    And każdy obrazek ma loading="lazy", width, height i link do pełnego PNG w nowej karcie
    And każdy panel ma podpis jako parę .en / .pl
    And web_firmware_dev.png nie jest pokazywany

  @test=tools/test_build_site.py:SiteHtmlTest.test_former_news_texts_kept
  Scenario: Żaden tekst z dawnych kart „Nowość” nie zginął
    Given charakterystyczne zdania (EN i PL) z każdej dawnej karty „Nowość” i karty kopii zapasowej
    When strona jest sprawdzana
    Then każde z nich jest w site/index.html (w „Więcej” kafelka albo w „Dla dociekliwych”)
    And „Dla dociekliwych” zawiera „0,1 s”, „5,7 s” i cyd-sky.local/perf.json

  @test=tools/test_build_site.py:SiteHtmlTest.test_new_texts_come_in_en_pl_pairs
  Scenario: Każdy nowy tekst w sekcjach #features i #panel ma wersję PL i EN
    Given sekcje #features i #panel w site/index.html
    When strona jest sprawdzana
    Then liczba elementów .en równa się liczbie elementów .pl w każdej z nich
    And każdy kafelek ma <details> z podpisem „More” / „Więcej”

  # ---- wygląd, sprawdzany w przeglądarce (zrzuty w PR)

  @manual
  Scenario: Strona ma najwyżej 9 500 px wysokości przy 1280 px szerokości
    Given strona zbudowana przez python tools/build_site.py --out _site
    When mierzę ją w headless Edge przy szerokości 1280 px (measure_site.py z planu)
    Then wysokość dokumentu wynosi najwyżej 9 500 px (przed zmianą: 12 389 px)
    And zrzut całej strony przed i po jest w opisie PR

  @manual
  Scenario: Przy 375 px strona nie przewija się w bok, zakładki panelu się zawijają
    Given strona otwarta przy szerokości 375 px
    When przewijam ją od góry do dołu
    Then strona nie ma poziomego przewijania (dziś ma: tabele roadmapy rozpychają kartę do 897 px)
    And przyciski zakładek panelu zawijają się do kolejnych wierszy
    And ramka zrzutu ma najwyżej 70vh i przewija się w środku

  @manual
  Scenario: Zakładki panelu przełącza klik i strzałki, jeden zrzut naraz w ramce o stałej wysokości
    Given strona otwarta w przeglądarce przy 1280 px z włączonym JS
    When klikam zakładkę „Ogólne”, a potem naciskam strzałkę w prawo
    Then widać jeden zrzut naraz, ramka ma najwyżej 640 px i długi zrzut przewija się w środku
    And podpis stoi po prawej stronie ramki
    And aktywna zakładka ma aria-selected="true" i kolor akcentu, strzałka przenosi na „Kopia”
    And klik w zrzut otwiera pełny PNG w nowej karcie

  @manual
  Scenario: Bez JS widać wszystkie zrzuty panelu i pełną treść „Więcej”
    Given JavaScript wyłączony w przeglądarce
    When otwieram stronę
    Then wszystkie 6 zrzutów panelu stoją jeden pod drugim, każdy z podpisem
    And każde „Więcej” w kafelkach da się rozwinąć i pokazuje pełny tekst

  @manual
  Scenario: Przełącznik PL/EN działa w nowych elementach i nie psuje przeglądarki zakładek
    Given strona z włączonym JS
    When przełączam język PL → EN → PL
    Then historia zmian, kafelki, „Więcej”, zakładki i podpisy panelu zmieniają język
    And przeglądarka zakładek dalej pokazuje tylko jeden zrzut
