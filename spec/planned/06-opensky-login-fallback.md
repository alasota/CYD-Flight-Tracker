# Spec 06 — Błąd logowania OpenSky: tryb anonimowy + komunikat w panelu

Z przeglądu kodu 2026-10-25, punkt 1.2. Status: **do zrobienia**.

## 1. Problem

[opensky_client.cpp](../../src/opensky_client.cpp) `fetchAircraftStates()`: gdy pobranie tokenu
OAuth się nie uda, kod **po cichu** przechodzi na zapytanie anonimowe. Anonimowe `/states/all`
zwraca 200, więc stan to `Ok`. Skutki:

- Użytkownik z literówką w `client_secret` nie dowiaduje się o tym nigdzie. Panel pokazuje
  „konto” (`hasOpenSkyCredentials()` sprawdza tylko, czy pola są niepuste), a po kilku
  godzinach ekran mówi „LIMIT OPENSKY WYCZERPANY” (limit anonimowy 400 zamiast 4000).
- Token nie jest ważny, więc `tokenNeedsRefresh()` zwraca true przy każdym pollu i co 15 s
  leci zbędne zapytanie do serwera logowania OpenSky.

## 2. Zachowanie docelowe (decyzja właściciela 2026-10-25)

**Złe dane logowania → urządzenie działa dalej w trybie anonimowym, a panel administracyjny
pokazuje, że logowanie się nie udało i że urządzenie pracuje anonimowo.**

- **Wyświetlacz:** samoloty widoczne, kropka stanu zielona (dane płyną), a w nagłówku
  **czerwony wykrzyknik** — ostrzeżenie `OpenSkyLoginFailed` z ogólnego mechanizmu
  [spec 08](08-device-alerts.md). (`OpenSkyHealth::AuthError` zostaje tylko dla sytuacji,
  w której nawet zapytanie anonimowe dostaje 401/403.)
- **Panel, strona główna:** nad zakładkami ramka z wykrzyknikiem i wyjaśnieniem, dlaczego się
  pojawił (spec 08 §3.4).
- **Panel, zakładka OpenSky:** zamiast zielonej pigułki „konto” czerwona pigułka i karta ze
  szczegółami (§3.4).
- Kolejność wdrożenia: najpierw spec 08 (mechanizm), potem ta specyfikacja jako jego pierwszy
  użytkownik.
- **Log:** jedna linia przy przejściu w ten stan, nie co poll.

## 3. Projekt

### 3.1 Czysty stan logowania (`src/opensky_auth.h/.cpp`, nowy moduł)

```cpp
enum class TokenOutcome { Ok, Rejected, Transient };
// Rejected  = serwer logowania odpowiedział 400 / 401 / 403 (zły client_id lub secret)
// Transient = brak odpowiedzi, timeout, 5xx, odpowiedź bez access_token

struct OpenSkyAuthState {
  enum class Mode : uint8_t { NoCredentials, Account, LoginFailed };
  Mode mode = Mode::NoCredentials;
  int lastHttpCode = 0;          // kod odpowiedzi serwera logowania przy błędzie (0 = brak odpowiedzi)
  uint32_t failedSinceMs = 0;    // millis() pierwszego odrzucenia w tej serii
  uint32_t nextTokenTryMs = 0;   // wcześniej nie próbować tokenu ponownie
  uint32_t credentialsHash = 0;  // dla jakich danych liczony jest ten stan
};

TokenOutcome classifyTokenResponse(int httpCode, bool hasAccessToken);
OpenSkyAuthState onCredentials(const OpenSkyAuthState &s, uint32_t credentialsHash, bool haveCredentials);
OpenSkyAuthState onTokenResult(const OpenSkyAuthState &s, TokenOutcome o, int httpCode, uint32_t nowMs);
bool shouldTryToken(const OpenSkyAuthState &s, uint32_t nowMs);  // wrap-safe
bool useBearer(const OpenSkyAuthState &s);  // Account + ważny token
uint32_t credentialsHash(const std::string &id, const std::string &secret);  // FNV-1a
```

Reguły:

| Zdarzenie | Nowy stan | Kolejna próba tokenu |
|---|---|---|
| brak `client_id`/`secret` | `NoCredentials` | nigdy |
| token OK | `Account` | normalnie (`tokenNeedsRefresh`, 60 s przed wygaśnięciem) |
| `Rejected` | `LoginFailed` | za **30 min** (`kLoginRetryMs`) |
| `Transient`, wcześniej `Account` | `Account` (ten poll anonimowo) | następny poll |
| `Transient`, wcześniej `LoginFailed` | `LoginFailed` | za 30 min |
| zmiana danych logowania (inny hash) | reset → `Account`/`NoCredentials` | od razu |

Dlaczego 30 min: zły secret nie naprawi się sam, a ponowna próba co pół godziny łapie
sytuację „OpenSky miał awarię serwera logowania i zwracał 401”. Zmiana danych na stronie
ustawień próbuje od razu.

### 3.2 `fetchAircraftStates()`

1. `auth = onCredentials(auth, credentialsHash(id, secret), shouldUseOAuth(id, secret))` —
   wykrywa zmianę danych po zapisie w panelu (Config jest przeładowywany w `net_task`).
