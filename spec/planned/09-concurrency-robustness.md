# Spec 09 — Poprawki odporności: zrzut ekranu, kolejka komend, serwer WWW w trakcie lookupów

Z przeglądu kodu 2026-10-25, punkty 1.3 i 1.5. Status: **w trakcie**. Niezależne od siebie;
każdy punkt = osobny commit z testem.

## 1. Wyścig w protokole zrzutu ekranu (`shot::` w `main.cpp`)

**Problem.** [main.cpp](../../src/main.cpp) `shot::waitFor()` po przekroczeniu czasu ustawia
`cancelled = true` i `state = Idle`, a handler HTTP (rdzeń 0) zwalnia bufory. Dwa błędy:

1. UI (rdzeń 1) kończy rozpoczęty pas i na koniec zapisuje `state = BandDone`, nadpisując
   `Idle`. `capturing` zostaje `true`, więc UI nie rysuje nic przez `kCaptureIdleMs` (5 s).
2. Jeśli w tym czasie przyjdzie nowe `/screenshot.bmp`, `begin()` ustawia `cancelled = false`,
   a UI — wciąż w starym pasie — pisze do `shot::out`, który wskazuje na **zwolniony** bufor
   poprzedniego żądania (use-after-free). Mało prawdopodobne, ale możliwe.

**Poprawka.** Licznik generacji zamiast flagi `cancelled`:

- Każde `begin()` i każde `requestBand()` pod blokadą zapisuje `gen = ++genCounter`
  i parametry pasa. Timeout po stronie handlera: pod blokadą `gen = 0` (unieważnia wszystko).
- UI na starcie pasa kopiuje `myGen = gen` i parametry (pod blokadą). Każdy `memcpy` i końcowe
  `state = BandDone` tylko wtedy, gdy pod blokadą `gen == myGen`. Inaczej porzuca pas bez zapisu.
- Logikę wyciągnąć z `main.cpp` do czystej klasy `ScreenshotBridge` (`src/screenshot.h`,
  obok istniejącej matematyki BMP) z wstrzykiwanym zegarem i funkcją budzenia UI; `main.cpp`
  zostaje z odczytem `tft.readRect()`.

**Testy** (`test_admin` po spec 07): sekwencyjne — timeout w trakcie pasa nie zostawia
`BandDone`; pas ze starej generacji nie zapisuje do nowego bufora; normalny przebieg
k pasów. Współbieżny — `std::thread` dla „handlera” i „UI”, 10 000 przebiegów z losowymi
timeoutami, bufory wypełniane wzorcem i sprawdzane po zwolnieniu (w CI pod `native-asan`
ze spec 07 — ASan wychwyci zapis do zwolnionej pamięci).

## 2. Zgubione „NIE” w pytaniu o aktualizację

**Problem.** [main.cpp](../../src/main.cpp) `serviceUpdatePrompt()`: przy `ResetChoice::No`
wynik `core_link::sendCommand(SkipTag)` jest ignorowany. Przy pełnej kolejce (8 komend) wybór
przepada — po restarcie pytanie wraca, mimo że użytkownik odpowiedział „NIE”.

**Poprawka.** Komenda, której nie udało się wysłać, czeka w jednoelementowym buforze
`pendingCommand` po stronie UI i jest ponawiana na początku każdego przejścia `loop()`, aż
kolejka ją przyjmie (log raz: `[update] command queue full, retrying`). To samo dla
`InstallTag`. Czysta funkcja `retryPendingCommand(pending, trySend) -> pending` z testem.

## 3. Strona ustawień nie odpowiada w trakcie lookupów hexdb.io

**Problem.** [net_task.cpp](../../src/net_task.cpp) `pollOpenSky()` faza B robi do 5 zapytań
HTTPS (2 samoloty/trasy + 3 lotniska) jedno po drugim. Przez ten czas `configPortalLoop()`
się nie wykonuje — strona ustawień ładuje się z opóźnieniem do kilku sekund.

**Poprawka.**

- `LookupPorts` dostaje opcjonalny hak `betweenFetches` (pusty w testach), wołany przez
  `buildDataSnapshot()` po każdym **rzeczywistym** zapytaniu sieciowym (nie po trafieniu
  w cache). W `net_task` hak woła `configPortalLoop()`.
- Ochrona przed zagnieżdżeniem: flaga `inBetweenFetches` — hak nic nie robi, jeśli jest już
  w trakcie (handler nie może wywołać kolejnej fazy B).
- Zapisy ustawień w trakcie fazy B są bezpieczne: `seenSaveCount` jest sprawdzany dopiero
  po zakończeniu pollu, więc Config zmienia się między pollami, nie w trakcie.
- Jeśli w haku zaczęła się aktualizacja OTA (`configPortalRestartPending()` albo
  `otaFlashSession().inProgress()`), faza B kończy się bez dalszych zapytań.

**Testy** (`test_airspace`): `buildDataSnapshot` woła hak dokładnie tyle razy, ile było
fetchy; trafienia w cache go nie wołają; hak, który ustawia „przerwij”, kończy fazę B bez
dalszych fetchy i z poprawnym (częściowym) snapshotem.

## 4. Kryteria akceptacji (płytka)

1. 50 × `curl` na `/screenshot.bmp` z losowym przerwaniem (`--max-time 0.3`) w trakcie
   pracy: brak restartu, ekran rysuje dalej najpóźniej 1 s po przerwaniu, `[perf]` bez
   przerw UI > 5 s.
2. Odświeżanie strony ustawień co 2 s przez 5 min: każde ładowanie < 1,5 s, także gdy w logu
   jest `phase B: N hexdb.io lookups`.
3. `pio test -e native` zielone, w CI także `native-asan`.
