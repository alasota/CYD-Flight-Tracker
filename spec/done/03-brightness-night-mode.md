# Spec 03 — Automatyczna jasność i tryb nocny

Pozycja #1 z „Planned” w [ROADMAP.md](../../ROADMAP.md) („Quick win: the CYD has a light sensor on
GPIO34 and a PWM backlight”). Status: **zrobione — wydane w v1.3.0** (przetestowane na płytce 2026-10-24) (decyzje
właściciela: §8, wyniki i odstępstwa: §10).

## 1. Cel

Wyświetlacz sam dopasowuje jasność do światła w pokoju (fotorezystor na GPIO34, podświetlenie
PWM na GPIO21), a w nocy — od zachodu do wschodu słońca w miejscu domu albo według godzin —
przyciemnia się albo gaśnie zupełnie. Opcjonalnie (domyślnie wyłączone) w nocy urządzenie
przestaje odpytywać OpenSky, żeby nie palić kredytów na samoloty, których nikt nie ogląda.

Kryterium sukcesu w jednym zdaniu: *w ciągu dnia ekran jest jasny przy oknie i przygaszony
wieczorem przy lampce, bez migania i skoków; o zachodzie słońca płynnie schodzi do jasności
nocnej, rano wraca sam; z włączonym wstrzymaniem w nocy w logu nie ma zapytań do OpenSky,
a dotknięcie ekranu przywraca dane na ustawiony czas.*

## 2. Stan obecny (co już jest)

- **Podświetlenie:** `include/LGFX_CYD.hpp` — `lgfx::Light_PWM`, `pin_bl = 21`, kanał LEDC 7,
  44,1 kHz. Jasność ustawia `tft.setBrightness(0..255)`.
- ⚠️ **Firmware nigdzie nie woła `setBrightness()`.** LovyanGFX startuje z
  `_brightness = 127` (`LGFXBase.hpp`), czyli płytka świeci dziś **na ~50 %**. Samo
  świadome ustawienie jasności to już widoczna zmiana — do sprawdzenia w kroku 1 (§7).
- **Czujnik światła:** fotorezystor (LDR) na przedniej stronie płytki, w dzielniku napięcia
  na **GPIO34** (ADC1_CH6, tylko wejście). ADC1 działa przy włączonym WiFi (ADC2 nie działa).
  Z doświadczeń społeczności: odczyt jest **odwrócony** (więcej światła → niższa wartość)
  i ma **wąski zakres** przy domyślnym tłumieniu 11 dB (jasno ≈ 0, ciemno — kilkaset),
  dlatego zwykle zaleca się `analogSetPinAttenuation(34, ADC_0db)`. **Nie zmierzone na naszej
  płytce** — krok 1 (§7). Nie wiadomo też, czy własne podświetlenie ekranu nie świeci w czujnik.
- **Czas:** `time_sync` — `isTimeSynced()`, `timeSyncNowLocal()`, strefa `TZ_WARSAW` z DST.
  Bez synchronizacji nie znamy godziny.
- **Pozycja domu:** `Config::home_lat` / `home_lon` (+ `home_configured`) — wystarczy do
  policzenia wschodu i zachodu słońca, bez żadnej sieci.
- **Pętla:** `main.cpp` — `loop()` bez `delay()`, `millis()`; polling OpenSky przez
  `openSkyClientPoll(millis(), cachedCfg)` — wystarczy go nie wołać, żeby nie odpytywać.
  Dotyk edge-detected (`handleTap()`), ekrany serwisowe (OTA, pytanie o aktualizację, reset
  hasła) przejmują ekran.
- **Kredyty:** 15 s polling = 5760 kredytów/dobę > 4000 konta (CLAUDE.md, „Rate limits”).
  Wstrzymanie pollingu na noc 23:00–6:30 to 1800 zapytań mniej — przy 15 s mieścimy się
  w limicie. Dlatego opcja, ale świadoma: domyślnie wyłączona (§8.3).
- **Zrzuty ekranu:** `tools/screenshots.py` — strony panelu z `test_config_page`
  (`SCREENSHOT_OUT`, headless Chrome / Edge), dziś tylko zakładka Firmware
  (`web_firmware*.png`); `web_general.png` i `web_opensky.png` w `docs/screenshots/` są
  starsze i nie pokażą nowej sekcji. Strona projektu (`site/index.html`) ma galerię panelu
  z tymi plikami.
- **Brak:** odczytu czujnika, sterowania jasnością, harmonogramu nocnego, obliczeń słońca.

## 3. Zakres

### W zakresie (v1)

1. Nowy czysty moduł **`backlight`**: filtr odczytu czujnika, krzywa „światło → jasność”
   z histerezą, korekcja percepcyjna (gamma), płynne przejścia, decyzja nocna, wybudzenie
   dotykiem, decyzja o pollingu; adapter (ADC + `setBrightness`) za `#ifdef ARDUINO`.
2. Nowy czysty moduł **`sun_times`**: wschód / zachód słońca z daty i współrzędnych (algorytm
   NOAA, bez sieci), z obsługą dnia / nocy polarnej.
3. **Tryby jasności:** *automatyczna* (czujnik, z min / maks; **domyślna**) albo *stała*
   (suwak).
