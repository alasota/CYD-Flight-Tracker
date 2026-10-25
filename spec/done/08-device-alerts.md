# Spec 08 — Ostrzeżenia urządzenia: „!” na ekranie, ekran komunikatu, wyjaśnienie w panelu

Z przeglądu kodu 2026-10-25 (decyzje właściciela 2026-10-25). Status: **zrobione, v1.5.0**
(2026-10-25). Na płytce sprawdzone kryteria 1, 2, 5 i 7 z §5; 6 tylko na zrzucie demo (429 nie
da się wywołać na żądanie); 3–4 (dotyk) do potwierdzenia przy okazji. Stan logowania
(`OpenSkyAuthState`) wszedł jako pierwszy kawałek spec 06 (`src/opensky_auth.h`).
Pierwszy użytkownik mechanizmu: [spec 06](../planned/06-opensky-login-fallback.md) (błąd logowania OpenSky).

## 1. Cel

Jeden, wspólny sposób, w jaki urządzenie mówi „coś wymaga Twojej uwagi”:

- **Wyświetlacz:** wykrzyknik na pasku u góry ekranu (nagłówek), na wszystkich trzech ekranach —
  **czerwony** przy błędzie, **pomarańczowy** przy ostrzeżeniu.
- **Dotknięcie „!”** otwiera ekran z komunikatem: co się dzieje i co zrobić. Zamyka się
  przyciskiem albo sam po **15 s**.
- **Panel WWW:** na górze strony, nad zakładkami, ramka z tym samym wykrzyknikiem, pełnym
  wyjaśnieniem, co go wywołało, i linkiem do właściwej zakładki.

Kolejne ostrzeżenie dodaje się **jednym wierszem w tabeli definicji + jedną regułą + tekstami
(ekran i panel, PL/EN)**, bez zmian w rysowaniu nagłówka, ekranu komunikatu ani panelu.

Różnica wobec kropki stanu w nagłówku (`statusBarHealthColor()`): kropka mówi, w jakim stanie
jest **strumień danych w tej chwili**. Wykrzyknik mówi, że **jest coś do naprawienia albo do
wiedzy dla właściciela**, nawet jeśli dane płyną (np. tryb anonimowy po błędzie logowania:
kropka zielona, wykrzyknik czerwony).

## 2. Ostrzeżenia w v1

| `AlertId` | Kiedy aktywne | Poziom | Kolor „!” | Zakładka w linku |
|---|---|---|---|---|
| `OpenSkyLoginFailed` | `OpenSkyAuthState::Mode::LoginFailed` (spec 06) | błąd | czerwony | OpenSky |
| `OpenSkyQuotaExhausted` | `OpenSkyQuota::State::Exhausted` (ostatnia odpowiedź = 429) | błąd | czerwony | OpenSky |
| `OpenSkyBudgetOverLimit` | szacowane kredyty/dzień > dzienny limit (400 anonimowo / w `LoginFailed`, 4000 z kontem) | ostrzeżenie | pomarańczowy | OpenSky |

`OpenSkyBudgetOverLimit` to przyczyna, a `OpenSkyQuotaExhausted` skutek: przy 15 s pollach
wychodzi 5760 zapytań na dobę > 4000 i po kilkunastu godzinach kończą się kredyty. Pomarańczowy
„!” pojawia się od razu po zapisaniu takich ustawień, zanim limit się skończy.

Wszystkie ostrzeżenia w v1 są widoczne i na ekranie, i w panelu. Pole `onDisplay` w definicji
zostaje: dla przyszłych ostrzeżeń, które mają być tylko w panelu.

Kandydaci na później (mechanizm przyjmie ich bez zmian w kodzie rysowania): brak odpowiedzi
OpenSky dłużej niż X min, mało pamięci (`maxBlock` < 50 KB, sprawdzanie aktualizacji
wstrzymane), nieudana weryfikacja TLS GitHuba.

## 3. Projekt

### 3.1 Czysty moduł `device_alerts` (`src/device_alerts.h/.cpp`)

