# Senior embedded review — CYD Sky Tracker

Przegląd modułów: `wifi_manager`, `config_store`, `config_portal`, `opensky_client`,
`aircraft_lookup`, `lcars_theme`, `table_view`, `main.cpp`. Numeracja `X.Y` do
wskazywania konkretnych pozycji do wdrożenia.

**Status:** 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.1, 4.1, 4.2, 4.3, 5.1, 5.2, 5.4, 5.5
wprowadzone (patrz adnotacje `✅ WPROWADZONE` przy każdej pozycji, z testami
natywnymi tam gdzie logika była czysta). 5.6 celowo pominięte — wymaga fizycznej
kalibracji na sprzęcie, nie da się tego "naprawić" w kodzie. Pozostałe pozycje
(1.7, 2.2, 2.3, 4.4, 4.5, 4.6, 4.7, 5.3, 5.7, 5.8) nie były proszone o wdrożenie —
zostawione jako obserwacje/pozycje "OK, bez akcji" z oryginalnego przeglądu.

## 1. Obsługa błędów

Żadna ze sprawdzonych ścieżek błędów nie prowadzi do zawieszenia czy crasha —
`fetchAircraftStates`, `lookupAircraft`, `openSkyClientPoll` zawsze kończą się
zwróceniem pustego wyniku/`false` i logiem na Serial, nigdy wyjątkiem czy pętlą
nieskończoną. Ale jest kilka miejsc, gdzie degradacja jest *cicha* albo *zbyt długa*:

1.1 **[KRYTYCZNE] `loop()` blokuje się synchronicznie na wywołaniach HTTP.**
`openSkyClientPoll()` → `fetchAircraftStates()` robi do 2 sekwencyjnych requestów
(token + states, albo states + retry po 401) *w tej samej klatce `loop()`*, a pętla
`for (ac : aircraft) lookupAircraft(ac.icao24)` w `main.cpp:141` robi to
**synchronicznie dla każdego samolotu z osobna** — przy pierwszym pollu z 15 nowymi
samolotami to 15 kolejnych blokujących żądań do hexdb.io. W żadnym miejscu
(`opensky_client.cpp`, `aircraft_lookup.cpp`) nie ma `http.setTimeout()`/
`setConnectTimeout()` (zweryfikowane grepem — zero wystąpień), więc czas trwania
zależy wyłącznie od domyślnego timeoutu `HTTPClient`. W tym oknie `wifiManagerLoop()`,
`configPortalLoop()` i dotyk **nie są obsługiwane** — to podważa "nieblokującą"
filozofię z CLAUDE.md głębiej niż tylko na poziomie `poll_interval_s`.