4. **Tryb nocny:** *wyłączony* / *według godzin* (od–do) / *od zachodu do wschodu słońca*
   (**domyślny**, jasność nocna **10 %**); jasność nocna 0 % = ekran wygaszony.
5. **Wybudzenie dotykiem:** w nocy dotknięcie zapala ekran / wznawia dane na **czas
   wybudzenia** (ustawienie, domyślnie 60 s); przy wygaszonym ekranie pierwsze dotknięcie
   tylko zapala (nie przełącza zakładek).
6. **Wstrzymanie pobierania danych w nocy** — ustawienie w panelu, **domyślnie wyłączone**;
   włączone: w oknie nocnym brak pollingu OpenSky (poza czasem wybudzenia), po wybudzeniu
   pierwszy polling od razu.
7. **Strona ustawień, zakładka Ogólne:** sekcja „Ekran” — tryb jasności, suwaki, tryb nocny,
   godziny, jasność nocna, czas wybudzenia, wstrzymanie pobierania, **podgląd na żywo**
   (odczyt czujnika, bieżąca jasność, „teraz: dzień/noc”, dzisiejszy wschód/zachód).
8. `config_store`, `config_backup` (grupa `display`), `web_i18n`.
9. Ekrany serwisowe (OTA, reset hasła, pytanie o aktualizację) zawsze na pełnej jasności;
   pytanie o aktualizację nie pojawia się na wygaszonym ekranie.
10. **Zrzuty ekranu** (§5.8): `web_general.png` (nowa sekcja „Ekran”) i `web_opensky.png`
    (szacunek kredytów z nocą) generowane z danych testowych przez `tools/screenshots.py`.
11. **Strona projektu** (`site/`, §5.9): sekcja o jasności i trybie nocnym PL/EN, odświeżone
    zrzuty w galerii panelu.
12. Dokumentacja: CLAUDE.md (nowa sekcja + moduły), README, ROADMAP → Done.

### Poza zakresem (na później)

- **Wybudzenie przez samolot** (ekran zapala się, gdy coś będzie nad domem za < 60 s) —
  nie w v1 (decyzja §8.4). Z wstrzymanym pobieraniem i tak nie wiemy o samolotach; wymagałoby
  rzadkiego pollingu w nocy.
- **Nocna paleta kolorów** (czerwona / przygaszona, jak w kokpicie) — `THEME_*` to dziś
  stałe `constexpr`; zmiana w locie wymaga przerobienia ich na odczyt z aktywnej palety we
  wszystkich modułach widoku. Osobna pozycja, jeśli ktoś będzie chciał.
- **Inne harmonogramy w weekend**, kilka okien nocnych, „nie przeszkadzać” z Home Assistant.
- **Kalibracja czujnika z kreatora / z panelu** — progi są stałymi z pomiaru (§8.5); podgląd
  surowego odczytu na stronie jest do diagnostyki.
- **Czujnik zewnętrzny** (BH1750 itp. na I2C) — CYD ma wbudowany, wystarczy.
- Wolniejszy polling w nocy zamiast wstrzymania.

## 4. Przepływ użytkownika

### 4.1 Dzień, tryb automatyczny (domyślny)

1. Po starcie ekran zapala się na **pełnej jasności** (ekran resetu hasła, logo, WiFi — zanim
   czujnik ma sensowną średnią), po ~2 s przechodzi płynnie na jasność z czujnika.
2. Zapalenie lampy / zasłonięcie okna → jasność zmienia się płynnie w ~1–2 s; drobne wahania
   (cień ręki, migotanie świetlówki) nie zmieniają nic (histereza + filtr).
3. Log co zmianę poziomu: `[light] raw=412 avg=398 -> 35% (pwm 61)`.

### 4.2 Noc (ustawienia domyślne: od zachodu do wschodu, 10 %, bez wstrzymania)

1. O zachodzie słońca ekran płynnie (~3 s) schodzi do jasności nocnej:
   `[light] night (sun 17:23-07:17): 10%`. Dane odświeżają się jak w dzień.
2. O wschodzie płynnie wraca do jasności dziennej.

### 4.3 Noc z wygaszeniem i wstrzymaniem pobierania (opcje włączone przez użytkownika)

1. Początek nocy → ekran schodzi do jasności nocnej (przy 0 % gaśnie), a przy włączonym
   wstrzymaniu polling staje:
   `[light] night (schedule 23:00-06:30): backlight off, polling paused`.
2. Dotknięcie ekranu → na **czas wybudzenia** (domyślnie 60 s) polling wraca od razu (po
   chwili widać świeże samoloty), a wygaszony ekran zapala się na jasności nocnej albo, gdy ta
   wynosi 0 %, na minimalnej jasności dziennej (`brightness_min`, nie mniej niż 10 %).
   Dotknięcie, które zapaliło wygaszony ekran, **niczego nie przełącza**. Każde kolejne
   dotknięcie przedłuża wybudzenie. Potem ekran znowu gaśnie / polling znowu staje.
3. Wstrzymanie bez wygaszenia (np. 10 % i wstrzymanie) też działa: ekran świeci słabo
   z ostatnią listą, dotknięcie wznawia dane na czas wybudzenia (ten dotyk działa normalnie —
   ekran był widoczny).
