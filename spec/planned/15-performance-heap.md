# Spec 15 — Wydajność: pamięć, która się nie tnie, i tańszy poll

Źródło: ROADMAP „What's next” (przegląd wydajności 2026-11-09). Status: **planowane**.

Przegląd kodu i mapy linkera (v1.11.4-39) pokazał, że flash nie jest problemem (1,32 MB z
1,875 MB slotu), a wąskim gardłem jest sterta rdzenia 0: największy wolny blok spada do ~41 KB
(docs/history.md „Concurrency and resources”) i trzeba go bronić progami `kUpdateMinFreeBlock` /
`kUpdateInstallMinFreeBlock`. Ta specyfikacja usuwa znane przyczyny, zamiast je obchodzić, i przy
okazji zdejmuje kilka stałych kosztów z pętli sieciowej i z rysowania.

**Wszystko poniżej wynika z czytania kodu, nie z pomiaru.** Dlatego etap 0 to pomiar, a każdy
następny etap kończy się tym samym pomiarem. Etap, który nie daje mierzalnej poprawy ani nie
usuwa ryzyka awarii, nie wchodzi (wpis w docs/history.md, dlaczego).

Cel, w liczbach do potwierdzenia na płytce:

- największy wolny blok po 24 h pracy ≥ 50 KB (dziś: 41–69 KB, potrafi osiąść przy 41 KB);
- poll z 300 samolotami w zasięgu nie restartuje płytki (dziś: brak limitu, `abort()` przy
  braku pamięci, bo build jest bez wyjątków);
- `net busy` w spoczynku (między pollami) wyraźnie niżej niż dziś;
- radar przerysowuje się bez mrugnięcia.

Zachowanie urządzenia nie zmienia się poza dwoma miejscami opisanymi wprost: limit śledzonych
samolotów (p. 2) i radar bez mrugania (p. 8).

## 0. Pomiar wyjściowy (przed jakąkolwiek zmianą)

- Nowe pola w `PerfReport` / `[perf]` / `GET /perf.json` (`perf_stats`, czyste, testowane):
  - `minBlock` — najmniejszy „największy wolny blok” od startu (odczyt raz na okno 60 s, jak
    dziś `maxBlock`, nie częściej);
  - `rows` — liczba wierszy w ostatnim `AirspaceSnapshot` i `inRange` — ile samolotów przysłał
    OpenSky;
  - `pollMs` — czas ostatniego pollu rozbity na: żądanie + parsowanie, fazę A, fazę B.
- Scenariusz pomiaru, zapisany w `docs/architecture.md` „Concurrency — two cores” (akapit
  „Perf”), żeby dało się go powtórzyć:
  1. zwykła praca, promień domyślny, 30 min od startu;
  2. promień 10° (maksimum z `clampRadiusDeg()`), 30 min;
  3. 24 h zwykłej pracy (wartości `minBlock`, `heap`, `net busy`).
- Wynik: tabela „przed” w tej specyfikacji (sekcja „Pomiary” na dole), uzupełniana po każdym
  etapie o kolumnę „po”.

## 1. Stałe koszty pętli `net_task` (co 5 ms)

Pętla robi 200 przebiegów na sekundę; wszystko, co jest w niej bezwarunkowo, kosztuje stale.

- **`ESP.getMaxAllocHeap()` w `updateGate()`** (`src/main/net_task.cpp`): przechodzi całą
  stertę pod jej blokadą 200 razy na sekundę, czyli wstrzymuje też alokacje UI. Zmiana:
  `CheckGate::maxFreeBlock` liczone leniwie — tylko gdy `release_check` faktycznie ma co
  uruchomić (instalacja w kolejce, „sprawdź teraz”, należne sprawdzenie). W czystej logice:
  `releaseCheckWantsGate(now)` (albo `CheckGate` z funkcją zamiast pola) — decyzja testowana
  natywnie: bez zadania do uruchomienia odczyt sterty nie jest wołany ani razu.
- **`deviceAlertInputs()`** (`src/main/alert_sources.cpp`): `openSkyAuthStatus()` liczy
  `credentialsHash()` z danych logowania w każdym przebiegu. Zmiana: hash liczony przy
  załadowaniu ustawień (`reloadSettings()`) i trzymany obok `cfg`; `refreshAlerts()` wołane po
  zdarzeniach, które mogą zmienić alerty (poll, zapis ustawień, zmiana stanu logowania, zmiana
  decyzji podświetlenia), a nie w każdym przebiegu. Alert nie może się przez to spóźnić o więcej
  niż jeden przebieg po zdarzeniu — test na każdym ze źródeł.