> ✅ **WPROWADZONE (częściowo — bounded, nie eliminuje blokowania):**
> `opensky_client`/`aircraft_lookup` mają teraz jawne `setConnectTimeout(5000)`/
> `setTimeout(8000)` (patrz 5.1), więc pojedyncze wywołanie ma znany, ograniczony
> czas zamiast nieudokumentowanego defaultu biblioteki. Dodatkowo `main.cpp` ma
> teraz `kMaxNewLookupsPerPoll = 5` — nowe (niecache'owane) `aircraft_lookup`
> ograniczone do 5 requestów HTTP na cykl pollingu; reszta samolotów renderuje się
> jako "--" i zostaje dociągnięta w kolejnych cyklach (`aircraftLookupIsCached()`,
> nowa czysta funkcja z testami). Pełne wyeliminowanie blokowania w
> `opensky_client` (token+states, do 4 requestów w najgorszym razie) wymagałoby
> przepisania na asynchroniczny state machine — poza zakresem tej poprawki,
> uczciwie zostawione jako known limitation.

1.2 **[WYSOKIE] `aircraft_lookup` cache'uje błąd transportu tak samo jak realny
"not found".** `httpFetchAircraftJson()` przy niepowodzeniu (`http.begin()` fail,
`GET()` fail, timeout, zerwane WiFi) zwraca `""` → `parseAircraftLookupResponse("")`
→ `found=false` → **to trafia do cache'a na stałe** (`aircraft_lookup.cpp:44`),
nieodróżnialne od prawdziwej odpowiedzi 404 z hexdb.io. Samolot, który akurat trafił
na chwilowy zanik łącza, zostaje permanentnie oznaczony jako "—" do końca sesji,
nawet gdy WiFi wróci. To bezpośrednio łączy się z pytaniem o "timeout z
aircraft_lookup" — obecnie timeout = trwały fałszywy negatyw.

> ✅ **WPROWADZONE.** `AircraftFetchFn` zwraca teraz `FetchResult{response_ok, body}`
> zamiast gołego `std::string`; `response_ok` jest `true` tylko dla definitywnego
> HTTP 200. `lookupAircraftWithFetcher()` cache'uje wynik tylko gdy `response_ok`
> — błąd transportu/timeout/non-200 zwraca `found=false` bez zapisu do cache'a, więc
> kolejny poll spróbuje ponownie. Dodano `aircraftLookupIsCached()` (czysta funkcja
> query). Testy: `test_lookup_does_not_cache_transport_failure`,
> `test_lookup_recovers_after_transport_failure`, `test_is_cached_reflects_confirmed_results_only`.

1.3 **[WYSOKIE] Wygasły portal setupu = ślepy zaułek.** `kPortalTimeoutS = 180`
w `wifi_manager.cpp`. Jeśli po 3 minutach nikt nie skonfiguruje sieci, WiFiManager
zamyka portal, ale `deriveWifiStatus(false, false)` nadal zwraca `Connecting` — nic
już nie próbuje się połączyć ani nie ma aktywnego portalu. Urządzenie utyka bez
żadnej ścieżki powrotu poza fizycznym resetem.

> ✅ **WPROWADZONE.** Nowa czysta funkcja `shouldRestartConnection(portal_was_active,
> portal_active_now, wifi_connected)` w `wifi_manager` wykrywa "portal właśnie się
> zamknął, a nie ma połączenia" i `wifiManagerLoop()` wtedy ponownie woła
> `wm.autoConnect()` — portal otwiera się od nowa zamiast utykać. 4 testy natywne
> pokrywające wszystkie kombinacje przejść.

1.4 **[ŚREDNIE] Backoff po 429 nie jest bezpieczny na przewinięcie `millis()`.**
`if (rateLimitedUntilMs != 0 && now < rateLimitedUntilMs)` w `opensky_client.cpp:210`
używa gołego `<`, w przeciwieństwie do `tokenNeedsRefresh`/`openSkyClientPoll`, które
poprawnie liczą różnicę przez odejmowanie. Jeśli 429 przydarzy się blisko
przewinięcia licznika (~49 dni pracy), backoff może "utknąć" na bardzo długo.

> ✅ **WPROWADZONE.** Nowa czysta funkcja `isBeforeDeadline(now_ms, until_ms)` w
> `opensky_client`, ten sam wzorzec odejmowania co `tokenNeedsRefresh`. Backoff po
> 429 teraz jej używa. 2 testy natywne, w tym jawny przypadek przewinięcia licznika.

1.5 **[ŚREDNIE] Brak wykrywania "nie skonfigurowano jeszcze lokalizacji".**
`home_lat`/`home_lon` domyślnie `0.0f, 0.0f` (zweryfikowane grepem — nigdzie nie ma
flagi typu `configured`/`first_run`). Jeśli użytkownik nie odwiedzi jeszcze
`config_portal`, aplikacja normalnie odpytuje/wyświetla samoloty koło (0,0) — bez
żadnego sygnału dla użytkownika, że coś jest nieskonfigurowane.

> ✅ **WPROWADZONE.** Nowe pole `Config::home_configured` (domyślnie `false`,
> persystowane w NVS jako `home_cfg`), ustawiane na `true` przez `config_portal`'s
> `handleSave()` przy każdym zapisie formularza ustawień. `main.cpp` sprawdza tę
> flagę po każdym pollu — jeśli `false`, pokazuje ekran "Set your home location at
> http://cyd-sky.local" zamiast tabeli (rysowany raz, nie co poll). Test:
> `test_sanitize_config_preserves_home_configured_flag` +
> domyślna wartość w `test_default_config_has_sane_values`.

1.6 **[NISKIE] `config_portal`'s `toFloat()` cicho zamienia niepoprawny input na
0.0**, które potem `sanitizeConfig` podciąga do dolnego limitu — brak informacji
zwrotnej dla użytkownika o błędnym wpisie.

> ✅ **WPROWADZONE.** Nowe czyste funkcje `isValidFloatString()`/
> `isValidUnsignedIntString()` w `config_portal`. `handleSave()` waliduje
> lat/lon/radius/poll_interval przed przypisaniem — nieprawidłowe pole zostaje przy
> starej wartości, a strona pokazuje czerwony komunikat z nazwami odrzuconych pól
> zamiast cicho zapisywać 0. 4 testy natywne (typowe wartości, whitespace, różne
> rodzaje śmieci wejściowych).

1.7 **[NISKIE] Brak rozróżnienia "poll się nie udał" vs "legalnie 0 samolotów
w zasięgu"** — w obu przypadkach tabela po prostu jest pusta; jedyny ślad to log na
Serial.

## 2. Zgodność z granicami modułów

2.1 **`main.cpp` używa `TFT_BLACK` bezpośrednio** (linie 58, 105) zamiast
`LCARS_BLACK` z `lcars_theme` — wartości są numerycznie identyczne (0x0000), ale to
omija zasadę "lcars_theme to jedyne miejsce definiujące kolory" z CLAUDE.md.

> ✅ **WPROWADZONE.** Wszystkie wystąpienia `TFT_BLACK` w `main.cpp` zamienione na
> `LCARS_BLACK` (`setup()`, `redrawTable()`, nowy `showSetupPrompt()`). Bez testu —
> to prosta podmiana stałej, nie logika.

2.2 **(potwierdzone czyste)** `opensky_client.cpp`/`aircraft_lookup.cpp` — zero
odwołań do `LGFX`/TFT (grep po `LGFX_CYD.hpp`/`LovyanGFX.hpp` w całym `src/` pokazuje
tylko `lcars_theme.h`, `table_view.h`, `main.cpp`). `table_view.cpp` — zero kodu
sieciowego (żadnych wywołań `opensky_client`/`aircraft_lookup`, tylko ich typy).

2.3 **`drawElbow`/`drawPanel` są w pełni zaimplementowane, ale nigdzie nie
wywołane.** Aktualnie na ekranie rysowany jest tylko przycisk toggle — nie ma
stałego elementu ramki "elbow sidebar", którego CLAUDE.md wymaga jako trwałego
elementu chrome na obu widokach. To nie jest naruszenie granic modułów, tylko luka
względem sekcji "Design language".

## 3. Pokrycie testami

Sprawdzone grepem, każda czysta funkcja z nagłówków przeciwko `test/*.cpp` (nie
zakładane, tylko zweryfikowane):

| Moduł | Funkcja | Test |
|---|---|---|
| config_store | `viewModeFromValue`, `defaultConfig`, `sanitizeConfig`, `clampRadiusDeg`, `clampPollIntervalS` | ✅ test_config_store.cpp |
| config_portal | `bboxAreaSqDeg`, `openSkyCreditCost`, `parseOpenSkyCredentialsJson` | ✅ test_config_portal.cpp |
| opensky_client | `computeBoundingBox`, `tokenNeedsRefresh`, `shouldUseOAuth`, `parseStatesResponse`, `parseRetryAfterSeconds`, `urlEncode` | ✅ test_opensky_client.cpp |
| aircraft_lookup | `parseAircraftLookupResponse`, `lookupAircraftWithFetcher` (cache) | ✅ test_aircraft_lookup.cpp |
| lcars_theme | `rgb565`, `elbowArcPoint`, `viewToggleButtonBounds` | ✅ test_lcars_theme.cpp |
| table_view | `computeDistanceBearing`, `buildEnrichedRecords`, `annotateDistances`, `sortRowsByDistance`, `rowsPerPage`, `getPageCount`, `getPageSlice` | ✅ test_table_view.cpp |
| touch_input | `hitTest` | ✅ test_touch_input.cpp |
| wifi_manager | `deriveWifiStatus`, `wifiStatusLabel` | ✅ test_wifi_manager.cpp |

**Wynik: 29/29 czystych funkcji ma pokrycie.** Nie znaleziono żadnej funkcji
spełniającej kryterium "czysta, bez zależności od hardware/SDK" bez testu.

## 4. Zasoby ESP32

4.1 **`config_portal::renderForm()` to najcięższy użytkownik `String`** (35
wystąpień w pliku, licząc konkatenacje) — cała strona HTML (~2KB+) budowana przez
łańcuch `String +=`, na każde żądanie web (`GET /`, `POST /save`,
`POST /upload_credentials`). Największe ryzyko fragmentacji heapa w projekcie.

> ✅ **WPROWADZONE.** `renderForm()` (budująca jeden duży `String`) zastąpiona przez
> `sendConfigPage()`, która strumieniuje odpowiedź przez `server.sendContent()`:
> statyczne fragmenty HTML jako literały `const char*` (zero alokacji, żyją we
> flash/rodata), wartości liczbowe (lat/lon/radius/poll_interval/credit) formatowane
> przez `snprintf` do małych buforów na stosie zamiast konkatenacji `String`.
> `server.setContentLength(CONTENT_LENGTH_UNKNOWN)` + chunked transfer encoding
> (zweryfikowane wprost w źródle biblioteki `WebServer.cpp`). Helper `sendChunk()`
> pomija wywołania z pustym stringiem (pusta treść ucinałaby strumień chunked
> przedwcześnie — sprawdzone w bibliotece i celowo obsłużone).

4.2 **`opensky_client`/`aircraft_lookup` budują URL-e/body przez `String`**
(odpowiednio co ~15s i tylko przy cache miss) — ten sam wzorzec, niższe ryzyko dzięki
niższej częstotliwości.

> ✅ **WPROWADZONE.** URL `/states/all` (`requestStatesOnce`), body tokena OAuth
> (`fetchToken`), nagłówek `Authorization` i URL hexdb.io (`httpFetchAircraftJson`)
> budowane teraz przez `snprintf` do buforów na stosie (`char[192]`, `char[512]`,
> `char[2048]`, `char[64]`) zamiast łańcuchów `String + String + ...`. Konwersja na
> `String` wciąż zachodzi *raz*, na granicy wywołania `http.begin()`/`addHeader()`
> (API `HTTPClient` nie ma przeciążeń `const char*` dla tych metod — zweryfikowane w
> źródle), ale bez pośrednich, wielokrotnie realokowanych konkatenacji. `POST` body
> tokena wysyłane przez `http.POST(uint8_t*, size_t)` — całkowicie bez `String`.
> Buforom towarzyszy sprawdzenie przepełnienia (`snprintf` zwraca `written >= sizeof`)
> — zbyt długie dane traktowane jako błąd, nie ucięty/zepsuty request.

4.3 **`JsonDocument doc;` (ArduinoJson v7.4.3) bez żadnego limitu rozmiaru** —
używane zarówno dla `/states/all`, jak i hexdb.io. To "elastyczny" dokument bez
capacity (potwierdzone w źródle biblioteki), rośnie przez realokację bez sztywnego
sufitu. `radius_deg` jest wprawdzie zaciśnięty do max 10° (400 sq°, próg
3-kredytowy) przez `config_store`, więc rozmiar nie jest *nieograniczony*, ale dla
gęstej przestrzeni powietrznej to wciąż potencjalnie 100+ samolotów w jednej
odpowiedzi bez żadnego defensywnego capa. `deserializeJson` poprawnie zwraca błąd
przy OOM zamiast crashować (sprawdzone w kodzie) — więc nie ma crasha, ale nie ma
też żadnej ochrony przed fragmentacją przy dużych odpowiedziach.

> ✅ **WPROWADZONE.** Dodane jawne limity rozmiaru sprawdzane *przed* próbą
> parsowania: `kMaxStatesResponseBytes = 64KB` (opensky_client),
> `kMaxLookupResponseBytes = 4KB` (aircraft_lookup),
> `kMaxCredentialsJsonBytes = 4KB` (config_portal, redundantnie z istniejącym
> `kMaxUploadBytes` na poziomie handlera uploadu — świadome "belt and suspenders").
> Wszystkie trzy jako nazwane stałe w nagłówkach (nie zakopane w .cpp), z testami
> natywnymi na przekroczenie limitu.

4.4 **Treść odpowiedzi jest kopiowana 2-3× przed parsowaniem**: bufor wewnętrzny
`HTTPClient` → `String payload`/`getString()` → konwersja na `std::string body` →
własna kopia `JsonDocument`. Dla dużej odpowiedzi `/states/all` to kilkukrotność
rozmiaru payloadu jednocześnie w RAM-ie. ArduinoJson wspiera deserializację
strumieniową bezpośrednio z `Stream`/`WiFiClient`, co ominęłoby pośrednie kopie
`String`/`std::string`.

4.5 **`getPageSlice()` zwraca pełną kopię** do `rowsPerPage` (~10) rekordów
`AircraftRow` (każdy z kilkoma polami `std::string`) przy każdym przerysowaniu —
drobne, ograniczone, ale unikalne przez indeksy/span zamiast kopiowanego wektora.

4.6 **(pozytywne)** `table_view.cpp` w ogóle nie używa `String` — wyłącznie
`std::string` + stałe bufory `char` + `snprintf`. Dokładnie właściwy wzorzec.

4.7 Nie znaleziono ryzyka przepełnienia stosu w callbackach — handler przerwania
XPT2046 żyje wewnątrz biblioteki (oznaczony `IRAM_ATTR`), nasze callbacki
`WebServer`/`HTTPUpload` mają tylko mały lokalny stan.

## 5. Magic numbers

5.1 Brak nazwanych stałych timeout HTTP (patrz 1.1/5.1 razem) — żaden
`setTimeout()`/`setConnectTimeout()` nigdzie w projekcie; obecnie zależny wyłącznie
od domyślnej wartości biblioteki.

> ✅ **WPROWADZONE.** `kHttpConnectTimeoutMs = 5000`, `kHttpTimeoutMs = 8000` jako
> nazwane stałe w `opensky_client.cpp` i `aircraft_lookup.cpp` (po jednym zestawie
> na plik, ustawiane przez `http.setConnectTimeout()`/`setTimeout()` przed każdym
> requestem). Zweryfikowane w źródle `HTTPClient.h`, że te metody istnieją z
> zakładanymi sygnaturami.

5.2 `parseRetryAfterSeconds(retryAfter, 60)` — domyślny backoff `60` to literał w
miejscu wywołania, nie nazwana stała (`kDefaultRetryAfterS` czy podobne).

> ✅ **WPROWADZONE.** `kDefaultRetryAfterS = 60` jako nazwana stała w
> `opensky_client.cpp`, użyta w miejscu dawnego literału.

5.3 `config_portal`'s `kMaxUploadBytes = 4096` — już nazwana stała, OK, bez akcji.

5.4 **Ułamki szerokości kolumn w `drawTablePage`** (`0.34f`, `0.50f`, `0.68f`,
`0.80f`, `0.90f`) — niezanazwane magiczne liczby wprost w kodzie rysującym.

> ✅ **WPROWADZONE.** Nazwane jako `kColAirlineFrac`/`kColFlightFrac`/
> `kColTypeFrac`/`kColAltFrac`/`kColSpeedFrac`/`kColDistFrac` w `table_view.cpp`.

5.5 **`kPageTapZoneHeight` (main.cpp) i `kRowHeight` (table_view.cpp) to
niezależnie zdefiniowane, przypadkowo równe (20) magiczne liczby w dwóch różnych
plikach** — nie mają wspólnego źródła; zmiana jednej nie zaktualizuje drugiej.

> ✅ **WPROWADZONE.** Nowa publiczna funkcja `tableRowHeightPx()` w `table_view`
> zwraca `kRowHeight` (jedyne źródło prawdy). `main.cpp`'s `kPageTapZoneHeight`
> inicjalizowane teraz z `tableRowHeightPx()` zamiast własnego literału `20`. Test:
> `test_table_row_height_px_matches_rows_per_page` (spina `tableRowHeightPx()` z
> zachowaniem `rowsPerPage()`).

5.6 **Kalibracja surowych współrzędnych XPT2046** (`kRawXMin/Max`, `kRawYMin/Max` =
200–3900) w `touch_input.cpp` — już oznaczona w komentarzu jako niezweryfikowany
placeholder, powtórzona tu jako najbardziej praktycznie istotna "magiczna liczba"
w projekcie (zła kalibracja = dotyk w ogóle nie działa).

> ⏭️ **CELOWO POMINIĘTE.** To wymaga fizycznego dotyku ekranu do kalibracji — nie
> da się tego "naprawić" w kodzie bez dostępu do sprzętu. Stałe już są nazwane, w
> jednym miejscu, z komentarzem ostrzegawczym — nic więcej nie da się sensownie
> zrobić tutaj bez płytki na stole.

5.7 Rozmiar/margines przycisku toggle (54×22, margines 4) i wysokość wiersza
(20px) w `lcars_theme` — już nazwane stałe, OK.

5.8 Progi `config_store` (promień 0.1–10°, min. poll 5s, ±90/±180) — już nazwane
stałe w `config_store.cpp`, OK, bez akcji.