4. Koniec nocy → ekran wraca, polling rusza sam.

### 4.4 Strona ustawień — zakładka Ogólne, nowa sekcja „Ekran”

```
Ekran
  Jasność            (•) automatyczna (czujnik światła)   ( ) stała
  Minimalna / maks.  [ 10 ] % / [ 100 ] %                  (tylko automatyczna)
  Stała jasność      [ 80 ] %                               (tylko stała)
  Tryb nocny         [ od zachodu do wschodu ▾ ]  wyłączony / według godzin / od zachodu do wschodu
  Noc od – do        [ 23:00 ] – [ 06:30 ]                  (tylko „według godzin”)
  Jasność w nocy     [ 10 ] %   (0 % = wygaszony)
  Po dotknięciu w nocy  [ 60 ] s   świeć / pobieraj dane
  [ ] Wstrzymaj pobieranie danych w nocy
      Oszczędza kredyty OpenSky: dziś w nocy ok. 3300 zapytań mniej (przy 15 s).
      Dotknięcie ekranu wznawia pobieranie na czas wybudzenia.

  Teraz: czujnik 412 (średnio 398) · jasność 35 % · dzień
  Dziś słońce: wschód 07:17, zachód 17:23 (dla Twojej lokalizacji)
```

- Wiersz „Teraz” odświeża się co 2 s skryptem z `GET /light.json` tylko, gdy zakładka Ogólne
  jest widoczna (diagnostyka: zasłoń czujnik palcem i patrz na liczbę).
- Bez ustawionej lokalizacji opcja „od zachodu do wschodu” daje „nigdy noc” z podpowiedzią
  „ustaw lokalizację”; bez synchronizacji czasu wiersz pokazuje „czas niezsynchronizowany —
  tryb nocny nieaktywny”.
- „Czas wybudzenia” i „Wstrzymaj pobieranie” są wyszarzone, gdy tryb nocny jest wyłączony.
- Szacunek oszczędności liczy ta sama funkcja co szacunek „kredyty na dobę” na zakładce
  OpenSky — i ten szacunek uwzględnia teraz wstrzymanie w nocy (§5.6).

## 5. Projekt techniczny

### 5.1 Nowy moduł czysty: `sun_times` (`src/sun_times.h/.cpp`, `test/test_sun_times/`)

```cpp
struct SunTimes {
  enum class Kind { Normal, PolarDay, PolarNight } kind = Kind::Normal;
  int sunrise_utc_min = 0;  // minuty od północy UTC danego dnia
  int sunset_utc_min = 0;
};

// Algorytm NOAA (uproszczony, dokładność ~1–2 min dla |lat| < 66°), wysokość słońca
// -0.833° (refrakcja + promień tarczy). Data jako rok / miesiąc / dzień — bez time_t,
// bez strefy, testowalne bez zegara.
SunTimes computeSunTimes(int year, int month, int day, float lat_deg, float lon_deg);
```

- Konwersję UTC → czas lokalny (CET/CEST) robi adapter przez `localtime_r` — strefa i DST
  już są ustawione przez `time_sync`. Czysta funkcja nie zna strefy.
- Liczone raz na dobę (zmiana daty lokalnej) i po zmianie lokalizacji; wynik w pamięci.
- **Testy** (tolerancja ±3 min, wartości z tablic astronomicznych — zweryfikować z
  timeanddate.com przy pisaniu testów, nie z pamięci):
  - Warszawa (52,23 N, 21,01 E): 21.06 — wschód ~02:14 UTC, zachód ~19:01 UTC;
    21.12 — wschód ~06:43 UTC, zachód ~14:25 UTC.
  - Równik / południk 0 w ekwinokcjum — ~06:00 / ~18:00 UTC (± równanie czasu).
  - Tromsø (69,65 N) w czerwcu → `PolarDay`, w grudniu → `PolarNight`.
  - Długość geograficzna ujemna (Nowy Jork) i półkula południowa (Sydney) — znak się zgadza.
  - Zachód „po północy UTC” (daleki zachód) — wynik zawija się w 0..1439.

### 5.2 Nowy moduł czysty: `backlight` (`src/backlight.h/.cpp`, `test/test_backlight/`)

**Filtr czujnika.** Próbka co 100 ms (`kLightSampleMs`); mediana z 5 ostatnich (odrzuca
pojedyncze szpilki ADC), potem EMA (`alpha` ≈ 0,1 → stała czasowa ~1 s).

```cpp
class LightFilter {
 public:
  void add(uint16_t raw);          // co kLightSampleMs
  bool ready() const;              // >= 5 próbek
  float average() const;
};
```

**Krzywa światło → jasność** (tryb automatyczny). Dwa progi odczytu z kalibracji (§7 krok 1),
jako stałe w kodzie, nie ustawienia (§8.5): `kLightRawBright` (pełne światło →
`brightness_max`) i `kLightRawDark` (ciemno → `brightness_min`). Pomiędzy — liniowo w skali
logarytmicznej odczytu (oko i LDR są w przybliżeniu logarytmiczne). Działa w obie strony
niezależnie od tego, czy `kLightRawBright < kLightRawDark` (odczyt odwrócony).