```cpp
enum class AlertId : uint8_t { OpenSkyLoginFailed, OpenSkyQuotaExhausted, OpenSkyBudgetOverLimit, Count };
enum class AlertSeverity : uint8_t { None, Warning, Error };   // rosnąco: max() = najpoważniejsze

struct AlertDef {
  AlertId id;
  AlertSeverity severity;
  bool onDisplay;        // „!” w nagłówku + wpis na ekranie komunikatu
  SettingsTab tab;       // dokąd prowadzi link w panelu
  Str displayTitle;      // ekran: „BŁĄD LOGOWANIA OPENSKY” (i18n.h, isFontSafeText)
  WebStr webTitle;       // panel: „Błąd logowania OpenSky”
  WebStr webAction;      // panel: „Co zrobić: …”
  const char *code;      // stabilny identyfikator do logów: "opensky_login_failed"
};

struct AlertSet {        // bitmaska, POD — tani do kopiowania i porównania w NetState
  uint32_t bits = 0;
  bool has(AlertId) const;
  void set(AlertId);
  bool any() const;
  AlertSeverity displaySeverity() const;  // najpoważniejsze z onDisplay; None = brak „!”
};

// Liczby do treści komunikatów (ekran i panel). POD, w NetState obok AlertSet.
struct AlertDetails {
  int loginHttpCode = 0;             // spec 06: kod odrzucenia przez serwer logowania
  uint32_t quotaResetInS = 0;        // do odnowienia puli, stan z chwili wyliczenia
  uint32_t quotaObservedMs = 0;      //   … liczone od tego millis() (jak OpenSkyQuota)
  uint32_t estimatedDailyCredits = 0;
  uint32_t dailyLimit = 0;           // 400 / 4000
  uint32_t minPollIntervalS = 0;     // minPollIntervalForLimit()
};

struct AlertInputs {
  OpenSkyAuthState auth;
  OpenSkyQuota quota;
  uint32_t estimatedDailyCredits;    // openSkyDailyCredits(radius, poll, pausedMinutesPerDay())
  uint32_t dailyLimit;               // openSkyDailyLimit(auth)
  float radiusDeg;
  uint32_t pausedMinutesPerDay;
};

AlertSet collectAlerts(const AlertInputs &in);                  // wszystkie reguły w jednym miejscu
AlertDetails collectAlertDetails(const AlertInputs &in);
const AlertDef &alertDef(AlertId id);                           // z tabeli kAlertDefs[]
std::vector<AlertId> alertsInOrder(AlertSet s, bool displayOnly); // błędy przed ostrzeżeniami, potem wg id
uint16_t alertBadgeColor(AlertSeverity s);                      // Error -> THEME_ALERT, Warning -> THEME_WARNING
std::string alertDisplayBody(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang);
std::string alertWebWhy(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang);
std::string alertChangeLog(AlertSet before, AlertSet after);    // "[alert] + opensky_login_failed"
uint32_t minPollIntervalForLimit(float radiusDeg, uint32_t limit, uint32_t pausedMinutesPerDay);
```

Zasady:

- Ostrzeżenia są **wyliczane** z bieżącego stanu, a nie zapamiętywane jako zdarzenia: znikają
  same, gdy przyczyna minie (logowanie się udało, poll znów zwraca 200, zmieniono interwał).
  Nie trzeba niczego „potwierdzać”; zamknięcie ekranu komunikatu ostrzeżenia nie kasuje.
- `static_assert(static_cast<int>(AlertId::Count) <= 32)`. Tabela `kAlertDefs[]` ma dokładnie
  `Count` wierszy w kolejności enumu (test).
- `minPollIntervalForLimit`: najkrótszy interwał w sekundach, przy którym szacunek mieści się
  w limicie (np. 22 s dla 4000 i 1 kredytu, 216 s dla 400).

### 3.2 Kolory (`lcars_theme`)

- Nowa rola `THEME_WARNING` = `LCARS_ORANGE` (`0xFC00`, `#FF8000`) — dziś zdefiniowany, nigdzie
  nierysowany. Na tle czarnym wyraźnie inny niż brzoskwiniowy `THEME_CHROME_ACCENT` (`0xFCCC`):
  jest nasycony i ciemniejszy. Jeśli na płytce okaże się za podobny, zmienić tylko tę stałą.
- Błąd: istniejące `THEME_ALERT` (`#FF3333`).
- Panel WWW: `--alert:#ff3333`, `--warn:#ff8000` — te same barwy co na ekranie.

### 3.3 Przepływ (rdzeń 0 → rdzeń 1)

