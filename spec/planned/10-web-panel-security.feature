Feature: Bezpieczeństwo panelu WWW — żądania z innych stron odrzucone, żadnych zmian stanu przez GET, panel nie daje się osadzić
  Obca strona otwarta w tej samej sieci nie może zmienić ustawień, wgrać kopii ani zainstalować
  wydania (sprawdzanie Origin / Referer przed hasłem i przed buforowaniem uploadu), przełącznik
  języka i zrzut ekranu nie zmieniają już zapisanego stanu przez GET, a panel nie wyświetla się
  w cudzej ramce. curl i `pio run -e cyd-ota -t upload` działają jak dziś.

  # ---- 2.1 checkRequestOrigin (web_auth): czy żądanie przyszło z samego panelu

  @test=test_admin/web_auth_test.cpp:test_origin_with_same_host_is_same_origin
  Scenario Outline: Origin z tym samym hostem co Host żądania to to samo pochodzenie
    Given żądanie z nagłówkiem Host "<host>"
    When nagłówek Origin to "<origin>", a Referer jest pusty
    Then checkRequestOrigin zwraca SameOrigin

    Examples:
      | host             | origin                  |
      | cyd-sky.local    | http://cyd-sky.local    |
      | cyd-sky.local    | http://cyd-sky.local:80 |
      | cyd-sky.local:80 | http://cyd-sky.local    |
      | 192.168.1.50     | http://192.168.1.50     |
      | cyd-sky.local    | http://CYD-Sky.Local    |
      | CYD-SKY.LOCAL    | http://cyd-sky.local    |

  @test=test_admin/web_auth_test.cpp:test_origin_from_elsewhere_is_cross_origin
  Scenario Outline: Origin z innego hosta, innego schematu albo „null” to inne pochodzenie
    Given żądanie z nagłówkiem Host "cyd-sky.local"
    When nagłówek Origin to "<origin>"
    Then checkRequestOrigin zwraca CrossOrigin

    Examples:
      | origin                         |
      | http://evil.example            |
      | http://192.168.1.50            |
      | https://cyd-sky.local          |
      | null                           |
      | http://cyd-sky.local.evil.com  |
      | http://cyd-sky.local:8080      |
      | cyd-sky.local                  |

  @test=test_admin/web_auth_test.cpp:test_referer_alone_decides_when_origin_missing
  Scenario Outline: Bez Origin decyduje host z Referer
    Given żądanie z nagłówkiem Host "cyd-sky.local" i bez nagłówka Origin
    When nagłówek Referer to "<referer>"
    Then checkRequestOrigin zwraca <wynik>

    Examples:
      | referer                                  | wynik       |
      | http://cyd-sky.local/?tab=general        | SameOrigin  |
      | http://cyd-sky.local:80/                 | SameOrigin  |
      | http://evil.example/attack.html          | CrossOrigin |
      | http://cyd-sky.local.evil.com/x?h=y      | CrossOrigin |

  @test=test_admin/web_auth_test.cpp:test_origin_wins_over_referer
  Scenario: Origin ma pierwszeństwo przed Referer
    Given żądanie z nagłówkiem Host "cyd-sky.local"
    When Origin to "http://evil.example", a Referer to "http://cyd-sky.local/"
    Then checkRequestOrigin zwraca CrossOrigin

  @test=test_admin/web_auth_test.cpp:test_no_origin_and_no_referer_is_allowed
  Scenario: Brak Origin i Referer (curl, wgrywanie OTA z PlatformIO) jest dozwolony
    Given żądanie z nagłówkiem Host "cyd-sky.local"
    When nie ma ani Origin, ani Referer
    Then checkRequestOrigin zwraca NoHeaders
    And takie żądanie nie jest odrzucane jako żądanie z innej strony

  @test=test_admin/web_auth_test.cpp:test_origin_present_but_no_host_is_cross_origin
  Scenario: Origin bez nagłówka Host nie przechodzi
    Given żądanie bez nagłówka Host
    When Origin to "http://cyd-sky.local"
    Then checkRequestOrigin zwraca CrossOrigin

  # ---- Kolejność: pochodzenie przed hasłem i przed zapisem uploadu

  @test=test_admin/web_auth_test.cpp:test_cross_origin_upload_refused_before_credentials
  Scenario: Upload z innej strony jest odrzucany, zanim cokolwiek zostanie zapisane — nawet z dobrym hasłem
    Given panel ma hasło, a przeglądarka dołącza do żądania poprawne dane logowania
    When przychodzi początek uploadu (UPLOAD_FILE_START) z Origin innej strony
    Then decyzja to „odrzuć: inne pochodzenie”, nie „przyjmij”
    And przy tym samym pochodzeniu z błędnym hasłem decyzja to „odrzuć: hasło”
    And bez Origin i Referer z poprawnym hasłem decyzja to „przyjmij”

  @test=test_admin/web_auth_test.cpp:test_cross_origin_log_line_names_path_and_origin
  Scenario: Odrzucone żądanie zostawia jedną linię w logu
    Given POST /save z Origin "http://evil.example" został odrzucony
    When powstaje linia logu
    Then brzmi "[auth] cross-origin POST /save from http://evil.example refused"
    And gdy był tylko Referer, w linii stoi Referer zamiast Origin

  # ---- 2.3 Nagłówki odpowiedzi

  @test=test_admin/web_auth_test.cpp:test_security_headers_deny_framing_and_allow_no_cors
  Scenario: Odpowiedzi panelu zabraniają osadzania i nie otwierają CORS
    Given lista nagłówków bezpieczeństwa wysyłanych z każdą odpowiedzią panelu
    Then zawiera "X-Frame-Options: DENY"
    And zawiera "Content-Security-Policy: frame-ancestors 'none'"
    And nie zawiera żadnego nagłówka "Access-Control-Allow-*"

  # ---- 2.2 Przełącznik języka przez POST

  @test=test_admin/config_page_test.cpp:test_language_switch_is_post_forms_without_lang_links
  Scenario Outline: Przełącznik PL/EN to dwa formularze POST, bez linków GET
    Given strona ustawień w języku <język>
    When strona jest renderowana
    Then nagłówek ma dwa formularze method="post" action="/lang" z przyciskami PL i EN
    And przycisk bieżącego języka ma klasę "on"
    And na stronie (HTML i skrypt) nie ma "href=\"/lang?" ani "/lang?l="

    Examples:
      | język |
      | PL    |
      | EN    |

  @test=test_admin/config_page_test.cpp:test_language_forms_carry_the_current_tab
  Scenario: Formularze języka wracają na tę samą zakładkę
    Given strona ustawień otwarta na zakładce Oprogramowanie
    When strona jest renderowana
    Then oba formularze języka mają ukryte pole tab o wartości "firmware"
    And skrypt przełączania zakładek uaktualnia to pole, a nie adresy linków

  # ---- 2.2 Zrzut ekranu nie zapisuje ekranu i wraca na poprzedni

  @test=test_display/screen_nav_test.cpp:test_screenshot_screen_change_is_never_persisted
  Scenario Outline: Zmiana ekranu na czas zrzutu nigdy nie trafia do NVS
    Given od ostatniego zapisu ekranu minęło <ms> ms
    When ekran zmienia się na potrzeby zrzutu ekranu
    Then shouldPersistScreenChange zwraca false

    Examples:
      | ms         |
      | 0          |
      | 600000     |
      | 4294967295 |

  @test=test_display/screen_nav_test.cpp:test_touch_and_auto_cycle_persistence_unchanged
  Scenario: Dotyk i auto-cykl zapisują ekran jak dotąd
    Given ekran zmienia się dotykiem albo przez auto-cykl
    Then dotyk zapisuje zawsze
    And auto-cykl zapisuje dopiero po kScreenPersistMinIntervalMs od poprzedniego zapisu

  @test=test_admin/screenshot_test.cpp:test_screen_returns_to_previous_after_capture
  Scenario: Po zrzucie innego ekranu urządzenie wraca na ekran sprzed zrzutu
    Given urządzenie pokazuje ekran 0 (LOTY)
    When przychodzi zrzut z screen=2 i kończy się przez end()
    Then w trakcie zrzutu pokazany jest ekran 2
    And po zrzucie powrót wskazuje ekran 0, dokładnie raz

  @test=test_admin/screenshot_test.cpp:test_screen_returns_after_abandoned_capture
  Scenario: Powrót następuje także, gdy klient zniknął w trakcie zrzutu
    Given trwa zrzut z screen=1, a urządzenie pokazywało wcześniej ekran 2
    When klient przestaje prosić o pasy i mija ponad 5 s bez aktywności
    Then po wznowieniu rysowania powrót wskazuje ekran 2

  @test=test_admin/screenshot_test.cpp:test_no_return_when_capture_did_not_switch_screen
  Scenario Outline: Bez przełączenia ekranu nie ma do czego wracać
    Given urządzenie pokazuje ekran 1
    When przychodzi zrzut z <parametr> i się kończy
    Then powrót nie wskazuje żadnego ekranu

    Examples:
      | parametr                          |
      | bez screen                        |
      | screen=1 (ten sam ekran)          |
      | screen=3 (demo pytania o wersję)  |
      | screen=4 (demo ekranu ostrzeżeń)  |

  # ---- Na płytce / w przeglądarce

  @manual
  Scenario: Formularz POST na /save z obcej strony dostaje 403 i niczego nie zmienia
    Given strona testowa z innego hosta (python -m http.server na laptopie) z formularzem POST na http://cyd-sky.local/save
    When użytkownik wysyła formularz w przeglądarce
    Then odpowiedź to 403 "Cross-site request refused"
    And ustawienia na zakładkach są takie same jak przed próbą
    And w logu jest "[auth] cross-origin POST /save from http://<host-laptopa>:8000 refused"

  @manual
  Scenario: Upload firmware z obcej strony jest odrzucany bez zapisu do flash
    Given strona testowa z innego hosta z formularzem multipart POST na http://cyd-sky.local/update
    When użytkownik wysyła plik firmware
    Then odpowiedź to 403, w logu nie ma "[ota] start", a urządzenie się nie restartuje

  @manual
  Scenario: Panel działa jak dotąd z własnej strony i z PlatformIO
    Given panel otwarty pod http://cyd-sky.local (z hasłem i bez)
    When użytkownik zapisuje ustawienia, przełącza PL/EN, robi kopię i ją przywraca, wgrywa OTA z zakładki Oprogramowanie, klika „Sprawdź teraz”
    And wgrywa firmware przez pio run -e cyd-ota -t upload
    Then wszystko działa jak przed zmianą, PL/EN wraca na tę samą zakładkę

  @manual
  Scenario: GET /lang nie zmienia już języka
    Given język panelu to PL
    When curl -i "http://cyd-sky.local/lang?l=en"
    Then odpowiedź to 405 z nagłówkiem Allow: POST
    And język panelu dalej jest PL

  @manual
  Scenario: Zrzuty ekranu nie zmieniają ekranu zapamiętanego po restarcie
    Given użytkownik wybrał dotykiem ekran RADAR
    When uruchamia python tools/screenshots.py --device cyd-sky.local oraz curl "http://cyd-sky.local/screenshot.bmp?screen=0"
    Then po każdym zrzucie urządzenie wraca na RADAR
    And po restarcie urządzenie startuje na RADAR, a nie na ostatnim ekranie ze zrzutu

  @manual
  Scenario: Panel osadzony w ramce na obcej stronie się nie wyświetla
    Given strona testowa z innego hosta z <iframe src="http://cyd-sky.local/">
    When użytkownik otwiera ją w przeglądarce
    Then ramka jest pusta (przeglądarka zgłasza odmowę osadzenia)
    And curl -I http://cyd-sky.local/ pokazuje X-Frame-Options: DENY i Content-Security-Policy: frame-ancestors 'none', bez Access-Control-Allow-Origin