```cpp
// Wynik w procentach 0..100 (to, co widzi użytkownik); histereza: nowy poziom tylko, gdy
// różni się od poprzedniego o >= kLightHysteresisPct (5 %).
uint8_t autoBrightnessPct(float avgRaw, uint8_t minPct, uint8_t maxPct, uint8_t previousPct);
```

**Procenty → PWM.** Jasność odczuwana nie jest liniowa w wypełnieniu PWM: `pwm = 255 ·
(pct/100)^2.2`, a każdy `pct > 0` daje co najmniej `kMinVisiblePwm` (do zmierzenia; poniżej
pewnego wypełnienia podświetlenie CYD po prostu gaśnie). `pct == 0` → `pwm == 0`.

```cpp
uint8_t pctToPwm(uint8_t pct);
```

**Płynne przejścia.** Zmiana wypełnienia ograniczona prędkością: dzień ~1 s na pełny zakres,
wejście w noc / wyjście z nocy ~3 s, wybudzenie dotykiem — natychmiast (ktoś właśnie dotknął).

```cpp
uint8_t stepPwm(uint8_t current, uint8_t target, uint32_t dtMs, uint32_t fullRangeMs);
```

**Decyzja nocna, wybudzenie, polling.**

```cpp
enum class NightMode : uint8_t { Off = 0, Schedule = 1, Sun = 2 };

// Okno nocne w minutach od północy czasu lokalnego; obsługuje okno przez północ
// (23:00–06:30) i zwykłe (01:00–05:00); from == to → nigdy noc.
bool inNightWindow(int nowMin, int fromMin, int toMin);

// Ile minut okna nocnego zostało / trwa dziś — do szacunku kredytów (§5.6).
int nightWindowMinutes(int fromMin, int toMin);

struct BacklightInputs {
  bool time_synced;          // bez czasu nie ma nocy
  int now_local_min;         // 0..1439
  NightMode night_mode;
  int night_from_min, night_to_min;           // Schedule
  SunTimes sun_local;        // Sun: już w minutach lokalnych; PolarDay = nigdy noc, PolarNight = zawsze
  bool home_configured;      // Sun bez lokalizacji = nigdy noc
  bool auto_mode;
  uint8_t fixed_pct, min_pct, max_pct, night_pct;
  bool light_ready; float light_avg;
  uint32_t now_ms, woken_until_ms;   // wybudzenie dotykiem
  bool service_screen;       // OTA / reset hasła / pytanie o aktualizację
};

struct BacklightDecision {
  uint8_t target_pct;
  bool night;                // jesteśmy w oknie nocnym
  bool woken;                // noc, ale w czasie wybudzenia
  bool screen_off;           // target 0 — ekran wygaszony
  uint32_t ramp_ms;          // jak szybko dojść do celu
};

BacklightDecision decideBacklight(const BacklightInputs &in, uint8_t previousPct);

// Dotyk w nocy: ustawia woken_until_ms = now + wakeS·1000 (przedłuża przy każdym dotyku).
// Zwraca true („zjedzony” — nie wołać handleTap()), tylko gdy ekran był wygaszony.
// W dzień nic nie robi i zwraca false.
bool onTouchWake(const BacklightDecision &d, uint32_t nowMs, uint32_t wakeS,
                 uint32_t &wokenUntilMs);

// Czy odpytywać OpenSky: tak, chyba że noc && pauseAtNight && !woken.
bool shouldPollOpenSky(const BacklightDecision &d, bool pauseAtNight);
```

Reguły `decideBacklight()` (każda z testem):
- `service_screen` → 100 %, natychmiast (ekrany serwisowe zawsze czytelne).
- `!time_synced` → nigdy noc (tylko tryb dzienny).
- noc i wybudzony (`now_ms < woken_until_ms`, porównanie odporne na przepełnienie `millis()`)
  i `night_pct == 0` → `max(min_pct, 10)` — wybudzony ekran świeci jak najciemniejszy dzień,
  ale nie ciemniej niż 10 %; przy `night_pct > 0` wybudzenie jasności nie zmienia (dotyczy
  tylko pollingu).
- noc → `night_pct`, `screen_off = (night_pct == 0)`, rampa 3 s.
- dzień, auto i `light_ready` → `autoBrightnessPct()`, rampa 1 s; auto i jeszcze nie `ready`
  → `max_pct` (start na pełnej, §4.1).
- dzień, stała → `fixed_pct`.
- noc nigdy nie rozjaśnia ekranu: gdy dzienna jasność (np. auto w ciemnym pokoju dało 5 %)
  jest niższa niż `night_pct`, bierzemy **mniejszą**.

### 5.3 Adapter (`backlight.cpp`, `#ifdef ARDUINO`)

```cpp
void backlightBegin(LGFX &tft);                       // ADC: analogSetPinAttenuation(34, ...) wg kroku 1
void backlightLoop(LGFX &tft, const Config &cfg, bool serviceScreen);  // próbka co 100 ms, decyzja, rampa
const BacklightDecision &backlightDecision();
bool backlightHandleTouch(const Config &cfg);         // true = dotyk zjedzony
BacklightStatus backlightStatus();                    // do /light.json i logu
```