- `net_task` po każdym pollu i po przeładowaniu Config liczy `state.alerts = collectAlerts(...)`
  i `state.alertDetails = collectAlertDetails(...)`. Przy zmianie `alerts` loguje
  `alertChangeLog()`, raz.
- `NetState` dostaje pola `AlertSet alerts` i `AlertDetails alertDetails`. `netStateDiffers()`
  porównuje oba (bez `quotaObservedMs`, żeby nie publikować co poll bez powodu).
- `diffNetState()` ustawia nowe zdarzenie `RedrawEvents::alertsChanged`, gdy zmieni się
  `displaySeverity()`. `computeRedrawMask()`: `alertsChanged` → `kRedrawHeader` (tylko nagłówek,
  jak `healthChanged`).

### 3.4 Wyświetlacz — nagłówek (`status_bar`)

- `drawStatusBar(..., AlertSeverity badge, ...)`, wywoływane z `main.cpp` z
  `net->alerts.displaySeverity()`.
- **Odznaka:** wypełnione koło o średnicy 13 px w kolorze `alertBadgeColor()`, czarny „!”
  (`THEME_ON_FILL`) na środku, w y: 1..13 (mieści się w 15 px nagłówka nad 1 px linią).
  Pozycja: 4 px na prawo od nazwy `SKY TRACKER`.
- `statusBarZones(..., bool badge)` zwraca dodatkowo `alert_x` (−1 bez odznaki); licznik lotów jest
  centrowany w przerwie między **prawym brzegiem odznaki** (albo nazwy) a datą. Istniejący test
  „strefy się nie nakładają dla najdłuższych tekstów w obu językach” rozszerzony o wariant
  z odznaką.
- Na ekranach serwisowych (OTA, pytanie o aktualizację, ekran komunikatu) nagłówka nie ma, więc
  odznaki też nie.

### 3.5 Dotknięcie „!” → ekran komunikatu (`service_screen`, `screen_nav`, `main.cpp`)

**Strefa dotyku** (czysta, `screen_nav`): `alertBadgeHit(x, y, zones)` — prostokąt od lewej
krawędzi ekranu do `alert_x + 13 + 8` px, y: 0..**24** (nagłówek + 9 px zapasu w dół; odznaka
ma 13 px, a rezystancyjny dotyk na krawędzi bywa niedokładny — patrz „Touch calibration”).
Aktywna tylko, gdy odznaka jest narysowana. Sprawdzana w `handleTap()` **przed** innymi strefami;
nie nachodzi na pasek zakładek (y ≥ 210) ani na strefę stronicowania tabeli (pierwszy wiersz
od y: 116).

**Ekran komunikatu** (`drawAlertScreen()`, pełny ekran, jak pytanie o aktualizację):

```
┌──────────────────────────────────────────┐
│  (!)  UWAGA                              │  y 8..36   tytuł: kolor najpoważniejszego
│                                          │
│  (!) BŁĄD LOGOWANIA OPENSKY              │  wpis 1: ikona + tytuł (kolor poziomu)
│      Działa anonimowo: 400 kredytów/dzień│          treść, mała czcionka, muted
│  (!) ZA CZĘSTE ODPYTYWANIE               │  wpis 2
│      5760 z 4000 kredytów dziennie       │
│                                          │
│  Szczegóły: http://cyd-sky.local         │  y ~176
│           ┌──────────────┐               │
│           │ ZAMKNIJ  15 s│               │  przycisk y 196..232, min. 36 px wysokości
│           └──────────────┘               │
└──────────────────────────────────────────┘
```

- Wpisy: `alertsInOrder(alerts, /*displayOnly=*/true)`, maks. **3** (więcej = trzeci wiersz
  „+ N więcej — zobacz panel”). Tytuł: `displayTitle`, treść: `alertDisplayBody()` (jedna linia,
  przycięta `fitToWidth()`). Teksty wyłącznie z `i18n.h` (`Str`), `isFontSafeText()`.
- Przycisk `ZAMKNIJ` / `CLOSE` z odliczaniem sekund (`15 s` … `1 s`); odliczanie przerysowuje
  tylko przycisk, raz na sekundę.
- Geometria czysta i testowana: `alertScreenLayout(w, h, count)` → prostokąty tytułu, wpisów,
  podpowiedzi i przycisku.

