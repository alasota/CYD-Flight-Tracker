Feature: Błąd logowania OpenSky — urządzenie działa anonimowo i mówi o tym w panelu
  Złe client_id / client_secret nie zatrzymują danych: płytka odpytuje OpenSky anonimowo,
  nie pyta serwera logowania częściej niż co 30 min, a panel WWW pokazuje czerwoną pigułkę
  i ostrzeżenie z kodem HTTP. (Stan logowania, alert „!” i jednorazowy log są już z spec 08.)

  # ---- opensky_auth: stan logowania i odstęp między próbami tokenu (§3.1)

  @test=test_opensky/opensky_auth_test.cpp:test_classify_token_response
  Scenario Outline: Odpowiedź serwera logowania jest klasyfikowana jako Ok, Rejected albo Transient
    Given serwer logowania odpowiedział kodem <kod>, a access_token <token>
    When płytka klasyfikuje tę odpowiedź
    Then wynik to <wynik>

    Examples:
      | kod | token | wynik     |
      | 200 | jest  | Ok        |
      | 200 | brak  | Transient |
      | 400 | brak  | Rejected  |
      | 401 | brak  | Rejected  |
      | 403 | brak  | Rejected  |
      | 0   | brak  | Transient |
      | -1  | brak  | Transient |
      | 500 | brak  | Transient |
      | 503 | brak  | Transient |

  @test=test_opensky/opensky_auth_test.cpp:test_rejected_login_waits_30_min
  Scenario: Odrzucone dane logowania wstrzymują próby tokenu na 30 minut
    Given płytka ma dane logowania w stanie Account
    When serwer logowania odrzuca je kodem 401 w chwili T
    Then stan to LoginFailed z kodem 401 i czasem pierwszego odrzucenia T
    And przez 30 minut od T płytka nie próbuje pobrać tokenu
    And dokładnie po 30 minutach próbuje ponownie

  @test=test_opensky/opensky_auth_test.cpp:test_retry_wait_survives_millis_rollover
  Scenario: Odstęp 30 minut działa przez przepełnienie millis()
    Given odrzucenie nastąpiło tuż przed przepełnieniem licznika millis()
    When licznik przekręca się przez zero
    Then płytka dalej czeka do upływu 30 minut od odrzucenia
    And po 30 minutach próbuje ponownie

  @test=test_opensky/opensky_auth_test.cpp:test_repeated_rejection_keeps_first_failure_time
  Scenario: Kolejne odrzucenie po 30 minutach przesuwa następną próbę, ale nie zmienia „od kiedy”
    Given stan LoginFailed od chwili T
    When po 30 minutach serwer logowania znów odrzuca dane
    Then następna próba jest za kolejne 30 minut
    And czas pierwszego odrzucenia pozostaje T

  @test=test_opensky/opensky_auth_test.cpp:test_transient_error_keeps_account
  Scenario: Przejściowy błąd logowania przy działającym koncie nie jest błędem logowania
    Given płytka ma dane logowania w stanie Account
    When serwer logowania nie odpowiada albo zwraca 5xx
    Then stan pozostaje Account
    And przy następnym pollu płytka znów próbuje pobrać token

  @test=test_opensky/opensky_auth_test.cpp:test_transient_error_while_failed_waits_30_min
  Scenario: Przejściowy błąd w stanie LoginFailed też odsuwa próbę o 30 minut
    Given stan LoginFailed
    When ponowna próba tokenu kończy się brakiem odpowiedzi
    Then stan pozostaje LoginFailed z dotychczasowym kodem HTTP
    And następna próba jest za 30 minut

  @test=test_opensky/opensky_auth_test.cpp:test_new_credentials_try_at_once
  Scenario: Zapisanie innych danych logowania pozwala spróbować od razu
    Given stan LoginFailed i trwa 30-minutowa przerwa
    When w panelu zostają zapisane inne client_id / client_secret
    Then stan to Account bez kodu błędu
    And płytka może pobrać token od razu

  @test=test_opensky/opensky_auth_test.cpp:test_no_credentials_never_tries_token
  Scenario: Bez danych logowania płytka nigdy nie pyta serwera logowania
    Given client_id albo client_secret jest pusty
    When płytka sprawdza, czy pobrać token
    Then stan to NoCredentials
    And próba tokenu nie jest dozwolona
    And zapytanie /states/all idzie bez tokenu

  # ---- Decyzje wokół /states/all (§3.2, czyste planStatesRequest / afterStatesResponse)

  @test=test_opensky/opensky_auth_test.cpp:test_bearer_only_with_account_and_valid_token
  Scenario Outline: Token Bearer jest wysyłany tylko z działającym kontem i ważnym tokenem
    Given stan logowania <stan> i token <token>
    When płytka planuje zapytanie /states/all
    Then zapytanie <bearer> nagłówek Authorization: Bearer

    Examples:
      | stan          | token    | bearer   |
      | Account       | ważny    | ma       |
      | Account       | nieważny | nie ma   |
      | LoginFailed   | ważny    | nie ma   |
      | NoCredentials | nieważny | nie ma   |

  @test=test_opensky/opensky_auth_test.cpp:test_states_401_with_bearer_refreshes_once
  Scenario: Pierwsze 401 na /states/all z tokenem oznacza „odśwież token raz i powtórz”
    Given zapytanie /states/all poszło z tokenem Bearer
    When OpenSky odpowiada 401, a token nie był jeszcze odświeżany w tym pollu
    Then płytka odświeża token i powtarza zapytanie
    And stan logowania się nie zmienia

  @test=test_opensky/opensky_auth_test.cpp:test_states_rejected_after_refresh_retries_anonymously
  Scenario Outline: Świeży token też odrzucony — LoginFailed i ten sam poll anonimowo
    Given zapytanie /states/all poszło ze świeżo pobranym tokenem
    When OpenSky odpowiada <kod>
    Then stan to LoginFailed z kodem <kod>
    And płytka ponawia ten sam poll bez tokenu
    And następna próba tokenu jest za 30 minut

    Examples:
      | kod |
      | 401 |
      | 403 |

  @test=test_opensky/opensky_auth_test.cpp:test_anonymous_401_is_auth_error
  Scenario Outline: Odrzucone zapytanie anonimowe to błąd autoryzacji
    Given zapytanie /states/all poszło bez tokenu
    When OpenSky odpowiada <kod>
    Then wynik pollu to błąd autoryzacji (AuthError)
    And płytka nie ponawia zapytania

    Examples:
      | kod |
      | 401 |
      | 403 |

  @test=test_opensky/opensky_auth_test.cpp:test_states_200_and_429_are_accepted
  Scenario: Odpowiedzi inne niż 401/403 są przyjmowane bez ponawiania
    Given zapytanie /states/all poszło z tokenem albo bez
    When OpenSky odpowiada 200, 429 albo 500
    Then płytka nie ponawia zapytania
    And stan logowania się nie zmienia

  # ---- Log (§3.2 p. 5)

  @test=test_opensky/opensky_auth_test.cpp:test_auth_log_line_only_on_change
  Scenario: Przejście w LoginFailed daje jedną linię logu, kolejne odrzucenia żadnej
    Given stan Account
    When serwer logowania odrzuca dane kodem 401
    Then linia logu brzmi „login failed (http=401) — check client_id/secret; using anonymous access, next try in 30 min”
    And kolejne odrzucenie w stanie LoginFailed nie daje żadnej linii

  @test=test_opensky/opensky_auth_test.cpp:test_auth_log_line_login_ok
  Scenario: Powrót do konta daje linię „login ok”
    Given stan LoginFailed
    When serwer logowania wydaje token
    Then linia logu brzmi „login ok — using account”

  # ---- Panel WWW, zakładka OpenSky (§3.3, §3.4)

  @test=test_admin/config_page_test.cpp:test_opensky_pill_per_auth_state
  Scenario Outline: Pigułka na zakładce OpenSky odpowiada stanowi logowania
    Given stan logowania <stan> i język panelu <język>
    When panel renderuje zakładkę OpenSky
    Then pigułka ma klasę „<klasa>” i tekst „<tekst>”

    Examples:
      | stan          | język | klasa    | tekst                                   |
      | NoCredentials | PL    | pill     | Tryb anonimowy                          |
      | NoCredentials | EN    | pill     | Anonymous mode                          |
      | Account       | PL    | pill ok  | Konto OpenSky skonfigurowane            |
      | Account       | EN    | pill ok  | OpenSky account configured              |
      | LoginFailed   | PL    | pill err | Błąd logowania — tryb anonimowy         |
      | LoginFailed   | EN    | pill err | Login failed — anonymous mode           |

  @test=test_admin/config_page_test.cpp:test_login_failed_warning_with_code_time_and_retry
  Scenario: W stanie LoginFailed pod pigułką jest ostrzeżenie z kodem HTTP, czasem i następną próbą
    Given stan LoginFailed z kodem 401 od 25.10.2026 14:32 i zsynchronizowany zegar
    And do następnej próby zostało 27 minut
    When panel renderuje zakładkę OpenSky
    Then ostrzeżenie zawiera „HTTP 401”, „25.10.2026 14:32”, limit 400 zamiast 4000 i „27 min”

  @test=test_admin/config_page_test.cpp:test_login_failed_warning_without_clock
  Scenario: Bez zsynchronizowanego zegara ostrzeżenie pomija czas „od”
    Given stan LoginFailed z kodem 401, a zegar nie jest zsynchronizowany
    When panel renderuje zakładkę OpenSky
    Then ostrzeżenie zawiera „HTTP 401”
    But nie zawiera daty ani godziny

  @test=test_admin/config_page_test.cpp:test_no_login_warning_unless_failed
  Scenario: Ostrzeżenia o logowaniu nie ma w stanach Account i NoCredentials
    Given stan logowania Account albo NoCredentials
    When panel renderuje zakładkę OpenSky
    Then nie ma ostrzeżenia o odrzuconych danych logowania

  @test=test_admin/config_page_test.cpp:test_daily_limit_is_anonymous_when_login_failed
  Scenario: W stanie LoginFailed szacunek kredytów dziennie porównywany jest z limitem 400
    Given dane logowania są zapisane, stan LoginFailed, odpytywanie co 30 s (2880 kredytów dziennie)
    When panel renderuje zakładkę OpenSky
    Then szacunek pokazuje limit 400 i ostrzeżenie o przekroczeniu
    And z tymi samymi danymi w stanie Account limit to 4000 bez ostrzeżenia

  @test=test_admin/config_page_test.cpp:test_just_saved_credentials_show_account
  Scenario: Świeżo zapisane inne dane logowania od razu pokazują zieloną pigułkę
    Given stan LoginFailed policzony dla starych danych logowania
    When w konfiguracji są już inne client_id / client_secret
    Then panel pokazuje zieloną pigułkę „konto” i limit 4000
    And nie pokazuje ostrzeżenia o logowaniu

  # ---- Na płytce (kryteria akceptacji §5)

  @manual
  Scenario: Zły client_secret — samoloty dalej widoczne, czerwony „!” i ostrzeżenia w panelu
    Given płytka z poprawnym kontem OpenSky
    When w panelu zostaje zapisany zły client_secret
    Then w ciągu jednego pollu ekran pokazuje samoloty z zieloną kropką i czerwonym „!” w nagłówku
    And panel ma ramkę ostrzeżenia nad zakładkami
    And zakładka OpenSky ma czerwoną pigułkę i ostrzeżenie z HTTP 401

  @manual
  Scenario: Po odrzuceniu jedna linia logu i 30 minut ciszy wobec serwera logowania
    Given płytka przeszła w stan LoginFailed
    When przez 30 minut trwa normalne odpytywanie co 15 s
    Then w logu jest jedna linia „login failed”
    And w tym czasie nie ma żadnego zapytania o token (brak linii „token fetch failed”)

  @manual
  Scenario: Poprawne dane zapisane w panelu przywracają konto
    Given płytka w stanie LoginFailed
    When w panelu zostają zapisane poprawne dane logowania
    Then następny poll używa konta i log mówi „login ok — using account”
    And pigułka na zakładce OpenSky jest zielona, a „!” znika

  @manual
  Scenario: Bez danych logowania płytka działa jak przed tą zmianą
    Given płytka bez client_id / client_secret
    When działa przez kilka pollów
    Then samoloty są widoczne, kropka zielona, nie ma „!” ani zapytań o token