- `tft.setBrightness()` tylko przy zmianie wypełnienia (nie co pętlę).
- **Własne światło w czujnik:** jeśli krok 1 pokaże, że podświetlenie zmienia odczyt w ciemnym
  pokoju, próbka jest brana z kompensacją (odjęcie zmierzonego wpływu dla bieżącego PWM).
  Przygaszanie ekranu na czas pomiaru odpada (miganie). Decyzja po pomiarze, zapisana w §10.
- Rysowanie przy wygaszonym ekranie działa dalej jak zwykle (bufor panelu jest aktualny,
  `/screenshot.bmp` pokazuje treść, po wybudzeniu nic nie trzeba odświeżać).

### 5.4 `main.cpp`

- `setup()`: `backlightBegin(tft)` zaraz po `tft.init()`, z `setBrightness(255)` — ekran resetu
  hasła i start zawsze na pełnej jasności (dziś 127).
- `loop()`:
  1. `backlightLoop(tft, cachedCfg, serviceScreenActive)` na początku, gdzie
     `serviceScreenActive` = ekran OTA / pytanie o aktualizację / „aktualizacja nieudana”.
  2. Dotyk: przy nowym naciśnięciu najpierw `backlightHandleTouch(cachedCfg)`; `true` → nie
     wołać `handleTap()` (dotyk tylko zapalił ekran).
  3. Polling: `openSkyClientPoll()` tylko, gdy `shouldPollOpenSky(decision,
     cachedCfg.night_pause_polling)`. Log raz przy zmianie stanu:
     `[light] polling paused (night)` / `[light] polling resumed (touch, 60 s)` /
     `[light] polling resumed (day)`.
  4. Auto-cycle: przy wygaszonym ekranie stoi (zmiana ekranu, której nikt nie widzi, tylko
     zapisuje NVS); timer resetuje się przy wybudzeniu.
  5. `releaseCheck`: `shouldShowUpdatePrompt()` dostaje nowy parametr `screenOff` — pytanie
     tylko na zapalonym ekranie; samo sprawdzanie GitHuba może iść w nocy (raz na dobę).
- Wybudzenie po wstrzymanym pollingu: `openSkyClientPoll()` jest już „due” (interwał minął),
  więc dane odświeżą się w pierwszej pętli; do tego czasu widać ostatnią listę, jak przy
  błędzie sieci. Kropka zdrowia w nagłówku zostaje zielona (wstrzymanie to nie błąd).

### 5.5 `config_store`, `config_backup`

Nowe pola `Config` (klucze NVS ≤ 15 znaków):

| Pole | NVS | Typ | Domyślnie | Zakres / sanitizacja |
|---|---|---|---|---|
| `brightness_auto` | `bl_auto` | bool | `true` | — |
| `brightness_fixed_pct` | `bl_fix` | u8 | 80 | 5..100 |
| `brightness_min_pct` | `bl_min` | u8 | 10 | 5..100, ≤ max |
| `brightness_max_pct` | `bl_max` | u8 | 100 | 5..100, ≥ min |
| `night_mode` | `nt_mode` | u8 (`NightMode`) | `Sun` | spoza zakresu → `Sun` |
| `night_from_min` | `nt_from` | u16 | 1380 (23:00) | 0..1439 |
| `night_to_min` | `nt_to` | u16 | 390 (06:30) | 0..1439 |
| `night_pct` | `nt_pct` | u8 | 10 | 0..100 |
| `night_wake_s` | `nt_wake` | u16 | 60 | 10..600 |
| `night_pause_polling` | `nt_poll` | bool | `false` | — |

- `sanitizeConfig()` + czyste `clamp*` z testami (min > max → zamiana miejscami).
- `config_backup`: nowa grupa `"display"` (`brightness_auto`, `brightness_fixed`,
  `brightness_min`, `brightness_max`, `night_mode` jako tekst `off`/`schedule`/`sun`,
  `night_from` / `night_to` jako `"HH:MM"`, `night_brightness`, `night_wake_s`,
  `night_pause_polling`). `format_version` zostaje 1 (brakujące klucze = bez zmian, tak jak
  dziś); stare kopie wczytują się bez błędu, nowe na starym firmware — nieznana grupa jest
  pomijana (sprawdzić w teście, że restore nie odrzuca pliku z nieznaną grupą).

### 5.6 Strona ustawień (`config_page`, `config_portal`, `web_i18n`)

- `config_page`: sekcja `SectionDisplay` na zakładce Ogólne (przed „Ekrany”, bo dotyczy tego
  samego), pola z §4.4; pola zależne od trybu ukrywane / wyszarzane tym samym prostym JS co
  dziś przy zakładkach. Pola czasu jako `<input type="time">`, w modelu minuty.
- Szacunek kredytów na dobę (zakładka OpenSky): nowa czysta funkcja
  `estimateDailyRequests(pollIntervalS, pausedMinutesPerDay)`; minuty wstrzymania liczone
  z okna nocnego (godziny albo dzisiejszy zachód→wschód), **tylko gdy
  `night_pause_polling`** (bez czasu wybudzeń — tych nie da się przewidzieć). Testy: 15 s,
  okno 23:00–06:30, wstrzymanie → 5760 − 1800 = 3960; bez wstrzymania → 5760.