**Zachowanie** (czysty automat w `service_screen`: `AlertScreen` + `alertScreenStep(state,
input) -> {state, close, redrawTimer}`, testowany bez sprzętu):

- Otwiera się po dotknięciu strefy odznaki. Przy wygaszonym ekranie w nocy pierwsze dotknięcie
  tylko budzi ekran (istniejąca reguła z „Brightness & night mode”) — nie otwiera komunikatu.
- **Zamyka się**, gdy:
  1. dotknięto przycisku **ZAMKNIJ** — liczy się tylko świeże dotknięcie, po puszczeniu palca,
     którym otwarto ekran (jak `acceptPromptTap()` przy pytaniu o aktualizację);
  2. minęło **15 s** od otwarcia (`kAlertScreenTimeoutMs = 15000`; dotknięcia poza przyciskiem
     nic nie robią i czasu nie wydłużają);
  3. wszystkie ostrzeżenia `onDisplay` zniknęły w trakcie (np. logowanie się udało) — od razu.
- Gdy ostrzeżenia zmienią się w trakcie (doszło / zniknęło jedno), ekran przerysowuje listę,
  odliczanie biegnie dalej.
- Po zamknięciu: powrót na ten sam ekran i stronę tabeli, pełne przerysowanie (`firstDraw`).
- Dopóki ekran komunikatu jest otwarty: auto-cycle wstrzymany (licznik startuje od nowa po
  zamknięciu), pytanie o aktualizację nie jest pokazywane (`PromptGate::alertScreenOpen`),
  ekran OTA ma pierwszeństwo i zamyka komunikat.
- `sleepUntilNextDeadline()`: dodatkowy termin — następna pełna sekunda odliczania.
- Zrzut ekranu: `/screenshot.bmp?screen=4` (indeks po pytaniu o aktualizację) rysuje ekran
  komunikatu z danych demonstracyjnych (`alertDemoState()`, wszystkie trzy ostrzeżenia) —
  do `docs/screenshots/alert_screen.png`.

### 3.6 Panel WWW (`config_page`, `web_i18n`)

`ConfigPageModel` dostaje `AlertSet alerts` i `AlertDetails alertDetails` (w `sendPage()` liczone
tym samym `collectAlerts()` z bieżących danych, żeby panel zgadzał się z ekranem). Jeśli
`alerts.any()`, **nad zakładkami, na każdej zakładce** jest ramka `.card.alert`:

```
┌──────────────────────────────────────────────────────────────┐
│ (!)  Wymaga uwagi                                            │
│      Wykrzyknik na pasku u góry wyświetlacza (czerwony =     │
│      błąd, pomarańczowy = ostrzeżenie) oznacza, że           │
│      urządzenie wykryło problem. Dotknij go na ekranie,      │
│      żeby zobaczyć krótki opis.                              │
│                                                              │
│  (!) Błąd logowania OpenSky                       [czerwony] │
│      OpenSky odrzucił client_id / client_secret (HTTP 401).  │
│      Urządzenie działa w trybie anonimowym: 400 kredytów     │
│      dziennie zamiast 4000.                                  │
│      Co zrobić: sprawdź dane na stronie konta OpenSky        │
│      i zapisz je ponownie.        → Przejdź do: OpenSky      │
│                                                              │
│  (!) Za częste odpytywanie                     [pomarańczowy]│
│      15 s = 5760 zapytań/dzień, limit to 4000.               │
│      Co zrobić: ustaw interwał ≥ 22 s albo wstrzymanie nocą. │
│                                   → Przejdź do: OpenSky      │
└──────────────────────────────────────────────────────────────┘
```

- Ikona `(!)` w CSS to to samo koło z czarnym „!” co na wyświetlaczu, w tym samym kolorze
  poziomu. Ikona w nagłówku ramki ma kolor najpoważniejszego aktywnego ostrzeżenia.
- Treści: `webTitle` / `webAction` z tabeli definicji + `alertWebWhy()` z liczbami (kod HTTP,
  `formatWait()` do odnowienia puli, wyliczony interwał). Wszystko przez `htmlEscape`, w obu
  językach, bez znaków zabronionych przez `test_web_i18n`.
