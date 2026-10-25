# Spec 04 — Dwa rdzenie: sieć na rdzeniu 0, ekran i dotyk na rdzeniu 1

Pozycja #2 z „Planned” w [ROADMAP.md](../ROADMAP.md) („Fixes the 1–3 s UI freezes during HTTPS
requests (and the 3–4 s daily GitHub check); worth doing before more data sources”). Status:
**zrobione i przetestowane na płytce 2026-10-25** (wyniki i odstępstwa: §10; decyzje
właściciela: §8).

## 1. Cel

Interfejs (ekran, dotyk, odliczanie TERAZ, zegar) **nigdy nie czeka na sieć**. Cała praca
sieciowa — WiFi, OpenSky, hexdb.io, GitHub, strona ustawień, OTA — przechodzi do osobnego
zadania FreeRTOS przypiętego do **rdzenia 0** (tam, gdzie już działa stos WiFi i lwIP), a
dzisiejsze `loop()` na **rdzeniu 1** zostaje wyłącznie rysowaniem i dotykiem.

Kryterium sukcesu w jednym zdaniu: *w trakcie pollingu OpenSky, lookupów hexdb.io, sprawdzania
GitHuba i pobierania strony ustawień dotknięcie zakładki przełącza ekran od razu (< 50 ms),
a odliczanie na ekranie TERAZ tyka równo co sekundę — zmierzona najdłuższa przerwa pętli UI
spada z sekund do pojedynczych dziesiątek milisekund.*

**Wydajność jest wymaganiem, nie dodatkiem.** Projekt jest „share nothing”: każdy stan ma
jednego właściciela, a między rdzeniami płyną tylko niezmienne migawki (zamiana wskaźnika,
zero kopiowania danych), małe komendy i kilka liczb atomowych. Żadnego mutexa trzymanego
w trakcie I/O, żadnego `delay()` jako synchronizacji, żadnego aktywnego czekania.

## 2. Stan obecny (co już jest)

- **Jedna pętla robi wszystko** (`src/main.cpp`, `loop()` = Arduino `loopTask`, rdzeń 1,
  priorytet 1): `wifiManagerLoop()`, `configPortalLoop()` (WebServer), dotyk, polling
  `openSkyClientPoll()`, lookupy `lookupAircraft()` / `lookupRoute()` /
  `lookupAirportCountry()`, CPA, `releaseCheckLoop()`, rysowanie. Każde zapytanie HTTP
  **blokuje całą pętlę**.
- **Ile to dziś zamraża** (szacunki z logów, do zmierzenia w kroku 1, §7):
  - `/states/all`: nowy `WiFiClientSecure` + handshake TLS przy **każdym** pollingu (co 15 s),
    ~1–3 s; plus token OAuth co ~30 min.
  - hexdb.io: do `kDefaultLookupBudget` (2) + `kDefaultAirportLookupBudget` (3) = **5 nowych
    połączeń TLS na poll**, każde z osobnym handshake'iem (`aircraft_lookup.cpp:97`,
    `route_lookup.cpp` — brak `setReuse`), limit `kHexdbHttpTimeoutMs` = 4 s każde. Najgorszy
    przypadek: ~20 s bez reakcji na dotyk.
  - GitHub (`release_check`): handshake z weryfikacją łańcucha (P-384), 3–4 s raz na dobę;
    pobranie firmware'u ~20 s (wtedy i tak widać ekran OTA).
  - Strona ustawień: `handleClient()` w tej samej pętli — wygenerowanie strony, `/backup.json`,
    `/screenshot.bmp` (~230 KB strumieniowo) wstrzymują ekran na czas żądania.
  - Start: `wifiManagerBegin()` (`autoConnect`) czeka na połączenie w `setup()` — ekran jest
    czarny, zanim WiFi się nie połączy.
- **Watchdog:** `CONFIG_ESP_TASK_WDT_TIMEOUT_S = 5`, `CHECK_IDLE_TASK_CPU0 = 1`,
  **`CONFIG_ESP_TASK_WDT_PANIC = 1`** (sdkconfig arduino-esp32 2.0.17). Zadanie na rdzeniu 0,
  które nie odda CPU przez 5 s, **zrestartuje płytkę**. Dziś na rdzeniu 0 nie ma nic naszego,
  więc problem nie istniał — po zmianie istnieje (§5.2, §5.9).
- **Rdzenie dziś:** `CONFIG_ARDUINO_RUNNING_CORE = 1` (loop), `CONFIG_ARDUINO_EVENT_RUNNING_CORE
  = 1` (zdarzenia WiFi Arduino), `CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0 = 1`,
  `CONFIG_LWIP_TCPIP_TASK_AFFINITY = 0x0` (lwIP na rdzeniu 0), `CONFIG_FREERTOS_HZ = 1000`.
  Sieć „z natury” mieszka już na rdzeniu 0 — przenosimy do niej nasz kod sieciowy.
- **Stos:** `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` dodane tylko dlatego, że TLS GitHuba
  z weryfikacją przepełniał 8 KB pętli (komentarz w `main.cpp`).
- **Rysowanie spoza „swojego” miejsca:** obserwator OTA (`configPortalSetOtaObserver`,
  `releaseSetOtaObserver`) rysuje `service_screen` **z wnętrza handlera HTTP / pobierania**;
  `/screenshot.bmp` woła `prepare()` (rysuje ekran) i `readRow()` (`tft.readRect`) z handlera.
  Po rozdzieleniu rdzeni to byłoby rysowanie z rdzenia 0 — do przerobienia (§5.6, §5.7).
- **Dotyk:** `touchInputRead()` → `getTouchRaw()` + `getTouch()`. LovyanGFX
  `Touch_XPT2046::getTouchRaw()` najpierw czyta poziom `pin_int` (GPIO36) i bez dotyku wraca
  **bez transakcji SPI** (`Touch_XPT2046.inl:60`) — odpytywanie dotyku jest tanie.
- **Pętla kręci się bez przerwy** (brak `delay`/czekania) — rdzeń 1 jest dziś zajęty w 100 %,
  nawet gdy nic się nie dzieje.
- **Konfiguracja:** `cachedCfg` w `main.cpp`, przeładowywana po zapisie
  (`configPortalSaveCount()`) **i dodatkowo po każdym pollingu** (odczyt NVS co 15 s,
  „backstop”). Handlery strony same robią `loadConfig()` → zmiana → `saveConfig()`.
- **Logika pollingu nietestowana:** blok ~100 linii w `loop()` (budowa wierszy, plan
  lookupów, scalanie, fazy, lotniska, CPA, `pickNowAircraft`, przycięcie strony tabeli) żyje
  w `main.cpp`, który nie kompiluje się pod `native`.
- **Pamięć:** ~213 KB wolnej sterty po pollingu (spec 01 §10); jedna sesja TLS ~40–50 KB.
  Brak PSRAM.
- **Spec 03 (jasność, tryb nocny)** jest zaakceptowana, niezaimplementowana — ten projekt
  musi do niej pasować (§5.11).

## 3. Zakres

### W zakresie (v1)