- `GET /light.json` → `{"raw":412,"avg":398,"pct":35,"pwm":61,"night":false,"woken":false,
  "screen_off":false,"polling":true,"synced":true,"sunrise":"07:17","sunset":"17:23"}`; za
  `authorize()` jak każda ścieżka. Serializacja czysta (`lightStatusJson()`), testowana.
- `web_i18n`: nowe teksty (PL/EN, bez `" < > \ &`, `test_web_i18n` to pilnuje).

### 5.7 Wyświetlacz

Nic nowego do narysowania w v1 — przyciemnienie to podświetlenie, nie treść. Wskaźnika „tryb
nocny” / „pobieranie wstrzymane” w nagłówku nie dodajemy (przy 0 % go nie widać; przy
wstrzymaniu z zapalonym ekranem — do rozważenia po teście na płytce, §10).

### 5.8 Zrzuty ekranu z testów (`test_config_page`, `tools/screenshots.py`)

- `test_config_page`: przypadek zrzutowy (ten sam co dla Firmware, tylko przy
  `SCREENSHOT_OUT`) renderuje dodatkowo **zakładkę Ogólne** (`web_general.html`) i **zakładkę
  OpenSky** (`web_opensky.html`) z danymi demo: tryb automatyczny, noc od zachodu do wschodu
  (10 %), wstrzymanie **włączone** (żeby na OpenSky było widać szacunek z nocą), podgląd
  „Teraz” wypełniony stałymi wartościami (w HTML-u od razu, nie z `/light.json`).
- `tools/screenshots.py`: robi z nich `docs/screenshots/web_general.png` i `web_opensky.png`
  (500 px × 2, jak pozostałe `web_*.png`); docstring opisuje nowe pliki. Od teraz wszystkie
  zrzuty panelu (poza Lokalizacją i Kopią, jeśli nie dojdą przy okazji) da się odtworzyć bez
  płytki.
- Obejrzeć oba PNG (PL) przed commitem; zaktualizować `width`/`height` w `site/index.html`,
  jeśli zmieniła się wysokość strony.
- **Wyświetlacz:** nowa funkcja nie zmienia treści ekranów, więc `flights.png` / `now.png` /
  `radar.png` zostają. Opcjonalnie zdjęcie płytki w nocy (przyciemniona) do sekcji na stronie
  — jeśli powstanie, `site/img/`, ≤ ~300 KB.

### 5.9 Strona projektu (`site/index.html`, README)

- Nowa krótka sekcja (lub karta w istniejącej sekcji funkcji) PL/EN, w tonie reszty strony:
  ekran sam się przyciemnia przy lampce, o zachodzie słońca przechodzi w tryb nocny, a kto
  chce oszczędzać kredyty — może w nocy wstrzymać pobieranie (dotknięcie = świeże dane).
  Każdy tekst jako para `.en` / `.pl`.
- Galeria panelu: nowe `web_general.png` / `web_opensky.png`, poprawione `alt` (wspomina
  „jasność ekranu i tryb nocny”).
- `python tools/build_site.py` i przegląd `_site/` (PL, EN, 360 px).
- README: ustawienia jasności i nocy, wpływ na kredyty, pobór prądu z kroku 1.

## 6. Endpointy

| Metoda | Ścieżka | Odpowiedź |
|---|---|---|
| `GET` | `/light.json` | 200 JSON (§5.6); 401 / 429 jak reszta przy haśle |
| `POST` | `/save` (istniejący) | nowe pola z §5.5; zapis od razu stosowany (`configPortalSaveCount()`) |

## 7. Plan prac (kolejność, każdy krok z testami `pio test -e native`)

0. ROADMAP: pozycja #1 z „Planned” do „In progress”.
1. **Pomiar na płytce** (szkic, bez kodu produkcyjnego, ~1 h), wynik do §10:
   - odczyt GPIO34 przy tłumieniu 11 dB i 0 dB (`analogRead`, `analogReadMilliVolts`):
     słońce przy oknie, pokój dzienny, lampka, ciemność, zasłonięty palcem;
   - wpływ podświetlenia na czujnik: ciemny pokój, PWM 255 vs 10 vs 0;
   - szum: 100 próbek co 10 ms przy stałym świetle (czy mediana z 5 wystarcza);
   - najniższe PWM, przy którym ekran jeszcze świeci (`kMinVisiblePwm`), i czy PWM 44,1 kHz
     nie piszczy / nie miga przy małym wypełnieniu (ewentualnie inna częstotliwość);
   - porównanie: dzisiejsze 127 vs 255 — jak bardzo różni się w dzień;
   - pobór prądu (USB-miernik) przy 255 / 127 / 0 — do README.
   Z tego: `kLightRawBright`, `kLightRawDark`, tłumienie, `kMinVisiblePwm`.
2. `sun_times` — testy, potem implementacja.
3. `backlight` czysta część — testy (filtr, krzywa w obu kierunkach, histereza, gamma, rampa,
   okno nocne przez północ, wszystkie reguły `decideBacklight`, wybudzenie z różnym
   `wakeS` i przedłużaniem, „zjadanie” dotyku tylko przy wygaszonym ekranie, przepełnienie
   `millis()`, `shouldPollOpenSky` z wstrzymaniem i bez), potem implementacja.