- Link „Przejdź do: …” = `/?tab=<id>`. Brak ostrzeżeń = brak ramki (zero zmian w wyglądzie).

## 4. Testy (po spec 07: `test_admin`, `test_display`, `test_platform`)

- `device_alerts`: każda reguła z §2 osobno, w obie strony; kombinacje (login + limit naraz);
  `displaySeverity()`: tylko ostrzeżenie → Warning, ostrzeżenie + błąd → Error, nic → None;
  `alertBadgeColor` dla każdego poziomu; `alertsInOrder` — błędy przed ostrzeżeniami; tabela
  definicji kompletna, w kolejności enumu, `code` unikalne; `alertDisplayBody` /
  `alertWebWhy` dla każdego id w PL i EN, `displayBody` przechodzi `isFontSafeText()`;
  `alertChangeLog`; `minPollIntervalForLimit` (4000 → 22 s, 400 → 216 s, z nocną pauzą krócej).
- `core_link`: zmiana `alerts` / `alertDetails` → `netStateDiffers` true, sama zmiana
  `quotaObservedMs` → false; `diffNetState` ustawia `alertsChanged` tylko przy zmianie
  `displaySeverity()`.
- `redraw_policy`: `alertsChanged` → tylko `kRedrawHeader`.
- `status_bar`: `statusBarZones` z odznaką — strefy rozłączne dla najdłuższych tekstów PL / EN,
  licznik nie nachodzi na odznakę, `alert_x` = −1 bez odznaki.
- `screen_nav`: `alertBadgeHit` — trafienie w odznakę i w zapas 8 px / do y 24; pudło bez
  odznaki; strefa rozłączna z `navHitTest` i ze strefą stronicowania tabeli.
- `service_screen`: `alertScreenLayout` — wszystko na ekranie, bez nakładania, przycisk ≥ 36 px
  wysokości, 1/2/3 wpisy i „+ N więcej”; automat `alertScreenStep`: zamknięcie przyciskiem
  (tylko świeże dotknięcie po puszczeniu), dotyk poza przyciskiem nic nie zmienia, zamknięcie
  dokładnie po 15 000 ms (14 999 — jeszcze otwarty), zamknięcie, gdy ostrzeżenia znikną,
  odliczanie co pełną sekundę, wrap `millis()`.
- `release_check`: `shouldShowUpdatePrompt` false przy `alertScreenOpen`.
- `config_page`: bez ostrzeżeń brak `.card.alert`; każde ostrzeżenie renderuje tytuł, „co
  zrobić”, link do zakładki i właściwą klasę koloru; ramka jest na każdej zakładce; PL i EN.
  Zrzut `web_alert.png` do `tools/screenshots.py`.

## 5. Kryteria akceptacji (płytka)

1. Zły `client_secret` → w ciągu jednego pollu **czerwony** „!” w nagłówku na wszystkich trzech
   ekranach, kropka zielona, samoloty widoczne. Panel: ramka z „Błąd logowania OpenSky”.
2. Interwał 15 s z kontem → **pomarańczowy** „!” od razu po zapisie; interwał 30 s → znika.
   Oba naraz → „!” czerwony, na ekranie komunikatu dwa wpisy (błąd pierwszy).
3. Dotknięcie „!” (także na krawędzi odznaki) otwiera ekran komunikatu; ZAMKNIJ wraca na ten sam
   ekran i stronę tabeli; bez dotknięcia ekran zamyka się sam po 15 s; odliczanie na przycisku
   tyka co sekundę bez migania reszty ekranu.
4. W nocy przy wygaszonym ekranie pierwsze dotknięcie „!” tylko budzi ekran.
5. Poprawne dane zapisane w czasie, gdy komunikat jest otwarty → komunikat zamyka się sam, „!”
   znika, ramka znika z panelu.
6. Wyczerpane kredyty (429) → czerwony „!”, na ekranie komunikatu i w panelu czas do odnowienia
   puli; po odnowieniu oba znikają same.
7. Log: dokładnie jedna linia `[alert] + …` / `[alert] - …` na każdą zmianę.

## 6. Poza zakresem

- Historia ostrzeżeń, wyciszanie wybranych ostrzeżeń, powiadomienia push lub e-mail.
- Miganie / animacja odznaki (redraw policy: żadnych odświeżeń na timerze poza odliczaniem).