1. **Zadanie sieciowe `net`** (rdzeń 0) — właściciel WiFi, NTP, mDNS, WebServera, OpenSky,
   hexdb.io (z cache RAM/NVS), `release_check`, OTA, oznaczania obrazu jako ważnego.
2. **Pętla UI** (`loop()`, rdzeń 1) — właściciel `tft`, dotyku, nawigacji, stanu rysowania,
   pytania o aktualizację, ekranów serwisowych; **śpi**, gdy nie ma nic do zrobienia
   (budzenie: termin, dotyk, nowa migawka).
3. Nowy czysty moduł **`core_link`**: typy migawek i komend, skrzynka „najnowsza wygrywa”
   (`LatestBox`), różnica migawek → `RedrawEvents`, czas uśpienia UI, wykrywanie zawieszenia
   sieci. Testy `native`.
4. Nowy czysty moduł **`poll_pipeline`**: cała obróbka wyniku pollingu wyjęta z `main.cpp`
   (z wstrzykiwanymi lookupami), dwufazowa publikacja (§5.4). Testy `native` — logika, która
   dziś nie ma żadnego testu.
5. Nowy czysty moduł **`perf_stats`**: najdłuższa przerwa pętli UI, histogram, zajętość
   zadania sieciowego, zapas stosów; log co minutę + `GET /perf.json`.
6. **Przeróbka OTA i zrzutów ekranu** na komunikację między rdzeniami (bez rysowania z rdzenia
   0); zrzut z podwójnym buforem pasów.
7. **Keep-alive dla hexdb.io**: jedna sesja TLS na partię lookupów zamiast jednej na lookup
   (§5.8) — po pomiarze w kroku 1.
8. `shouldMarkAppValid()` dostaje warunek „pętla UI żyje” (rollback również przy zawieszonym
   UI).
9. Dokumentacja: CLAUDE.md, README, strona projektu, ROADMAP (§5.12).

### Poza zakresem (na później)

- Osobne zadanie dla WebServera (żeby strona ustawień odpowiadała także w trakcie pollingu).
  Kosztowałoby kolejne ~8 KB stosu i synchronizację cache'y; strona i tak odpowiada po
  zakończeniu bieżącego żądania sieciowego. Wrócić, jeśli pomiar pokaże, że to przeszkadza.
- Przerobienie odświeżania treści na sprite / podwójne buforowanie (mignięcie przy `fillRect`
  treści co poll) — osobny temat „Redraw policy”, niezależny od rdzeni.
- Przerwanie z `pin_int` (GPIO36) do budzenia UI. Odpytywanie co 20 ms jest prawie darmowe
  (patrz §2), a GPIO36 ma erratę ESP32 (fałszywe zbocza przy włączonym ADC1 — a spec 03 czyta
  ADC1 na GPIO34). Jeśli kiedyś — tylko jako dodatkowy budzik, z weryfikacją poziomu.
- Wznawianie sesji TLS (session tickets) między pollingami OpenSky — `WiFiClientSecure`
  2.0.17 tego wygodnie nie wspiera.
- Usypianie CPU (light sleep, DFS) — WiFi i podświetlenie i tak dominują pobór.
- Warstwa źródeł danych (ROADMAP #4/#5) — ten projekt ją ułatwia (§5.4: pipeline przyjmuje
  `std::vector<Aircraft>`, skąd by nie przyszły), ale jej nie buduje.

## 4. Co zobaczy użytkownik

- **Start:** nagłówek, zakładki i ekran z „--” pojawiają się natychmiast po włączeniu, zanim
  WiFi się połączy (dziś: czarny ekran przez czas `autoConnect`). Kropka zdrowia — jak
  dotąd po pierwszym pollingu.
- **Dotyk zawsze działa:** zakładki i stronicowanie tabeli reagują w trakcie pollingu,
  lookupów, sprawdzania GitHuba, generowania strony ustawień.
- **TERAZ tyka równo** — bez „zacięć” i skoków o 2–3 s co 15 s.
- **Dane szybciej:** pozycje samolotów pojawiają się zaraz po odpowiedzi `/states/all`;
  linia / typ / trasa nowych samolotów dochodzą chwilę później w tym samym cyklu (dziś
  wszystko czeka na ostatni lookup). Znane samoloty (z cache) od razu mają komplet.
- **Ekran OTA** wygląda tak samo; pasek postępu płynniejszy (rysuje go UI, nie pętla
  pobierająca).
- **Zrzut ekranu** (`/screenshot.bmp`) działa jak dotąd, szybciej.
- Bez nowych ustawień w panelu. Jedna nowa strona diagnostyczna: `/perf.json`.

## 5. Projekt techniczny

### 5.1 Podział własności (jedna zasada, egzekwowana w kodzie)

| Zasób / stan | Właściciel | Inni dostają |
|---|---|---|
| `tft` (SPI HSPI), dotyk (VSPI), sprite odliczania | **UI** | nic — rdzeń 0 nigdy nie woła LovyanGFX |
| `ScreenNav`, `currentPage`, `RedrawEvents`, stan pytania o aktualizację | **UI** | — |
| WiFiManager, WebServer, mDNS, NTP (`configTzTime`) | **net** | `NetState.wifi` |
| `opensky_client` (token, harmonogram, quota) | **net** | `NetState.health/quota` |
| cache `aircraft_lookup` / `route_lookup`, `lookup_store` | **net** | gotowe wiersze w migawce |
| `release_check`, `otaFlashSession()` | **net** | `NetState.update`, `NetState.ota` |
| `Config` w pamięci | **net** (ładuje, po zapisie przeładowuje) | `shared_ptr<const Config>` w migawce |
| zapis NVS | net (ustawienia, cache), UI tylko `saveLastScreen()` (osobny klucz) | — |

Zasady (do CLAUDE.md, §5.12):

- **Kod sieciowy nie dotyka `tft`. Kod UI nie robi I/O sieciowego.** `#include
  "LGFX_CYD.hpp"` tylko w plikach UI; `net_task.cpp` go nie włącza (sprawdzane w przeglądzie,
  a `test_core_link` nie linkuje LovyanGFX z definicji).
- **Między rdzeniami tylko trzy kanały** (§5.3): migawka `NetState` (net → UI), kolejka
  `NetCommand` (UI → net), garść `std::atomic<uint32_t>` (bicie serca, zgoda na polling).
  Plus wąski kanał zrzutu ekranu (§5.7). Żadnych innych zmiennych współdzielonych.
- Blokada (spinlock) wokół kanału trwa **nanosekundy**: tylko zamiana `shared_ptr`. Nigdy
  alokacja, zwolnienie pamięci, log ani I/O pod blokadą.

### 5.2 Zadania FreeRTOS