4. `config_store` / `config_backup`: pola, sanitizacja, kopia (+ test nieznanej grupy).
5. Adapter `backlight` + `main.cpp` (start na 255, dotyk, polling, auto-cycle, pytanie
   o aktualizację, ekrany serwisowe).
6. `web_i18n` + `config_page` (sekcja, szacunek kredytów) + `config_portal` (`/light.json`).
7. Test na płytce (§9), wyniki do §10.
8. **Zrzuty ekranu** (§5.8): przypadki w `test_config_page`, `tools/screenshots.py`;
   wygenerować `web_general.png` i `web_opensky.png`, obejrzeć.
9. **Strona projektu** (§5.9): sekcja PL/EN, galeria, `python tools/build_site.py`,
   przegląd `_site/` (PL, EN, 360 px).
10. Dokumentacja: CLAUDE.md (sekcja „Brightness & night mode” + `backlight`, `sun_times`
    w „Code conventions”, uwaga przy „Rate limits” o wstrzymaniu w nocy, nowe zrzuty
    w „Commands”), README, ROADMAP → Done.

## 8. Decyzje właściciela (2026-10-24)

1. **Domyślnie jasność automatyczna** (z czujnika). Gdyby krok 1 pokazał, że czujnik nie daje
   użytecznego zakresu — wracamy z tym do właściciela, zanim zmienimy domyślną.
2. **Tryb nocny domyślnie „od zachodu do wschodu słońca”, jasność nocna 10 %.** Ekran nie
   gaśnie sam bez ustawienia — 0 % trzeba wybrać świadomie.
3. **Wstrzymanie pobierania danych w nocy — opcja w panelu, domyślnie wyłączona.** Włączona
   działa w całym oknie nocnym (niezależnie od jasności nocnej); dotknięcie wznawia
   pobieranie na czas wybudzenia.