2. Jeśli `shouldTryToken(auth, now)` i token wymaga odświeżenia → `fetchToken()`, który zwraca
   teraz `{TokenOutcome, httpCode}`, a stan aktualizuje `onTokenResult()`.
3. `/states/all` z Bearerem tylko gdy `useBearer(auth)`; inaczej anonimowo.
4. `/states/all` z Bearerem → 401 → odśwież token raz i powtórz (jak dziś). Gdy świeży token
   też dostaje 401/403 → `onTokenResult(Rejected, code)` i **ponów ten sam poll anonimowo**
   (jedno dodatkowe zapytanie, tylko przy przejściu w `LoginFailed`). `AuthError` tylko wtedy,
   gdy i zapytanie anonimowe dostanie 401/403.
5. Log przy zmianie `mode`, raz:
   `[opensky_client] login failed (http=401) — check client_id/secret; using anonymous access, next try in 30 min`
   oraz `[opensky_client] login ok — using account` po powrocie.

Nowa funkcja `openSkyAuthStatus()` zwraca kopię `OpenSkyAuthState`. Woła ją `config_portal`
(rdzeń 0, ten sam co `opensky_client` — bez synchronizacji, jak `openSkyQuota()`).

### 3.3 Licznik kredytów

W `LoginFailed` dzienny limit to limit anonimowy: zakładka OpenSky liczy szacunek
„kredyty/dzień vs limit” względem **400**, nie 4000 (`openSkyDailyLimit(authState)`).

### 3.4 Panel — zakładka OpenSky (`config_page`, `web_i18n`)

`ConfigPageModel` dostaje pole `auth` (`OpenSkyAuthState`, w `sendPage()` z
`openSkyAuthStatus()`). Pigułka u góry karty:

| Stan | Pigułka | Tekst PL / EN |
|---|---|---|
| `NoCredentials` | szara | „Tryb anonimowy” / „Anonymous” (jak dziś) |
| `Account` | zielona | „Konto OpenSky” / „OpenSky account” (jak dziś) |
| `LoginFailed` | czerwona (`.pill.err`) | „Błąd logowania — tryb anonimowy” / „Login failed — anonymous mode” |

W stanie `LoginFailed` pod pigułką ostrzeżenie (`.stat.warn`, styl jak przy wyczerpanym limicie):

> OpenSky odrzucił client_id / client_secret (HTTP 401, od 25.10.2026 14:32). Urządzenie działa
> w trybie anonimowym: limit 400 kredytów dziennie zamiast 4000. Sprawdź dane na stronie konta
> OpenSky i zapisz je ponownie. Następna próba logowania za 27 min.

Nowe wpisy `WEB_I18N_STRINGS`: `StatusLoginFailed`, `LoginFailedDetail` (z miejscami na kod,
czas i minuty), oba bez znaków zabronionych przez `test_web_i18n`. Czas „od” z
`failedSinceMs` przeliczony na zegar ścienny tylko gdy zegar jest zsynchronizowany, inaczej
pominięty.

## 4. Testy

`test_opensky` po spec 07, plik `opensky_auth_test.cpp`:

- `classifyTokenResponse`: 200 + token → Ok; 200 bez tokenu → Transient; 400/401/403 → Rejected;
  0 / -1 / 500 / 503 → Transient.
- Tabela przejść z §3.1, wiersz po wierszu, w tym: `Transient` nie przełącza `Account` →
  `LoginFailed`; zmiana hasha resetuje stan i pozwala próbować od razu; brak danych →
  `NoCredentials`, `shouldTryToken` false.
- `shouldTryToken` w `LoginFailed`: false przez 30 min, true po; wrap `millis()` (start tuż
  przed `UINT32_MAX`).
- `useBearer` false w `LoginFailed` i `NoCredentials`.
- `test_config_page`: każdy z trzech stanów renderuje swoją pigułkę; w `LoginFailed` jest
  ostrzeżenie z kodem HTTP; szacunek dzienny porównywany z 400; PL i EN.
- Sekwencja „token odrzucony → poll anonimowy” w `fetchAircraftStates` jest w `#ifdef ARDUINO`;
  logika decyzji (kroki 3–4) wyciągnięta do czystej `planStatesRequest(auth, tokenValid)` i
  `afterStatesResponse(auth, code, usedBearer)` → `{retryAnonymous, newAuth, health}` z testami
  na 200 / 401 z Bearerem / 401 anonimowo / 429.

## 5. Kryteria akceptacji (płytka)

1. Zły `client_secret` → w ciągu jednego pollu ekran pokazuje samoloty (zielona kropka)
   i czerwony „!” w nagłówku; panel ma ramkę ostrzeżenia nad zakładkami (spec 08), a zakładka
   OpenSky czerwoną pigułkę i ostrzeżenie z HTTP 401.
2. Log: jedna linia „login failed”, potem **brak** zapytań do serwera logowania przez 30 min.
3. Poprawne dane zapisane w panelu → następny poll używa konta, pigułka zielona, log „login ok”.
4. Bez danych logowania zachowanie identyczne jak dziś.