| Zadanie | Rdzeń | Priorytet | Stos | Pętla |
|---|---|---|---|---|
| `loopTask` (Arduino `loop()`) = **UI** | 1 | 1 (bez zmian) | **8 KB** (domyślne; usuwamy `SET_LOOP_TASK_STACK_SIZE(16 KB)`, TLS już tu nie ma) | czeka na `ulTaskNotifyTake(timeout)` (§5.5) |
| **`net`** (nowe, `xTaskCreatePinnedToCore`) | 0 | 1 | **16 KB** (TLS GitHuba z weryfikacją P-384 — to samo, co dziś wymusiło 16 KB w pętli) | iteracja → `vTaskDelay(kNetIdleSleepMs = 5)` |
| system: WiFi (23), lwIP (18), esp_timer (22), event loop | 0 | jak są | — | — |

- Bilans pamięci: +16 KB (net) − 8 KB (pętla wraca do 8 KB) = **+8 KB sterty**. Zapas stosów
  logowany (`uxTaskGetStackHighWaterMark`) w `perf_stats`; jeśli UI ma < 1,5 KB zapasu albo
  net < 2 KB — powiększyć i zapisać w §10.
- **Watchdog (twarde wymaganie):** zadanie `net` **nigdy nie trzyma CPU dłużej niż ~1 s bez
  oddania**. Pętle czekające na dane (pobieranie firmware'u, odczyt strumienia) już mają
  `delay(2)` — zostaje. Parsowanie JSON (do 64 KB `/states/all`) to dziesiątki ms — OK.
  Handshake TLS przeplata obliczenia z czekaniem na sieć — w kroku 1 zmierzyć najdłuższy
  ciągły odcinek CPU (log `[perf] net max busy`); jeśli > 2 s, rozważyć `esp_task_wdt` dla
  `net` zamiast polegania na IDLE0. Na `vTaskDelay(5)` każdej iteracji IDLE0 dostaje CPU,
  więc TWDT jest spokojny.
- Priorytet `net` = 1, nie wyżej: nic naszego nie powinno wywłaszczać lwIP/WiFi.
- Start w `setup()` (§5.10): najpierw UI (pierwsza klatka), potem `xTaskCreatePinnedToCore(
  netTask, "net", 16384, nullptr, 1, &netHandle, 0)`. `wifiManagerBegin()`,
  `timeSyncBegin()`, `configPortalBegin()`, `releaseCheckBegin()` wołane **na początku
  `netTask`**, nie w `setup()`.

### 5.3 Nowy moduł czysty: `core_link` (`src/core_link.h/.cpp`, `test/test_core_link/`)

Wszystko, co przechodzi między rdzeniami, w jednym miejscu.

```cpp
// --- Migawka net -> UI: niezmienna, publikowana w całości, UI trzyma wskaźnik.
struct DataSnapshot {                        // wynik jednej fazy pollingu (§5.4)
  std::vector<AircraftRow> rows;             // już posortowane, wzbogacone, z fazą
  int nowRowIndex = -1;                      // pickNowAircraft()
  CpaPrediction cpa;                         // dla nowRowIndex
  uint32_t polledAtMs = 0;                   // baza dla extrapolateCpa()
  uint32_t seq = 0;                          // rośnie z każdą publikacją danych
};

struct OtaProgress {                         // zamiast rysowania w obserwatorze OTA
  enum class Phase : uint8_t { Idle, Running, Done, Failed } phase = Phase::Idle;
  bool fromGithub = false;
  uint32_t written = 0, total = 0;
  std::string newVersion;
};

struct UpdateSummary {                       // to, czego potrzebuje PromptGate
  UpdateStatus status{};
  std::string installed, latestTag;
  bool installPending = false;
};

struct NetState {
  std::shared_ptr<const DataSnapshot> data;  // nullptr do pierwszego udanego pollingu
  std::shared_ptr<const Config> cfg;         // zawsze ustawione
  uint32_t cfgSeq = 0;                       // rośnie po każdym przeładowaniu konfiguracji
  OpenSkyHealth health = OpenSkyHealth::Ok;
  WifiStatus wifi{};
  bool timeSynced = false;
  bool restartPending = false;               // configPortalRestartPending()
  UpdateSummary update;
  OtaProgress ota;
};
```

- `data` jest współdzielone między kolejnymi `NetState`: zmiana zdrowia, stanu aktualizacji
  czy postępu OTA publikuje nowy `NetState`, ale **nie kopiuje wierszy** (kopia
  `shared_ptr`). Wiersze kopiuje się zero razy — powstają raz w `poll_pipeline`.
- Nieudany poll publikuje `NetState` z **tym samym** `data` (ostatnia dobra lista zostaje
  na ekranie, jak dziś — review notes 1.2) i nowym `health`.

```cpp
// --- Skrzynka "najnowsza wygrywa" (1 pisarz, 1 czytelnik). Lock = polityka:
// ESP32: portMUX spinlock (portENTER_CRITICAL), native: NullLock / std::mutex.
template <typename T, typename Lock>
class LatestBox {
 public:
  void publish(std::shared_ptr<const T> next);           // zamiana pod blokadą, stary obiekt
                                                         // niszczony PO zwolnieniu blokady
  bool takeIfNewer(uint32_t &seenSeq, std::shared_ptr<const T> &out);  // false = nic nowego
  uint32_t seq() const;
};
```

- Zero kolejkowania: jeśli net opublikuje dwa razy, zanim UI spojrzy, UI dostaje tylko
  najnowszy (to dokładnie to, czego chcemy — pośrednich klatek nikt by nie zobaczył).
- `takeIfNewer` bez nowej wersji: jedno porównanie `seq` pod blokadą, bez ruszania
  licznika referencji.
- Test: „destruktor starej wartości nie wykonał się pod blokadą” — `SpyLock` zapamiętuje,
  czy jest zajęty, a typ testowy sprawdza to w destruktorze.

```cpp
// --- Komendy UI -> net (FreeRTOS xQueue, długość 8, element POD, kopiowany).
enum class NetCommandType : uint8_t { InstallTag, SkipTag, RefreshNow };
struct NetCommand {
  NetCommandType type;
  char tag[32];          // InstallTag / SkipTag; ucięty bezpiecznie (copyTag())
};
NetCommand makeInstallCommand(const std::string &tag);   // czyste, testowane (ucinanie, \0)
```

- `RefreshNow` — na przyszłość (spec 03: dotyk wybudza ekran → świeże dane), zawsze
  z poszanowaniem `shouldPollNow()` (nigdy szybciej niż interwał — CLAUDE.md „Rate limits”).
- Pełna kolejka → komenda odrzucona, log; UI nie czeka (`xQueueSend(..., 0)`).

```cpp
// --- Liczby atomowe (32-bit, lock-free na Xtensa).
//   uiBeatMs        UI -> net: millis() ostatniej iteracji UI (dla shouldMarkAppValid)
//   netBeatMs       net -> UI: millis() ostatniej iteracji net (wykrywanie zawieszenia)
//   pollingAllowed  UI -> net: spec 03 (noc ze wstrzymaniem); domyślnie 1

// --- Czyste decyzje.
RedrawEvents diffNetState(const NetState *prev, const NetState &next);
//   data->seq zmienione -> newPoll; health -> healthChanged (+ newPoll przy pustej liście,
//   jak dziś: emptyStateText()); cfg: language -> languageChanged, home_configured ->
//   firstDraw; ota.phase Idle->Running / ->Failed -> firstDraw-podobne zdarzenia OTA (§5.6).

uint32_t uiSleepMs(const UiDeadlines &d);   // §5.5
bool netLooksStalled(uint32_t nowMs, uint32_t netBeatMs, uint32_t limitMs = 60000);
```

- `netLooksStalled` → UI pokazuje zdrowie `NetworkError` (czerwona kropka, „BRAK DOSTĘPU DO
  OPENSKY”) zamiast udawać, że stare dane są świeże. 60 s, bo net może legalnie stać ~25 s
  (token 8 s + `/states/all` 8 s + lookupy z limitami). Bez restartu — od tego jest watchdog.

### 5.4 Nowy moduł czysty: `poll_pipeline` (`src/poll_pipeline.h/.cpp`, `test/test_poll_pipeline/`)

Blok z `main.cpp:686–792` przeniesiony bez zmiany zachowania, z lookupami wstrzykiwanymi:

```cpp
struct LookupPorts {                      // produkcja: lookup*() + store*(); testy: stuby
  std::function<bool(const std::string &)> aircraftKnown, routeKnown, airportKnown;
  std::function<AircraftInfo(const std::string &)> aircraft;   // RAM/NVS/hexdb
  std::function<RouteInfo(const std::string &)> route;
  std::function<AirportInfo(const std::string &)> airport;
  // tylko z cache (RAM/NVS), bez sieci — dla fazy A:
  std::function<bool(const std::string &, AircraftInfo &)> cachedAircraft;
  std::function<bool(const std::string &, RouteInfo &)> cachedRoute;
  std::function<bool(const std::string &, AirportInfo &)> cachedAirport;
};

struct PipelineParams { float homeLat, homeLon, nearAirportKm, climbRateMps;
                        int lookupBudget, airportBudget; };

// Faza A: bez sieci. Wiersze, odległości, sortowanie, wszystko co jest w cache,
// reszta *_pending, fazy lotu, CPA + pickNowAircraft. Milisekundy.
DataSnapshot buildPhaseA(const std::vector<Aircraft> &ac, const PipelineParams &p,
                         const LookupPorts &ports, uint32_t nowMs);

// Plan nowych lookupów dla fazy B (planLookups, najbliższe najpierw — jak dziś).
LookupPlan planPhaseB(const DataSnapshot &a, const PipelineParams &p, const LookupPorts &ports);

// Faza B: wykonuje plan (sieć), składa nową migawkę. `changed` = czy coś przybyło
// (jeśli nie — nie publikujemy drugi raz, nie ma czego przerysować).
DataSnapshot buildPhaseB(const DataSnapshot &a, const LookupPlan &plan,
                         const PipelineParams &p, const LookupPorts &ports, bool &changed);
```

- **Dwufazowa publikacja:** net publikuje A zaraz po sparsowaniu `/states/all` (pozycje
  natychmiast), potem robi lookupy i publikuje B tylko gdy `changed`. Przy pełnym cache (stan
  ustalony po kilku minutach) faza B jest pusta — **jedno przerysowanie na poll, jak dziś**.
  Przy nowych samolotach — dwa przerysowania treści w odstępie ~1–3 s (decyzja §8.1).
- Lotniska (IATA) potrzebują trasy, a trasa przychodzi w fazie B — dlatego w B najpierw
  trasy, potem lotniska tras znalezionych (dziś identycznie, tylko w jednym kroku).
- `currentPage` przycina UI po odebraniu migawki (strona to stan UI) — `clampPage(page,
  rowsCount)` czysta, w `table_view`.
- Testy: kolejność (w powietrzu najbliższe → ziemia → bez pozycji), budżet wydany na
  najbliższe, `pending` vs `--`, cache z NVS nie zużywa budżetu, faza B bez zmian →
  `changed=false`, CPA/nowRowIndex po sortowaniu wskazuje właściwy wiersz, pusta lista,
  `home_configured=false` (pipeline nie wołany). To jest dziś zero testów.

### 5.5 Pętla UI (`main.cpp`, rdzeń 1)

```text
loop():
  perfUiTick()                                     // przerwa od poprzedniej iteracji
  uiBeatMs = millis()
  if box.takeIfNewer(seen, next): pendingRedraw |= diffNetState(cur, *next); cur = next
  if netLooksStalled(...): pokaż NetworkError (raz, healthChanged)
  if cur->ota.phase != Idle: drawOta*(...) tylko przy zmianie; return-na-sen   // §5.6
  if screenshotRequest: serviceScreenshot(); return-na-sen                    // §5.7
  pytanie o aktualizację (maybeShow / service) — dane z cur->update; TAK/NIE = NetCommand
  dotyk (edge-detected, jak dziś) -> handleTap()
  tick odliczania / minuta / auto-cycle (jak dziś, ale bazy czasu z cur->data->polledAtMs)
  applyRedraw(computeRedrawMask(pendingRedraw, nav.current()))   // bez zmian w polityce
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(uiSleepMs(deadlines)))
```

- **UI śpi, zamiast kręcić się w kółko.** `uiSleepMs()` = minimum z:
  - **20 ms**, gdy palec jest na ekranie lub pytanie TAK/NIE jest aktywne (dotyk i edge
    detection); bez dotyku też **20 ms** (odpytanie `pin_int` = jeden odczyt GPIO, §2) —
    latencja dotyku ≤ 20 ms, CPU rdzenia 1 praktycznie wolny;
  - do następnej pełnej sekundy odliczania (tylko na ekranie TERAZ);
  - do zmiany minuty w nagłówku;
  - do 250 ms timera pytania o aktualizację;
  - do kroku rampy jasności (spec 03, 100 ms) — gdy będzie;
  - górna granica 1000 ms.
  Net budzi UI natychmiast po publikacji (`xTaskNotifyGive(uiHandle)`), więc nowe dane
  rysują się bez czekania na timeout.
- Czysta funkcja `uiSleepMs(UiDeadlines)` w `core_link`, testy: każdy termin z osobna,
  minimum, termin już minął → 0, przepełnienie `millis()`.
- `currentCpa()` = `extrapolateCpa(cur->data->cpa, millis() - cur->data->polledAtMs)` —
  `polledAtMs` z migawki (net ustawia go przy fazie A, czyli momencie danych, nie lookupów).
- Usuwamy z `loop()`: `wifiManagerLoop()`, `configPortalLoop()`, `openSkyClientPoll()`
  i cały blok pollingu, `releaseCheckLoop()`, `markAppValidWhenStable()`, `reloadConfig()` po
  pollingu, `updateGate()`.
- `saveUpdateSkippedTag()` (NIE w pytaniu) → `NetCommand::SkipTag`; net zapisuje i publikuje
  nowy `cfg`. `releaseInstallRequest()` (TAK) → `NetCommand::InstallTag`.
- `saveLastScreen()` zostaje w UI (osobny klucz NVS, odszumiony jak dziś). NVS w ESP-IDF jest
  bezpieczne wątkowo.

### 5.6 Zadanie `net` (`src/net_task.h/.cpp`, `#ifdef ARDUINO`)

```text
netTask():
  cfg = loadConfig(); wifiManagerBegin(); timeSyncBegin(); configPortalBegin();
  releaseCheckBegin(cfg); publish(...)
  for (;;):
    perfNetBegin()
    netBeatMs = millis()
    wifiManagerLoop(); configPortalLoop()
    if configPortalSaveCount() changed: cfg = loadConfig(); releaseCheckBegin(cfg); publish
    obsłuż NetCommand (bez czekania)
    if configPortalRestartPending(): publish restartPending; continue (jak dziś)
    markAppValidWhenStable(uiAlive = millis() - uiBeatMs < 2000)
    if wifi && pollingAllowed:
      poll = openSkyClientPoll(millis(), cfg)
      if poll ran: health; if Ok && home: A = buildPhaseA; publish(A); notify UI
                                          plan = planPhaseB; if !plan.empty(): B ...; publish
                   else: publish(same data, new health)
    releaseCheckLoop(gate)                 // gate liczony tu (dawne updateGate() z main.cpp)
    if zmienił się stan aktualizacji: publish
    perfNetEnd()
    vTaskDelay(pdMS_TO_TICKS(kNetIdleSleepMs))    // 5 ms: WebServer responsywny, IDLE0 żyje
```

- **Publikuje tylko przy zmianie** (porównanie pól z poprzednim `NetState`); w stanie
  spoczynku żadnych alokacji. Każda publikacja = jedna alokacja `NetState` (~200 B) + `notify`.
- Przeładowanie konfiguracji po każdym pollingu (dzisiejszy „backstop”) **usunięte** —
  `configPortalSaveCount()` wystarcza, a to był odczyt NVS co 15 s. Zapis z UI (`last_screen`)
  nie wpływa na nic, co net czyta.
- `configPortalSaveCount()` staje się `std::atomic<uint32_t>` (nadal czytany tylko w net, ale
  tanio i bezpiecznie, gdyby UI kiedyś potrzebował).
- `CheckGate.otaBusy`, `msUntilNextPoll`, `maxFreeBlock` — wszystko z tego samego zadania,
  bez zmian w `release_check`. GitHub dalej omija okna pollingu (`canStartNetworkTask()`),
  choć już nie dla UI, tylko żeby nie spóźniać pollingu OpenSky.
- **OTA:** obserwatory (`configPortalSetOtaObserver`, `releaseSetOtaObserver`) nie rysują,
  tylko aktualizują `ota` i publikują (`onProgress` co 16 KB — ~85 publikacji na obraz, czyli
  nic). UI rysuje `drawOtaScreen` przy przejściu Idle → Running, `drawOtaProgress` przy każdej
  nowej wartości, `drawOtaResult` przy Done/Failed; „aktualizacja nieudana” 5 s i pełne
  przerysowanie — logika `otaFailedAtMs` przechodzi do UI bez zmian.
- ⚠️ **Zapis flasha zatrzymuje oba rdzenie** (cache flash wyłączony na czas zapisu / kasowania
  sektora; kod z flasha nie wykonuje się na żadnym rdzeniu). Dotyczy OTA (wtedy i tak tylko
  ekran postępu), `lookup_store` (zapisy NVS nowych lookupów w fazie B) i `saveLastScreen`.
  To przerwy rzędu ms–dziesiątek ms, nie sekund — `perf_stats` je pokaże; w razie potrzeby
  zapisy `lookup_store` z fazy B można zebrać i zrobić jednym otwarciem przestrzeni NVS
  (zapisać w §10, nie zgadywać).

### 5.7 Zrzut ekranu przez rdzenie (`config_portal`, `main.cpp`)

`ScreenshotSource` zmienia kontrakt: zamiast `prepare()` / `readRow()` wołanych z handlera
(rdzeń 0 → `tft`) — prośba do UI i odpowiedź pasami:

```cpp
struct ScreenshotSource {
  int width, height, screenCount;
  bool (*begin)(int screenIndex, uint32_t timeoutMs);    // UI: przełącz + narysuj, zablokuj rysowanie
  bool (*readBand)(int y0, int rows, uint8_t *bgr, uint32_t timeoutMs);  // UI: readRect pasa
  void (*end)();                                          // UI: odblokuj, zaległe zdarzenia rysuj
};
```

- Implementacja: jedna struktura żądania + `xTaskNotify` w obie strony (UI budzony, handler
  czeka na `ulTaskNotifyTake` z limitem). Bez kolejek, bez kopiowania poza buforem pasa.
- **Podwójny bufor pasów**: 2 × 16 wierszy × 320 × 3 B = **30 KB**, alokowane tylko na czas
  zrzutu (sprawdzić `ESP.getMaxAllocHeap()` przed; za mało → 8 wierszy, potem 503). Handler
  wysyła pas *k* do klienta, w tym czasie UI czyta pas *k+1* — `readRect` (16 MHz) i TCP
  pracują równolegle.
- BMP jest zapisywany od dołu (`screenshot` module): pasy czytane od dołu ekranu, wiersze
  w pasie emitowane w odwrotnej kolejności — czysta funkcja `bandOrder(height, bandRows)`
  w `screenshot` z testem.
- W trybie zrzutu UI nie rysuje (spójny obraz), zdarzenia się kumulują i są rysowane po
  `end()` albo po 5 s bez kolejnego pasa (klient zerwał połączenie).
- `?screen=3` (demo pytania o aktualizację) — jak dziś, w `begin()` po stronie UI.

### 5.8 Keep-alive dla hexdb.io (`aircraft_lookup`, `route_lookup`)

- Dziś: nowy `WiFiClientSecure` + handshake na każde z ≤ 5 zapytań w pollingu. Po zmianie:
  `HexdbSession` (adapter, `#ifdef ARDUINO`) — jeden `WiFiClientSecure` + `HTTPClient` z
  `setReuse(true)` na **czas fazy B**, zamykany po niej (serwery i tak zamykają bezczynne
  połączenia; trzymanie sesji 40–50 KB sterty między pollingami się nie opłaca).
- Gdy serwer zamknie połączenie w połowie partii (`Connection: close`, błąd zapisu) — jedna
  ponowna próba na nowym połączeniu, potem wynik jak dziś (`response_ok=false`, bez cache).
- Czyste API lookupów (`lookup*WithFetcher`) się nie zmienia — zmienia się tylko adapter
  fetchera. Testy bez zmian + test, że kolejność wywołań fetchera w partii jest zachowana.
- Zysk do zmierzenia w kroku 1 (czas handshake'u vs. całego zapytania); jeśli < 30 % czasu
  fazy B — zostawić na później i zapisać w §10. Po rozdzieleniu rdzeni to już nie zamraża UI,
  tylko skraca czas do pełnych danych i oszczędza CPU/energię.

### 5.9 Nowy moduł czysty: `perf_stats` (`src/perf_stats.h/.cpp`, `test/test_perf_stats/`)

```cpp
struct GapStats {                 // przerwy między iteracjami pętli UI
  uint32_t count = 0, maxMs = 0;
  uint32_t buckets[6] = {};       // <20, <50, <100, <250, <1000, >=1000 ms
  void add(uint32_t gapMs);
  uint32_t percentileMs(int p) const;   // z kubełków (górna granica kubełka)
};
struct BusyStats { uint32_t busyMs = 0, windowMs = 0, maxBusyMs = 0; };   // zajętość net
std::string perfLogLine(const GapStats &ui, const BusyStats &net, uint32_t uiStackFree,
                        uint32_t netStackFree, uint32_t freeHeap, uint32_t maxBlock);
std::string perfJson(...);   // GET /perf.json
```

- Log co 60 s: `[perf] ui gap max=14ms p99<20ms (>=250ms: 0) | net busy 3.1% max=1840ms |
  stack ui=3120 net=5480 | heap 201344 (max block 110580)`. Okno zerowane po logu.
- **Krok 1 tego projektu to pomiar na obecnym kodzie** (ta sama `GapStats` w dzisiejszym
  `loop()`) — liczby „przed” trafiają do §10, żeby „po” było z czym porównać.
- `GET /perf.json` (za hasłem panelu jak wszystko): ostatnie pełne okno + od startu. Liczniki
  aktualizowane z obu rdzeni → osobne struktury per rdzeń, czytane w net (UI publikuje swoje
  przez atomiki: max gap, licznik ≥ 250 ms) — nie dzielimy struktury między rdzeniami.
- `maxBusyMs` w net = najdłuższa iteracja pętli net — wskaźnik ryzyka TWDT (§5.2).

### 5.10 `setup()` po zmianie

1. `Serial`, `tft.init()`, rotacja, `fillScreen` — jak dziś.
2. `cfg = loadConfig()`, `i18nSetLanguage()`, stan rollbacku (`appPendingVerify`) — jak dziś.
3. `offerWebPasswordReset()` — jak dziś, **przed** startem net (pętle z `delay()` są tu OK:
   jeszcze nic innego nie działa, a zmiana hasła musi być w NVS, zanim net wczyta konfigurację).
4. `nav.set(cfg.last_screen)`, pierwsza klatka: nagłówek („--”), pusta treść, zakładki —
   **zanim WiFi się połączy**.
5. `coreLinkBegin()` (spinlock, kolejka komend, uchwyt UI do `notify`), start `net`.
6. `ScreenshotSource` / obserwatory OTA rejestrowane jak dziś, ale przez net (rejestracja
   przed startem zadania, żeby handler nigdy nie zobaczył pustych wskaźników).

### 5.11 Zgodność ze spec 03 (jasność, tryb nocny)

- `backlight` (ADC GPIO34, `setBrightness`) — **UI** (rdzeń 1): to sprzęt ekranu.
- `shouldPollOpenSky(decision, night_pause_polling)` liczone w UI → `pollingAllowed` (atomik)
  → net nie woła `openSkyClientPoll()` (zamiast wołania w `loop()` z §5.4 spec 03).
- Dotyk wybudzający w nocy → `NetCommand::RefreshNow` (z poszanowaniem interwału).
- `GET /light.json` czyta stan `backlight` z rdzenia 1 → przez atomiki / mała migawka UI→net
  (ta sama `LatestBox`), nie bezpośrednio.
- Kolejność wdrażania obojętna; ten spec ułatwia spec 03 (rampa jasności dostaje równy
  takt 100 ms zamiast „kiedy pętla zdąży”). Jeśli spec 03 wejdzie pierwsza — krok 6 (§7)
  przenosi jej wywołania zgodnie z powyższym.

### 5.12 Dokumentacja (README, strona projektu, CLAUDE.md, ROADMAP)

- **CLAUDE.md:**
  - nowa sekcja **„Concurrency — two cores”**: tabela własności (§5.1), trzy kanały,
    zasady (nie `tft` z net, nie HTTP z UI, nic pod blokadą poza zamianą wskaźnika), watchdog
    rdzenia 0 (`TWDT_PANIC=1`, 5 s), zapis flasha zatrzymuje oba rdzenie, `/perf.json`;
  - „Code conventions”: `core_link`, `poll_pipeline`, `perf_stats`, `net_task`; poprawki
    opisów `config_portal` (zrzut ekranu pasami przez UI), `opensky_client` / `aircraft_lookup`
    / `route_lookup` (działają na rdzeniu 0, hexdb.io z keep-alive);
  - „Firmware updates (OTA)”: obserwator nie rysuje, publikuje `OtaProgress`;
    `shouldMarkAppValid` z warunkiem „UI żyje”;
  - „Time & WiFi”: NTP startuje w zadaniu net;
  - „Redraw policy”: bez zmian w zasadach, dopisek, że UI śpi do najbliższego terminu
    (`uiSleepMs`);
  - usunąć zdanie o `SET_LOOP_TASK_STACK_SIZE(16 KB)` (jeśli gdzieś opisane) i poprawić
    komentarz w `src/net_config.h` („loop() is non-blocking cooperative…” → limity czasu
    chronią teraz zadanie net i WebServer, nie ekran).
- **README.md:**
  - „How it works, briefly” — nowy diagram:
    ```
    core 0 (net task)                              core 1 (UI, loop())
    WiFi · NTP · web page · OTA                    touch → screen_nav
    opensky_client ─▶ poll_pipeline ─▶ snapshot ══▶ table / now / radar (LovyanGFX)
    hexdb.io lookups (cached, keep-alive)  ▲        ▲ commands (install, skip)
                                           └────────┘
    ```
    plus jedno zdanie w tonie README („the screen no longer holds its breath while the
    network thinks”);
  - „Features”: punkt o płynnym interfejsie (dotyk i odliczanie nie stają w trakcie
    pobierania danych);
  - krótki akapit diagnostyczny przy „Configuring it from the browser”: `/perf.json` i log
    `[perf]` (dla dociekliwych / zgłaszających błędy).
- **Strona projektu (`site/index.html`):**
  - „For the curious” — nowy punkt (para `.en` / `.pl`), np. EN: *„Two CPU cores, two jobs:
    core 0 argues with the internet, core 1 draws. The screen no longer freezes while a
    server thinks.”* / PL: *„Dwa rdzenie, dwie role: rdzeń 0 kłóci się z internetem, rdzeń 1
    rysuje. Ekran już nie zamiera, kiedy serwer się zastanawia.”*;
  - FAQ: bez zmian (nikt o to nie pytał — zamrożenia znikają po cichu);
  - „What's next” i „What changed” aktualizują się same (ROADMAP.md, tagi git);
  - `python tools/build_site.py`, przegląd `_site/` (PL, EN, 360 px).
- **ROADMAP.md:** krok 0 — #2 do „In progress”; na końcu do „Done” z werdyktem zawierającym
  zmierzone liczby „przed / po” (np. „max UI gap 2840 ms → 18 ms”), PL w komentarzu.
- Zrzuty ekranu: **bez zmian** (wygląd się nie zmienia); jedynie sprawdzić, że
  `python tools/screenshots.py --device …` (korzysta z `/screenshot.bmp?screen=3`) działa
  po przeróbce §5.7.

## 6. Endpointy

| Metoda | Ścieżka | Zmiana |
|---|---|---|
| `GET` | `/perf.json` | **nowy** — `perf_stats` (§5.9); 401 / 429 jak reszta przy haśle |
| `GET` | `/screenshot.bmp[?screen=N]` | ten sam wynik, nowa ścieżka przez UI (§5.7); 503 gdy UI nie odpowie w 2 s albo brak pamięci na pasy |
| reszta | — | bez zmian (działają w zadaniu net) |

## 7. Plan prac (kolejność, każdy krok z testami `pio test -e native`)

0. ROADMAP: #2 z „Planned” do „In progress”.
1. **Pomiar „przed”** (na obecnym kodzie): `perf_stats` (testy → implementacja), wpięty
   w dzisiejszy `loop()`; 30 min na płytce w typowym ruchu + wywołanie strony ustawień,
   `/screenshot.bmp`, „Sprawdź teraz” na GitHubie. Do §10: max / p99 przerwy UI, czas
   handshake'u hexdb.io vs. całe zapytanie, najdłuższy ciągły odcinek CPU w handshake'u
   GitHuba, zapas stosu pętli. Osobny commit — przydatny niezależnie od reszty.
2. `core_link` — testy (`LatestBox`: publikacja / odbiór / tylko najnowsza / destruktor poza
   blokadą; `diffNetState` dla każdego pola; `uiSleepMs`; `netLooksStalled`;
   `makeInstallCommand` ucinanie), potem implementacja.
3. `poll_pipeline` — testy (§5.4) **na dzisiejszym zachowaniu** (najpierw testy, które
   przechodzą na kodzie przeniesionym 1:1 z `main.cpp`), potem rozdział na fazy A/B.
   `main.cpp` woła pipeline — wciąż jednordzeniowo; płytka działa jak przed zmianą
   (krok kontrolny, osobny commit).
4. `net_task` + nowa pętla UI: przeniesienie WiFi / portalu / pollingu / release_check,
   publikacja `NetState`, komendy, sen UI, `setup()` (§5.10), stos pętli z powrotem 8 KB.
5. OTA przez `OtaProgress` (oba źródła: upload i GitHub), `shouldMarkAppValid(…, uiAlive)`
   (test w `test_ota_update`).
6. Zrzut ekranu pasami (§5.7), `bandOrder` z testem w `test_screenshot`;
   `tools/screenshots.py --device` jako test na żywo. Jeśli spec 03 już jest — przeniesienie
   jej wywołań (§5.11).
7. `GET /perf.json`; pomiar „po” (te same 30 min co w kroku 1), do §10.
8. Keep-alive hexdb.io (§5.8) — tylko jeśli krok 1 pokazał sens; pomiar przed/po.
9. Test na płytce (§9), wyniki do §10.
10. Dokumentacja (§5.12): CLAUDE.md, README, `site/index.html` + `build_site.py`, ROADMAP →
    Done z liczbami.

Każdy krok kompiluje się (`pio run`) i przechodzi `pio test -e native`; kroki 3 i 4 zostawiają
płytkę w działającym stanie (da się bisectować).

## 8. Decyzje właściciela (2026-10-24)

Wszystkie rekomendacje przyjęte.

1. **Dwufazowa publikacja danych — tak.** Pozycje zaraz po `/states/all`, linia / typ / trasa
   po lookupach; drugie przerysowanie treści tylko gdy faza B coś dodała (§5.4).
2. **Keep-alive hexdb.io — tak, w tym projekcie**, warunkowo po pomiarze z kroku 1 (krok 8,
   §5.8). Jeśli zysk < 30 % czasu fazy B — odkładamy i zapisujemy w §10.
3. **`/perf.json` na stałe w firmware — tak**, za hasłem panelu jak reszta (§5.9), obok logu
   `[perf]`.
4. **Zawieszone zadanie net — czerwona kropka po 60 s bez bicia serca, bez restartu**
   (`netLooksStalled()`, §5.3). Restart zostaje sprawą watchdoga.
5. **Strona ustawień w trakcie pollingu odpowiada z opóźnieniem — akceptujemy w v1.** Osobne
   zadanie dla WebServera zostaje w „Poza zakresem” (§3).

## 9. Kryteria akceptacji (test ręczny na płytce)

- [ ] `pio test -e native` zielone; nowe testy: `test_core_link`, `test_poll_pipeline`,
      `test_perf_stats`, rozszerzone `test_ota_update`, `test_screenshot`.
- [ ] Po włączeniu chrome i „--” widać w < 1 s, zanim WiFi się połączy.
- [ ] 30 min typowego ruchu: log `[perf]` — **max przerwa UI < 100 ms, p99 < 20 ms, zero
      przerw ≥ 250 ms** (poza ekranem OTA). Liczby przed/po w §10.
- [ ] Stukanie w zakładki w trakcie pollingu (widać w logu `[main] poll…`) — każda zmiana
      ekranu natychmiast, żadne stuknięcie nie ginie.
- [ ] Ekran TERAZ z aktywnym odliczaniem przez 5 min: cyfry zmieniają się co sekundę, bez
      przeskoków o 2+ s przy pollingu.
- [ ] „Sprawdź teraz” (GitHub) i pobieranie `/backup.json` w trakcie odliczania — odliczanie
      nie staje.
- [ ] `/screenshot.bmp?screen=0..3` — poprawne obrazy, ekran po zrzucie wraca do normalnego
      widoku; czas zrzutu przed / po zapisany w §10 (oczekiwane: krócej, dzięki podwójnemu
      buforowi).
- [ ] OTA z panelu i z GitHuba: ekran postępu płynny, sukces → restart → obraz oznaczony jako
      ważny po ≥ 30 s; obraz testowy z zawieszoną pętlą UI (np. `for(;;)` po 8 s w `loop()`)
      **nie** zostaje oznaczony i wraca rollback.
- [ ] Brak restartów przez watchdog przez 24 h (log rozruchu: `rst:` / `Task watchdog`);
      `net max busy` w logu < 2 s.
- [ ] Zapas stosu po 24 h: UI ≥ 1,5 KB, net ≥ 2 KB; sterta po 24 h nie spada (brak wycieku
      migawek — `heap` w `[perf]` stabilny ±5 KB).
- [ ] Wyłączenie routera na 2 min: ekran działa, po ~60 s czerwona kropka i „BRAK DOSTĘPU…”,
      po powrocie WiFi dane wracają same.
- [ ] Hasło panelu, kopia zapasowa, zapis ustawień (zmiana języka → natychmiastowe
      przerysowanie), pytanie o aktualizację TAK / NIE — działają jak przed zmianą.

## 10. Wyniki implementacji

Płytka ESP32-2432S028R, arduino-esp32 2.0.17, poll co 30 s, konto OpenSky. „Przed” = kod
z commita b8051e5 z samym `perf_stats` w `loop()` (krok 1); „po” = ten projekt.

### 10.1 Pomiary

| | Przed (jedna pętla) | Po (dwa rdzenie) |
|---|---|---|
| Zamrożenie UI przy każdym pollingu OpenSky | 0,77–1,8 s | brak (net na rdzeniu 0) |
| Najdłuższa przerwa UI w minucie, sam polling (noc, 0 samolotów) | 0,77–4,5 s | — |
| Najdłuższa przerwa UI z ruchem webowym + „Sprawdź teraz” | **5,7 s** | **≤ 0,11 s** (pełne przerysowanie) |
| Przerwy ≥ 250 ms („slow”) przez ~40 min | 2–5 na minutę | **0** |
| p99 opóźnienia przebiegu UI | — | < 20 ms |
| Strona ustawień `/` | 5,0 s | 0,13 s |
| Zakładka Firmware | 4,95 s | 0,08 s |
| `/backup.json` | 0,43 s | 0,08 s |
| `/screenshot.bmp` (230 KB) | 3,46 s | 0,76–0,94 s (raz 3,2 s, gdy trwał polling) |
| Sprawdzenie GitHuba (TLS z weryfikacją) | 4,6 s zamrożenia UI | 3,5–3,9 s w net, UI bez zmian |
| Lookup hexdb.io | 2,4–3,1 s, z tego handshake 1,2–1,4 s (~48 %) | pierwszy 2,4–2,9 s, kolejne w partii **41–80 ms** |
| Faza B z 3 lookupami | 8,3 s | **3,2 s** (keep-alive) |
| Zapas stosu pętli (loopTask) | 8156 B z 16 KB | 4828 B z 8 KB |
| Zapas stosu `net` | — | 9,1–11,8 KB z 16 KB |
| Najdłuższa iteracja `net` | — | 8–12,7 s (polling + faza B, blokujące I/O — oddaje CPU, TWDT spokojny) |
| Upload OTA 1,45 MB przez stronę (po stronie płytki) | — | 21 s |
| Wolna sterta / największy blok, po starcie | ~206 KB / 110 KB (noc, 0 samolotów) | ~143 KB / 57 KB (dzień, 5–9 samolotów; sprite odliczania zarezerwowany przy starcie) |
| Największy blok po pierwszym wejściu na TERAZ | **41 KB** (< 50 KB progu GitHuba) | 57 KB, bez zmian |

Metryka „przerwa UI” po zmianie liczy **opóźnienie ponad zaplanowany sen** (czas rysowania +
zaspanie), nie surowy odstęp między przebiegami — pętla UI śpi z założenia co 20 ms, więc
surowy odstęp zawsze byłby ≥ 20 ms (pierwsza wersja metryki tak liczyła i p99 wychodziło
„< 50 ms”, choć nic nie czekało).

### 10.2 Testy na płytce (§9)

- [x] `pio test -e native`: 725 przypadków zielonych (nowe: `test_core_link` 26,
      `test_poll_pipeline` 9, `test_perf_stats` 8; rozszerzone `test_ota_update`,
      `test_screenshot`). `ERRORED` bez asercji = WinError 4551, ponowne uruchomienie przechodzi.
- [x] ~40 min ruchu (rano, 4–8 samolotów): zero przerw ≥ 250 ms, max 71–109 ms.
- [x] Strona, kopia, zakładka Firmware, `/perf.json`, „Sprawdź teraz” w trakcie pracy — UI bez
      przerw (log `[perf]`).
- [x] `/screenshot.bmp?screen=0..3` — poprawne obrazy (sprawdzone wzrokowo), ekran wraca.
- [x] OTA dobrym obrazem przez stronę: postęp na ekranie, restart, po restarcie `app1, valid`.
- [x] **Rollback przy zawieszonym UI:** obraz testowy (`for(;;)` w `loop()` po 8 s) przez OTA
      → `pending verify` → po 8 s UI stoi → po 30 s `[ota] the display loop has stopped on a
      new image: restarting to roll back` → `running from app0, valid`.
- [x] Brak restartów watchdoga w czasie testów (uptime 1265 s bez resetu w jednym przebiegu).
- [ ] Stukanie w zakładki w trakcie pollingu, odliczanie TERAZ przez 5 min, wyłączenie routera
      na 2 min, zmiana języka z panelu — **do sprawdzenia ręcznie** (wymaga palca / routera).
- [ ] Test 24 h (stos, sterta, watchdog) — nie wykonany w tej sesji.

### 10.3 Odstępstwa od projektu

1. **Restart przy zawieszonym UI** (`shouldRestartForRollback()`, nowe): samo „nie oznaczaj
   jako ważny” zostawiało płytkę na zamrożonym ekranie do odłączenia zasilania — rollback
   następuje dopiero przy restarcie. Teraz `net_task` restartuje obraz na próbie, gdy UI nie
   daje znaku życia > 2 s (po 30 s od startu).
2. **Faza B = pełne `buildDataSnapshot()` z pełnym budżetem** zamiast „planu różnicowego”
   (`planPhaseB`): powtórne sortowanie / CPA to milisekundy, a wyniki lookupów z fazy A są
   zapamiętane na czas pollingu (memo w `net_task`), więc nic nie jest pobierane ani logowane
   dwa razy. Prościej i łatwiej testować (test: B bez nowych kluczy == A).
3. **Zrzut ekranu:** handler czeka na UI odpytując atomik co 1 ms (`vTaskDelay(1)`) zamiast
   `xTaskNotify` w obie strony — bez ryzyka „zaległych” powiadomień po timeoucie; koszt
   pomijalny (15 pasów). Pasy po 16 wierszy (4 przy małej stercie), dwa bufory.
4. **`RefreshNow`** jest w `NetCommand`, ale UI go nie wysyła: po wybudzeniu w nocy polling i tak
   jest „due” (interwał minął), więc wystarcza flaga `pollingAllowed`.
5. **Keep-alive hexdb.io:** `HTTPClient` musi żyć tyle co sesja — jego destruktor robi
   `stop()` na gnieździe (pierwsza wersja tworzyła `HTTPClient` na każde zapytanie i każde
   zapytanie robiło handshake mimo `setReuse(true)`).
6. **OTA a watchdog rdzenia 0:** obserwator postępu robi `vTaskDelay(1)` co 16 KB, żeby
   szybki, ciągły strumień nie zagłodził zadania idle rdzenia 0.
7. **`/light.json`** czyta stan podświetlenia przez `Guarded<>` kopię (`backlight.cpp`), bo
   `backlightLoop()` działa na rdzeniu 1, a strona na rdzeniu 0.
8. **Sprite odliczania rezerwowany przy starcie** (`flightScreenReserveSprite()` w `setup()`,
   przed WiFi i `net_task`) — znalezione przy pomiarze sterty: tworzony przy pierwszym wejściu
   na TERAZ (~30 KB, 8-bit) lądował w środku największego wolnego bloku i dzielił go ze 110 KB
   do 41 KB na stałe, czyli poniżej 50 KB, przy których `release_check` w ogóle otwiera TLS do
   GitHuba (sprawdzanie i instalacja z GitHuba po cichu przestawały działać do restartu).
   Problem istniał już przed tym projektem; teraz blok ma stałe 57 KB od startu.