4. **Wybudzenie przez samolot nad domem — nie w v1.**
5. **Progi czujnika jako stałe z pomiaru**, nie pola w panelu. Przy innych wariantach CYD
   (ROADMAP #3) — stałe per płytka (`#if` per env), nadal nie ustawienie użytkownika.
6. **Czas wybudzenia ustawiany w panelu** (`night_wake_s`, domyślnie 60 s, 10–600 s).
7. **W ramach tej pracy:** zrzuty ekranu z testów (§5.8) i aktualizacja strony projektu
   (§5.9).

## 9. Kryteria akceptacji (test ręczny na płytce)

- [x] Po starcie ekran jest jaśniejszy niż dziś (255 zamiast 127), po ~2 s schodzi do poziomu
      z czujnika.
- [x] Zasłonięcie czujnika (płytka pod ręcznikiem) → ekran płynnie ciemnieje do minimum w ~2 s;
      odsłonięcie → wraca w ~4 s. Brak migania i „schodków” (potwierdzone okiem).
- [ ] Przy stałym świetle jasność się nie zmienia przez 10 min (log `[light]` milczy).
- [x] Tryb stały: zmiana ustawień od razu zmienia jasność (42 % → PWM 52).
- [x] Świeże ustawienia domyślne: po zachodzie słońca ekran na 10 %, dane dalej się
      odświeżają; godziny 07:17 / 17:25 vs 07:17 / 17:23 z api.sunrise-sunset.org (±3 min).
- [x] Jasność nocna 0 %, wstrzymanie włączone → ekran gaśnie, log `polling paused (night)`,
      przez 4 min ani jednego `[main] poll`.
- [x] Dotknięcie wygaszonego ekranu → zapala się, **zakładka się nie zmienia**, polling wraca
      (od razu poll); po ustawionych 20 s ekran gaśnie, polling staje.
- [ ] Wstrzymanie przy jasności nocnej 10 %: dotknięcie działa normalnie i wznawia dane
      (tylko test natywny).
- [x] Wstrzymanie wyłączone (domyślne): w nocy polling bez przerw.
- [x] Koniec okna → ekran wraca, polling rusza sam (przestawienie okna na 01:00–02:00).
- [x] Okno przez północ (23:00–06:30) i bez przejścia przez północ działają (test natywny +
      01:00–02:00 na płytce).
- [ ] Bez synchronizacji czasu tryb nocny nieaktywny (tylko test natywny).
- [ ] OTA w nocy przy wygaszonym ekranie → ekran OTA na pełnej jasności (kod +
      `decideBacklight` w teście; bez próby na płytce).
- [ ] Pytanie o aktualizację nie pojawia się na wygaszonym ekranie (tylko test natywny).
- [ ] `/light.json` z hasłem → 401 bez logowania (płytka nie ma hasła; trasa woła
      `authorize()` jak każda inna). Bez hasła → 200 z poprawnym JSON-em.
- [x] Kopia zapasowa zawiera grupę `display`; restore ją ustawia (użyte do wszystkich testów
      na płytce); stara kopia (bez grupy) wczytuje się bez błędu (test natywny).
- [x] Szacunek kredytów na zakładce OpenSky uwzględnia wstrzymanie w nocy (i tylko wtedy).
- [x] `web_general.png` i `web_opensky.png` z danych testowych; strona projektu ma nową kartę
      i odświeżoną galerię (PL, EN).
- [x] `pio test -e native` zielone, nowe moduły pokryte (`test_backlight` 23,
      `test_sun_times` 9, + config_store / backup / page / release_check).
- [x] Wolna sterta po pierwszym pollingu: 205 732 B (spec 01 notował ~213 KB przed
      sprawdzaniem GitHuba; nowy kod nie trzyma dużych buforów — porównywalna).

Nieodhaczone wymagają czasu albo człowieka przy płytce: 10 min stałego światła, naturalny
wschód słońca, OTA w nocy, prawdziwy telefon (headless Chrome ma minimum 500 px szerokości).

## 10. Wyniki implementacji (2026-10-24)

**Krok 1 — pomiar czujnika** (szkic: `analogRead` / `analogReadMilliVolts` GPIO34, każde
tłumienie × PWM 255 / 127 / 10 / 0, 100 próbek co 10 ms):

| Warunki | 0 dB | 2,5 dB | 6 dB | 11 dB |
|---|---|---|---|---|
| Oświetlony pokój, dowolne PWM | 0 | 0 | 0 | 0 |
| Zakryty palcem | 0 (szpilki do ~170) | 0–9 | 0 | 0 |
| Pod ręcznikiem, PWM 255 | ~780–1180 | ~620–840 | ~550 | **~263** |
| Pod ręcznikiem, PWM 127 | ~1680 | ~1290 | ~885 | **~447** |
| Pod ręcznikiem, PWM 10 | ~3600 | ~2865 | ~2055 | **~1095** |
| Pod ręcznikiem, PWM 0 | 4095 (nasycony) | ~3290 | ~2350 | **~1260** |

- Czujnik reaguje dopiero przy słabym świetle; w zwykle oświetlonym pokoju jest 0. Palec nie
  wystarcza (przepuszcza światło). **Wybrane 11 dB**: nie nasyca się w ciemności, cały zakres
  0–~1300.
- **Podświetlenie świeci w czujnik** (pod ręcznikiem odbija się od materiału): 255 → 263,
  0 → 1260. Bez kompensacji, bo sprzężenie działa w dobrą stronę: ciemno → ekran ciemnieje →
  odczyt rośnie → ekran schodzi do minimum. Na płytce: ze 100 % do 10 % w ~2 s, bez oscylacji.
- Progi: `kLightRawBright` = 40 (poniżej szpilek z jasnego pokoju po medianie),
  `kLightRawDark` = 1000 (PWM 10 w ciemności daje ~1100, więc minimum jest stabilne).
- Nie zmierzone: pobór prądu (brak miernika USB), najniższe widoczne PWM. `kMinVisiblePwm`
  = 8 to założenie (ekran wybudzony na 10 % = PWM 10 był widoczny — potwierdzone okiem).

**Wyniki na płytce** (CYD na COM3, 192.168.1.193, 23:12–23:21 czasu lokalnego):
- Start: `[light] raw=34 avg=34 -> 100% (pwm 255)`, po synchronizacji `[light] sun today:
  sunrise 07:17, sunset 17:25`, `[light] night (sun): 10%`, PWM 10.
- 0 % + wstrzymanie + 20 s (przez `/restore`): `-> 0% (pwm 0)`, `polling paused (night)`;
  23:12:59–23:17:00 zero zapytań do OpenSky. Dotyk o 23:17:00: `touch at night: awake for 20 s
  (screen lit)`, `polling resumed (touch)`, poll w tej samej sekundzie, zakładka bez zmian
  (potwierdzone okiem); 23:17:20 `-> 0%`, `polling paused (night)`.
- Okno 01:00–02:00 → `day (schedule): 100%`, `polling resumed`; stała 42 % → PWM 52.
- Tryb automatyczny pod ręcznikiem: 100 → 10 % w ~2 s (raw 750 → 1350), po odkryciu
  10 → 100 % w ~4 s; „płynnie, bez migania”.
- Po testach ustawienia przywrócone z kopii (porównanie JSON: identyczne).
- Firmware 1 427 053 B (72,6 % slotu), RAM 18,4 %.

**Odstępstwa od projektu:**
- `pctToPwm()`: `8 + 247·(pct/100)²` zamiast `255·(pct/100)^2.2` z minimum — przy 2,2 i
  minimum 8 dolne 20 % skali dawało prawie to samo PWM; kwadrat zostawia więcej rozróżnialnych
  poziomów na dole.
- Rozgrzewka filtra 20 próbek (2 s), nie 5 — w tym czasie ekran świeci na maksimum.
- Wybudzenie przy jasności nocnej > 0 nie zmienia jasności (dotyczy tylko pollingu) — tak jak
  w §4.3 p. 3, doprecyzowane w kodzie (`decideBacklight`).
- `/light.json` ma też pole `"sun"` (`normal` / `polar_day` / `polar_night` / `""`).
- Przy wygaszonym ekranie auto-cycle stoi, a wybudzenie zeruje jego licznik (żeby po
  dotknięciu ekran nie przeskoczył od razu).
- Zrzuty `web_general.png` / `web_opensky.png` przez `tools/screenshots.py`; Windows
  (WinError 4551) blokował plik `.exe` testu, pomogło przebudowanie z inną flagą
  (`PLATFORMIO_BUILD_FLAGS=-DSHOT_NONCE=…`).