- `publishIfChanged()` zostaje w każdym przebiegu (tanie porównanie), ale bez `refreshAlerts()`
  w środku.

## 2. Limit śledzonych samolotów

Dziś liczbę wierszy ogranicza tylko `kMaxStatesResponseBytes` (64 KB odpowiedzi ≈ kilkaset
samolotów). `TrackedFlight` to kilkaset bajtów plus napisy na stercie, a `std::vector` to jeden
ciągły blok; w szczycie żyją trzy listy naraz (ta u UI, faza A, faza B).

- `kMaxTrackedFlights = 48` (`airspace/core`), do skorygowania po pomiarze z etapu 0.
- Wybór **przed** budową `TrackedFlight`: nowa czysta klasa `NearestKeeper`
  (`airspace/core/nearest_keeper.{h,cpp}`) przyjmuje `AircraftState` po jednym i trzyma najwyżej
  N najlepszych według tej samej kolejności co `sortRowsForDisplay()`: w powietrzu od
  najbliższego, potem na ziemi od najbliższego, na końcu bez pozycji. Jedno źródło kolejności —
  wspólna funkcja porównująca, używana przez obie.
- `AirspaceSnapshot::inRange` — ile samolotów było w odpowiedzi. Nagłówek dalej pokazuje liczbę
  samolotów w zasięgu (`inRange`), nie liczbę wierszy; tabela, NOW i radar pracują na wierszach.
  Opis w `docs/screens.md` („Screen chrome”, „Screen 1”, „Screen 3”).
- Wybór samolotu dla NOW (`pickNowAircraft()`) działa na przyciętej liście. Samolot, który
  przeleci nad domem, jest z definicji blisko, ale test ma to potwierdzić: kandydat do NOW
  (CPA w `kOverheadRadiusKm`, w powietrzu) nie może wypaść przez limit, dopóki w powietrzu jest
  mniej niż N bliższych.
- Testy (`test_airspace/nearest_keeper_test.cpp`): mniej niż N, dokładnie N, N+1, 300; remisy
  odległości (stabilnie); same naziemne; brak pozycji; kolejność wyniku identyczna z
  `sortRowsForDisplay()` na pełnej liście obciętej do N.

## 3. `/states/all` parsowane po jednym samolocie

Dziś (`src/opensky/adapters/opensky_http.cpp`) cała odpowiedź trafia do jednego `JsonDocument`
z filtrem, który zostawia wszystkie 17 pól każdego samolotu (ArduinoJson nie filtruje po
indeksie tablicy), a potem jest przepisywana do `std::vector<AircraftState>`.

- Nowa czysta funkcja w `opensky/core` (obok `extractAircraftFromStates()`):
  `StatesParse parseStatesStream(Reader &in, const AircraftSink &sink)` — wzorzec ArduinoJson
  „deserialization in chunks”: znajdź klucz `"states"`, potem w pętli `deserializeJson()` dla
  jednego wektora stanu (mały dokument, wielokrotnie używany), mapowanie pól tą samą funkcją co
  dziś (jedno miejsce z indeksami), `sink(AircraftState&&)`, separator `,` albo `]`.
- `Reader` to własny, minimalny interfejs (`read()` / `readBytes()`), który ArduinoJson
  przyjmuje jako źródło: na płytce owija `http.getStream()`, w testach napis podawany w kawałkach
  dowolnej długości.
- **Co jest poprawną odpowiedzią** (dziś: „jest klucz `time`”): klucz `"states"` znaleziony,
  jego wartość to `null` albo tablica zamknięta `]`. Wszystko inne (HTML błędu, ucięta
  odpowiedź, timeout w połowie) to błąd parsowania, a **nie** „0 samolotów” — ekran zostaje przy
  ostatnich dobrych danych, jak dziś. Częściowo sparsowana lista jest odrzucana w całości.
- `sink` na płytce to `NearestKeeper` z p. 2, więc szczyt pamięci pollu to N × `AircraftState`
  (oba napisy mieszczą się w SSO) plus jeden wektor stanu, niezależnie od ruchu w zasięgu.
- HTTP/1.0 zostaje (docs/history.md „Data sources”), `kMaxStatesResponseBytes` zostaje jako
  kontrola `Content-Length`.
- `parseStatesResponse(std::string)` zostaje tylko, jeśli coś poza testami go używa; inaczej
  znika razem z testami (CLAUDE.md „Code conventions”), a testy mapowania przechodzą na nową
  funkcję.
