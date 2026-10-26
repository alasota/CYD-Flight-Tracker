Feature: Odporność współpracy rdzeni — zrzut ekranu bez wyścigu, żadne „NIE” nie ginie, strona ustawień odpowiada w trakcie lookupów
  Przerwany /screenshot.bmp nigdy nie pisze do zwolnionego bufora ani nie zatrzymuje rysowania,
  odpowiedź na pytanie o aktualizację dociera do net_task także przy pełnej kolejce komend,
  a strona ustawień jest obsługiwana między zapytaniami hexdb.io fazy B.

  # ---- 1. ScreenshotBridge (screenshot.h): licznik generacji zamiast flagi `cancelled`

  @test=test_admin/screenshot_test.cpp:test_bridge_normal_capture_fills_every_band
  Scenario: Zwykły zrzut — wszystkie pasy trafiają do buforów, potem ekran rysuje dalej
    Given handler zaczyna zrzut ekranu i UI odpowiada na begin
    When handler prosi kolejno o k pasów na zmianę do dwóch buforów, a UI każdy wypełnia
    Then każde waitBand zwraca true, a bufory mają wiersze pasów w kolejności BMP (od dołu)
    And dopóki trwa zrzut, UI nie rysuje
    And po end() UI od razu znowu rysuje

  @test=test_admin/screenshot_test.cpp:test_bridge_timeout_mid_band_leaves_no_band_done
  Scenario: Timeout w trakcie pasa nie zostawia „BandDone” i nie blokuje rysowania
    Given UI zaczęło czytać pas z panelu
    When handlerowi kończy się czas oczekiwania na ten pas
    And UI kończy pas już po timeoucie
    Then pas nie zostaje oznaczony jako gotowy, a stan mostu to bezczynność
    And przy następnym przejściu UI znowu rysuje — nie czeka 5 s

  @test=test_admin/screenshot_test.cpp:test_bridge_late_prepare_after_begin_timeout_is_ignored
  Scenario: Spóźnione przygotowanie ekranu po timeoucie begin nie wznawia zrzutu
    Given handler zaczął zrzut, a UI nie odpowiedziało w czasie
    When UI kończy przygotowanie ekranu już po timeoucie
    Then begin zwróciło false, a stan mostu to bezczynność
    And UI nie wstrzymuje rysowania

  @test=test_admin/screenshot_test.cpp:test_bridge_stale_band_does_not_write_new_buffer
  Scenario: Pas ze starego żądania nie pisze do bufora nowego żądania
    Given UI czyta pas żądania A, a handler A przekroczył czas i zwolnił swoje bufory
    When przychodzi nowe żądanie B z nowymi buforami, zanim UI skończy stary pas
    Then żaden wiersz starego pasa nie trafia ani do buforów A, ani do buforów B
    And stary pas nie oznacza się jako gotowy dla żądania B

  @test=test_admin/screenshot_test.cpp:test_bridge_end_invalidates_outstanding_band
  Scenario: Po end() spóźniony pas niczego już nie zapisuje
    Given UI czyta pas, o który handler poprosił
    When handler woła end() i zwalnia bufory
    Then UI porzuca pas bez zapisu do bufora

  @test=test_admin/screenshot_test.cpp:test_bridge_idle_client_resumes_drawing_after_5_s
  Scenario Outline: Klient zniknął bez end() — UI wraca do rysowania po 5 s bez aktywności
    Given zrzut trwa, a ostatni pas UI oddało w chwili T
    When od T minęło <ms> ms bez kolejnego żądania
    Then UI <rysuje> rysuje

    Examples:
      | ms   | rysuje |
      | 4999 | nie    |
      | 5000 | nie    |
      | 5001 | znowu  |

  @test=test_admin/screenshot_test.cpp:test_bridge_concurrent_random_timeouts_never_touch_freed_buffers
  Scenario: Współbieżnie — 10 000 zrzutów z losowymi timeoutami nie pisze do zwolnionej pamięci
    Given wątek „handler” i wątek „UI” pracują równolegle na jednym moście
    When handler wykonuje 10 000 zrzutów, losowo przerywając je timeoutem w dowolnym miejscu
    And po każdym zrzucie wypełnia swoje bufory wzorcem i dopiero wtedy je zwalnia
    Then wzorzec w każdym zwolnionym buforze jest nienaruszony
    And każdy zrzut, który nie został przerwany, ma komplet wierszy
    And po ostatnim zrzucie UI nie wstrzymuje rysowania

  # ---- 2. Komenda UI -> net, która nie zmieściła się w kolejce (core_link)

  @test=test_platform/core_link_test.cpp:test_pending_command_sent_once_queue_accepts
  Scenario Outline: Odpowiedź na pytanie o aktualizację czeka, aż kolejka ją przyjmie
    Given kolejka komend do net_task jest pełna
    When użytkownik odpowiada <odpowiedź> na pytanie o wersję v1.8.0
    Then komenda <komenda> z tagiem v1.8.0 czeka w buforze UI
    And przy kolejnych przejściach loop() jest ponawiana
    And gdy kolejka ją przyjmie, bufor jest pusty i komenda wysłana dokładnie raz

    Examples:
      | odpowiedź | komenda    |
      | NIE       | SkipTag    |
      | TAK       | InstallTag |

  @test=test_platform/core_link_test.cpp:test_pending_command_kept_while_queue_full
  Scenario: Dopóki kolejka jest pełna, oczekująca komenda nie ginie
    Given w buforze UI czeka SkipTag v1.8.0
    When kolejka odrzuca ją przy 100 kolejnych próbach
    Then po każdej próbie w buforze wciąż jest SkipTag v1.8.0

  @test=test_platform/core_link_test.cpp:test_no_pending_command_sends_nothing
  Scenario: Bez oczekującej komendy ponawianie nic nie wysyła
    Given bufor UI jest pusty
    When loop() próbuje ponowić oczekującą komendę
    Then do kolejki nic nie jest wysyłane

  @test=test_platform/core_link_test.cpp:test_queue_full_logged_once_per_command
  Scenario: „command queue full, retrying” trafia do logu raz na komendę
    Given kolejka jest pełna
    When użytkownik odpowiada NIE, a ponowienie nie udaje się 10 razy
    Then log o pełnej kolejce jest zgłoszony tylko przy pierwszej nieudanej próbie

  @test=test_platform/core_link_test.cpp:test_command_sent_directly_when_queue_has_room
  Scenario: Kolejka ma miejsce — komenda idzie od razu, nic nie czeka
    Given kolejka komend ma wolne miejsce
    When użytkownik odpowiada NIE
    Then SkipTag jest wysłany od razu, bufor UI jest pusty i nic nie trafia do logu

  # ---- 3. Strona ustawień obsługiwana między zapytaniami hexdb.io (poll_pipeline)

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_called_once_per_fetch
  Scenario: Hak jest wołany po każdym rzeczywistym zapytaniu hexdb.io
    Given faza B ma do pobrania 2 samoloty, 2 trasy i 3 lotniska, których nie ma w cache
    When buildDataSnapshot wykonuje fazę B
    Then hak betweenFetches jest wołany tyle razy, ile wynosi fetches snapshotu (7)
    And każde wywołanie następuje po zapytaniu, nie przed nim

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_not_called_for_cache_hits
  Scenario: Trafienia w cache nie wołają haka
    Given wszystkie samoloty, trasy i lotniska są w cache (RAM albo NVS)
    When buildDataSnapshot wykonuje fazę A i fazę B
    Then hak betweenFetches nie jest wołany ani razu

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_optional
  Scenario: Bez haka faza B działa jak dotąd
    Given LookupPorts bez haka betweenFetches
    When buildDataSnapshot wykonuje fazę B z nowymi lookupami
    Then snapshot jest taki sam jak przed tą zmianą (te same fetche, te same wiersze)

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_stop_ends_phase_b_with_partial_snapshot
  Scenario: Hak każe przerwać — faza B kończy się bez dalszych zapytań, z częściowym snapshotem
    Given faza B ma do pobrania kilka samolotów, tras i lotnisk
    When hak po drugim zapytaniu zwraca „przerwij” (zaczęła się aktualizacja OTA)
    Then po nim nie ma już żadnego zapytania do hexdb.io
    And fetches snapshotu wynosi 2
    And wiersze z pobranymi danymi są uzupełnione, reszta ma „...” (pending), nie „--”
    And dane z cache nadal trafiają do wierszy
    And pozycje, odległości, fazy lotu i wybór ekranu TERAZ są policzone jak zwykle

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_guard_ignores_nested_call
  Scenario: Hak wywołany w trakcie haka nic nie robi
    Given hak właśnie obsługuje stronę ustawień
    When w trakcie tej obsługi hak zostaje wywołany ponownie
    Then drugie wywołanie nie obsługuje strony i nie przerywa fazy B

  @test=test_airspace/poll_pipeline_test.cpp:test_between_fetches_stops_when_update_started
  Scenario Outline: Aktualizacja rozpoczęta w trakcie haka kończy fazę B
    Given hak obsługuje stronę ustawień
    When w tej obsłudze <zdarzenie>
    Then hak zwraca „przerwij”

    Examples:
      | zdarzenie                                            |
      | wgrano firmware i czeka restart                      |
      | trwa zapis obrazu OTA                                |

  # ---- 4. Kryteria akceptacji na płytce

  @manual
  Scenario: 50 przerwanych zrzutów ekranu w trakcie pracy nie psuje płytki
    Given płytka z tą wersją pokazuje dane na żywo
    When 50 razy pobieram /screenshot.bmp przez curl z losowym przerwaniem (--max-time 0.3)
    Then płytka się nie restartuje
    And ekran rysuje dalej najpóźniej 1 s po każdym przerwaniu
    And w [perf] nie ma przerwy UI dłuższej niż 5 s

  @manual
  Scenario: Strona ustawień ładuje się szybko także w trakcie fazy B
    Given płytka z tą wersją odpytuje OpenSky i robi lookupy hexdb.io
    When przez 5 min odświeżam stronę ustawień co 2 s
    Then każde ładowanie trwa krócej niż 1,5 s, także gdy w logu jest „phase B: N hexdb.io lookups”
    And stack net w [perf] nie zbliża się do 16 KB