- Testy (`test_opensky/states_stream_test.cpp`): te same przypadki co dziś dla
  `parseStatesResponse()` (null w polach, za krótki wektor, callsign ze spacjami,
  `inferOnGround()`), plus: `"states": null`, pusta tablica, body pocięte co 1 / 7 / 64 bajty,
  ucięcie w środku wektora, ucięcie między wektorami, brak klucza `states`, śmieci po tablicy,
  klucze koperty w odwrotnej kolejności.

## 4. Faza B tylko wtedy, gdy ma co pobrać

`pollOpenSky()` zawsze buduje listę drugi raz (`buildAirspaceSnapshot()` z budżetem) i wyrzuca
wynik, gdy `fetches == 0` — czyli w większości polli. To setki krótkich alokacji przeplatanych
z długo żyjącymi wpisami cache, dokładnie ten wzorzec, który tnie blok.

- `AirspaceSnapshot` fazy A dostaje `pendingLookups` — ile kluczy zostało `…_pending` z powodu
  budżetu 0 (liczone tam, gdzie i tak ustawiane są flagi `*_pending`).
- Faza B rusza tylko, gdy `pendingLookups > 0`. Przy 0 nie ma ani drugiej budowy, ani
  `hexdbSessionEnd()`.
- Zachowanie bez zmian: faza B publikowana tylko, gdy coś pobrała; przerwana przez
  `betweenFetches` nadal publikuje to, co ma.
- Testy (`test_airspace/poll_pipeline_test.cpp`): wszystko w cache → `pendingLookups == 0`;
  jeden nieznany samolot / trasa / lotnisko → > 0; lotniska liczone dopiero po trasach (trasa
  z cache, lotnisko nieznane → > 0).

## 5. Rysowanie tabeli bez kopiowania wierszy

- `splitFeaturedAndRest()` kopiuje całą listę przy każdym przerysowaniu treści i przy każdym
  dotknięciu (`tablePageCount()`), a `getPageSlice()` kopiuje stronę drugi raz (otwarty punkt
  docs/review-notes-2026.md 4.5).
- Zmiana: widoki zamiast kopii. `FeaturedSplit` trzyma wskaźnik na wyróżniony wiersz i zakres
  (`const TrackedFlight *begin, *end` albo indeksy) reszty; `getPageSlice()` zwraca zakres.
  `tablePageCount()` liczy z samego rozmiaru listy, bez dotykania wierszy.
- Zakres jest ważny tak długo, jak `AirspaceSnapshot`, z którego pochodzi; UI trzyma go przez
  `shared_ptr<const NetState>` na czas całego przebiegu — warunek zapisany w komentarzu przy
  typie.
- Testy istniejące (`flight_ordering_test`, `table_view_test`) przepisane na zakresy, te same
  przypadki; test, że `tablePageCount()` dla 0 / 1 / 6 / 7 wierszy daje to samo co dziś.

## 6. `storeHas*()` bez czytania rekordu

- `has()` w `src/enrichment/adapters/lookup_store_nvs.cpp` woła `load()`, czyli czyta cały
  rekord do `String`, kopiuje do `std::string` i wyrzuca. Zmiana: samo `isKey()`.
- Jeden uchwyt `Preferences` na rodzaj rekordu, otwarty na czas pollu (otwarcie w
  `enrichmentBeginPoll()`, zamknięcie tam, gdzie dziś `enrichmentClearMemos()` po fazie B),
  zamiast `begin()` / `end()` na każde pytanie.
- Negatywna odpowiedź `…Known()` zapamiętana na czas pollu (dziś memo trzyma tylko wyniki
  lookupów), żeby `planLookups()` w fazie A i B nie pytało NVS drugi raz o ten sam klucz.
  Czysta część (memo) w `main/net_enrichment` ma test: drugi `aircraftKnown(k)` w tym samym
  pollu nie woła portu NVS.
- Adapter NVS pozostaje bez testu Unity (CLAUDE.md „Testing”); sprawdzenie na płytce: po wymianie
  płytka z pełnym NVS rozpoznaje te same samoloty bez zapytań do hexdb.io (log `[hexdb]` pusty).

## 7. Cache hexdb.io w stałej puli

To pozycja „Not done: pooling the cache entries” z docs/history.md: ok. 80 małych, długo żyjących
bloków (`std::list` + `std::unordered_map` + napisy powyżej SSO) rozrzuconych po stercie. Etapy
1–6 zmniejszają ruch wokół nich; ten usuwa je ze sterty.

- `LookupCache<V>` zastąpiony przez `FixedLookupCache<Record, N>`: tablica `N` rekordów o stałym
  rozmiarze w `.bss` (nie na stercie), LRU przez licznik użycia w rekordzie albo listę indeksów
  `uint8_t`, wyszukiwanie liniowe po kluczu `char[9]` (N ≤ 96, porównanie 8 bajtów — szybsze
  niż hash napisu).
- Rekordy z napisami w `char[]`:

  | Cache | N | Pola (z miejscem na NUL) | Rozmiar ok. |
  |---|---|---|---|
  | samolot (`icao24`) | 96 | klucz 9, linia 33, typ 25, typ ICAO 9, rejestracja 13, flagi | 96 × 92 B ≈ 8,8 KB |
  | trasa (callsign) | 96 | klucz 9, skąd 5, dokąd 5, flagi | 96 × 22 B ≈ 2,1 KB |
  | lotnisko (ICAO) | 64 | klucz 5, IATA 4, kraj 3, nazwa 41, flagi | 64 × 56 B ≈ 3,6 KB |

  Razem ok. 14,5 KB stałej pamięci zamiast do 64 KB na stercie w najgorszym razie.
- Długości pól: ekran ma 320 px, czyli najwyżej 40 znaków czcionką 8 px, a każde pole i tak
  przechodzi przez `fitToWidth()`. Napis dłuższy niż pole jest ucinany na granicy znaku UTF-8
  **tylko w RAM**; NVS dalej trzyma pełny. Test: żadna nazwa z tablic linii / typów / miast
  i żaden rekord testowy nie jest ucinany krócej, niż pokazuje najszerszy ekran.
- Interfejs zostaje (`find` / `contains` / `put` / `size` / `capacity` / `clear`), wyniki
  lookupów dalej jako `AircraftInfo` / `RouteInfo` / `AirportInfo` (konwersja przy odczycie),
  więc `poll_pipeline` i ekrany bez zmian.
- Testy (`test_enrichment/lookup_cache_test.cpp`): obecne przypadki LRU bez zmian (kolejność
  wypierania, `contains()` nie odświeża, nadpisanie klucza), plus klucz na granicy długości,
  klucz za długi (odrzucony, nie ucięty), ucinanie napisu na granicy UTF-8.
- Etap największy i wchodzi tylko, jeśli po etapach 1–6 `minBlock` nadal spada poniżej 50 KB —
  decyzja z pomiaru, zapisana w tej specyfikacji.

## 8. Radar bez mrugania

Dziś każdy poll czyści obszar treści jednym `fillRect` i rysuje radar wprost na panelu — czarny
błysk co kilkanaście sekund. Bufor odliczania ekranu NOW (320 × 123 px, 8 bitów, 39 360 B)
istnieje od startu i poza ekranem NOW leży nieużywany.

- Radar (strefa 190 × 197 px) rysowany przez ten sam sprite w dwóch pasach po ≤ 123 wiersze:
  `drawRadar()` dostaje przesunięcie w pionie, rysuje do sprite'a, pas jest wypychany jednym
  `pushSprite()`. Bez nowej alokacji. Panel boczny (tekst) rysowany jak dziś, ale czyszczony
  wierszami, nie całą strefą.
- `computeRedrawMask()` dostaje dla radaru osobny bit (`kRedrawRadar`) zamiast `kRedrawContent`
  przy nowym pollu na ekranie radaru — pełny `fillRect` treści zostaje tylko przy zmianie ekranu
  (CLAUDE.md „Redraw policy” do uzupełnienia).
- **Kolory:** sprite 8-bitowy to RGB332, więc kolory motywu wyszłyby inne niż rysowane wprost.
  Sprite dostaje paletę (`createPalette()`) z dokładnymi kolorami `THEME_*` używanymi przez radar
  i odliczanie. Kryterium: zrzut ekranu radaru przed i po różni się tylko tam, gdzie zmieniły
  się dane (porównanie `docs/screenshots/`), a ekran NOW wygląda identycznie.
- Właścicielem bufora zostaje jeden moduł (`flight_screen_draw` → wydzielony
  `content_sprite` w `display/adapters/lgfx/`), używany przez NOW i radar; nigdy naraz, bo
  aktywny jest jeden ekran.
- Tabela (ekran LOTY) poza zakresem tego etapu: czyści się już wierszami.
- Testy: `redraw_policy_test` (nowy poll na radarze → `kRedrawRadar`, nie `kRedrawContent`;
  zmiana ekranu → pełne przerysowanie; inne ekrany bez zmian); podział na pasy jako czysta
  funkcja w `radar_geometry` (pasy pokrywają strefę dokładnie raz, żaden nie przekracza
  wysokości sprite'a). Rysowanie: `@manual` + zrzuty.

## Kolejność i kryteria

| Etap | Co | Zależy od | Kryterium wejścia do main |
|---|---|---|---|
| 0 | pomiar wyjściowy, nowe pola `/perf.json` | — | tabela „przed” wypełniona |
| 1 | stałe koszty pętli | 0 | `net busy` w spoczynku niżej; alerty bez opóźnień |
| 2 | limit samolotów | 0 | promień 10° przez 30 min bez restartu |
| 3 | parsowanie strumieniowe | 2 | szczyt sterty w pollu nie rośnie z liczbą samolotów |
| 4 | warunkowa faza B | 0 | poll bez nowych lookupów: jedna budowa listy |
| 5 | tabela bez kopii | — | zrzuty ekranu LOTY identyczne |
| 6 | NVS `isKey()` | — | te same samoloty rozpoznane, brak zapytań do hexdb.io |
| 7 | cache w puli | 1–6 + pomiar | `minBlock` ≥ 50 KB po 24 h |
| 8 | radar przez sprite | — | brak błysku, kolory zgodne |

Każdy etap to osobny commit (albo kilka), po którym `pio test -e native` jest zielone, a
`python tools/check_layers.py --strict` daje 0 naruszeń. Etapy 5, 6 i 8 są niezależne od reszty.

## Dokumentacja

- CLAUDE.md: „Concurrency — two cores” (akapit o stercie — nowe liczby z pomiaru; progi
  `kUpdate*MinFreeBlock` zostają, dopóki pomiar nie pokaże, że można je zmienić), „Redraw policy”
  (radar), „Pitfalls” (limit samolotów).
- `docs/architecture.md`: „Concurrency — two cores” (poll, faza B, pomiar), „Source layout”
  (nowe moduły: `nearest_keeper`, `content_sprite`, parser strumieniowy).
- `docs/data-sources.md`: „OpenSky” (parsowanie, co jest poprawną odpowiedzią), „Caching and the
  per-poll budget” (pula, długości pól, `isKey()`).
- `docs/screens.md`: liczba w nagłówku a liczba wierszy, radar.
- `docs/history.md`: co było przedtem i dlaczego się zmieniło; etapy odrzucone po pomiarze.
- `docs/review-notes-2026.md`: 4.4 i 4.5 oznaczone jako wprowadzone.
- ROADMAP: pozycja przechodzi do „Done” z liczbami z tabeli pomiarów.

## Poza zakresem

- **Keep-alive albo wznawianie sesji TLS dla OpenSky.** Każdy poll to nowy handshake (~0,8 s
  według docs/history.md), ale keep-alive wymaga HTTP/1.1 z własnym dekoderem chunked, a zysk
  zależy od timeoutu serwera i znika przy długich interwałach (konto anonimowe). Najpierw pomiar
  z etapu 0 (`pollMs`: ile z pollu to handshake); osobna specyfikacja, jeśli wyjdzie warto.
- Napisy w `TrackedFlight` (`std::string` na wiersz): po limicie z p. 2 to najwyżej 48 wierszy
  krótko żyjących; wracamy do tego tylko, jeśli pomiar po etapie 7 dalej pokaże cięcie bloku.
- Rozmiar firmware: 67 % slotu, zapas ~550 KB. Wymiana WiFiManagera (43 KB) nie jest warta
  zachodu.
- Częstotliwość SPI (40 MHz), usypianie pętli UI, przerwanie zamiast odpytywania dotyku
  (GPIO 36 łapie zakłócenia przy włączonym WiFi) — bez zmian.
- Pobór mocy (`setCpuFrequencyMhz`, tryby uśpienia WiFi): urządzenie jest zasilane z USB,
  a dominuje podświetlenie.

## Pomiary

Do wypełnienia w etapie 0 i po każdym etapie (wersja, data, scenariusz 1 / 2 / 3).

| Wielkość | Przed | Po 1 | Po 2–4 | Po 5–6 | Po 7 | Po 8 |
|---|---|---|---|---|---|---|
| `minBlock` po 30 min, promień domyślny | | | | | | |
| `minBlock` po 30 min, promień 10° | | | | | | |
| `minBlock` po 24 h | | | | | | |
| `net busy` w spoczynku | | | | | | |
| `pollMs` (żądanie + parsowanie / A / B) | | | | | | |
| `ui gap max` przy pollu na ekranie radaru | | | | | | |
| rozmiar firmware | 1 319 072 B | | | | | |
