# Spec 10 — Bezpieczeństwo panelu WWW: CSRF, zmiany stanu przez GET, osadzanie w ramce

Z przeglądu kodu 2026-10-25, punkt 1.4. Status: **zrobione** 2026-10-26, wydane w v1.9.0.

## 1. Problem

Panel domyślnie nie ma hasła. Każda strona internetowa otwarta w przeglądarce w tej samej
sieci może wysłać formularz na `http://cyd-sky.local/...` (CSRF) i np.:

- `POST /save` — zmienić lokalizację, wyczyścić dane OpenSky, ustawić hasło panelu
  (właściciel traci dostęp);
- `POST /restore` — wgrać dowolne ustawienia;
- `POST /update/install` — zainstalować wydanie z GitHuba (tylko nasze, ale bez pytania).

Hasło Digest nie chroni: przeglądarka sama dołącza zapamiętane dane logowania do żądań
z innych stron. Do tego:

- `GET /lang` zmienia zapisany język panelu (zmiana stanu przez GET, działa z `<img src>`);
- `GET /screenshot.bmp?screen=N` przełącza ekran urządzenia **i zapisuje go w NVS**
  (`onScreenChanged(userInitiated=true)` → `saveLastScreen()`), więc obca strona może w kółko
  przełączać ekrany i zużywać flash;
- panel można osadzić w `<iframe>` na obcej stronie (clickjacking przycisku „Zainstaluj”).

## 2. Projekt

### 2.1 Sprawdzanie pochodzenia żądań zmieniających stan (czysta funkcja w `web_auth`)

```cpp
enum class OriginCheck { SameOrigin, NoHeaders, CrossOrigin };
OriginCheck checkRequestOrigin(const std::string &origin, const std::string &referer,
                               const std::string &host);
```

- `Origin` obecny → `SameOrigin`, gdy jego host (bez schematu i portu 80) równa się `Host`
  żądania; inaczej `CrossOrigin`. `Origin: null` → `CrossOrigin`.
- Brak `Origin`, `Referer` obecny → to samo porównanie dla hosta z `Referer`.
- Brak obu → `NoHeaders` — **dozwolone**: tak wysyłają `curl` i `pio run -e cyd-ota -t upload`.
  Przeglądarki wysyłają `Origin` przy każdym POST z innej strony, więc to nie otwiera CSRF.
- `config_portal`: wszystkie trasy POST (`/save`, `/restore`, `/upload_credentials`,
  `/update`, `/update/check`, `/update/install`, `/update/prerelease`, `/lang`) wołają
  `requireSameOrigin()` **przed** `authorize()` i przed buforowaniem uploadu (w callbacku
  uploadu: `UPLOAD_FILE_START` przy `CrossOrigin` nic nie zapisuje). Odpowiedź: 403
  `Cross-site request refused`, log `[auth] cross-origin POST /save from <origin> refused`.
- `collectHeaders` rozszerzone o `Origin`, `Referer`, `Host`.

### 2.2 Bez zmian stanu przez GET

- `/lang` → `POST /lang` (`l=pl|en`, `tab=…`), przełącznik PL/EN w nagłówku strony to dwa
  małe formularze z przyciskami (wygląd bez zmian). `GET /lang` → 405.
- `/screenshot.bmp?screen=N` przełącza ekran tylko na czas zrzutu i **nie zapisuje** go
  w NVS: nowy parametr `onScreenChanged(…, persist=false)`; po zrzucie urządzenie wraca na
  ekran sprzed zrzutu. (`tools/screenshots.py` działa jak dziś.)

### 2.3 Nagłówki odpowiedzi

Każda strona HTML: `X-Frame-Options: DENY` i `Content-Security-Policy: frame-ancestors 'none'`.
Bez `Access-Control-Allow-Origin` nigdzie (dziś też go nie ma — test, żeby tak zostało).

## 3. Testy (`test_admin` po spec 07)

- `checkRequestOrigin`: ten sam host z i bez portu `:80`; `cyd-sky.local` vs adres IP
  (różne → `CrossOrigin`, bo przeglądarka wysyła to, co wpisano); `Origin: null`; tylko
  `Referer`; `Referer` z innej strony; brak obu; wielkość liter hosta; `https://` przy `Host`
  po HTTP (→ `CrossOrigin`).
- `config_page`: przełącznik języka to formularze POST, żaden link `href="/lang?`.
- `screenshot` / UI flow (po spec 12): zrzut z `screen=N` nie woła zapisu ekranu.

## 4. Kryteria akceptacji (płytka)

1. Strona testowa na innym hoście (np. `python -m http.server` na laptopie) z formularzem
   POST na `http://cyd-sky.local/save` → 403, ustawienia bez zmian, linia w logu.
2. Zapis ustawień, PL/EN, OTA z zakładki Oprogramowanie i `pio run -e cyd-ota -t upload`
   działają jak dziś.
3. `tools/screenshots.py --device …` → po zakończeniu urządzenie jest na ekranie sprzed zrzutu,
   a po restarcie wraca na ekran wybrany ręcznie, nie ostatni ze zrzutu.
4. Panel w `<iframe>` na obcej stronie się nie wyświetla.

## 5. Poza zakresem

- HTTPS na urządzeniu (certyfikat dla `cyd-sky.local` to osobny, duży temat).
- Tokeny CSRF w formularzach — sprawdzanie `Origin` wystarcza dla przeglądarek, które
  obsługują ten panel (wszystkie obecne).

## Wynik

Zrobione 2026-10-26 na gałęzi `spec/10-web-panel-security`. `pio test -e native`: wszystkie sześć
zestawów zielone; `pio run -e cyd`: 1 470 513 B (74,8 % slotu OTA, ok. +600 B);
`check_bdd.py` bez uwag. Scenariusze BDD: `10-web-panel-security.feature` (22, w tym 6 `@manual`).

- **§2.1 Pochodzenie żądań** — `checkRequestOrigin()` w `web_auth` (czysta): Origin rozstrzyga,
  bez niego host z Referer; tylko `http://`, dokładne porównanie host[:port] bez względu na
  wielkość liter, `:80` = brak portu. `CrossOrigin` także dla `null`, `https://`, userinfo
  (`cyd-sky.local@evil.example`), podszywającego się sufiksu (`cyd-sky.local.evil.com`), Origin
  ze ścieżką i pustego `Host`. `requireSameOrigin()` stoi przed `authorize()` we wszystkich
  ośmiu trasach POST; 403 `Cross-site request refused` + log
  `[auth] cross-origin POST /save from <origin> refused`. Callbacki uploadu decydują przy
  `UPLOAD_FILE_START` przez `decideUploadStart()` (najpierw pochodzenie, potem hasło) — nic nie
  jest buforowane, `OtaSession` nie startuje, na wyświetlaczu nie ma „aktualizacja nieudana”.
  `collectHeaders`: `Content-Type, Origin, Referer, Host`.
- **§2.2 Bez zmian stanu przez GET** — przełącznik PL/EN to dwa formularze `POST /lang`
  (ukryte `l` i `tab`, skrypt trzyma `tab` na bieżącej zakładce), wygląd bez zmian (zrzuty
  stron ustawień wyszły identyczne co do piksela); `GET /lang` → 405 + `Allow: POST`.
  `/screenshot.bmp?screen=N`: `ScreenChangeCause::Screenshot` nigdy nie zapisuje `last_screen`;
  `ScreenshotReturn` (`screenshot.h`) przywraca ekran sprzed zrzutu, gdy zrzut się kończy
  (`end()`, timeout albo 5 s bezczynności), raz, tylko po rzeczywistym przełączeniu.
- **§2.3 Nagłówki** — `X-Frame-Options: DENY` i `Content-Security-Policy: frame-ancestors 'none'`
  na **każdej** odpowiedzi panelu (opakowanie tras `secured<Handler>`, także 401 / 429 / 403,
  BMP, JSON), nie tylko na HTML; żadnego `Access-Control-*` (test).

**Odstępstwa od specyfikacji i decyzje:**
- Zamiast parametru `persist=false` w `onScreenChanged()` — przyczyna zmiany
  (`ScreenChangeCause`: `Touch` / `AutoCycle` / `Screenshot`); stara wersja z `bool` usunięta.
- Origin obecny, a `Host` pusty → `CrossOrigin`.
- Bez `X-Content-Type-Options` / `Referrer-Policy` (poza zakresem, decyzja właściciela).
- Teksty 403 / 405 po angielsku, bez tłumaczenia (jak istniejące 429).
- Powrót po zrzucie to zwykła zmiana ekranu: zeruje stronę tabeli i licznik auto-przełączania.
  Komunikat „!” zamknięty przez zrzut zwykłego ekranu nie otwiera się ponownie; po zrzucie
  ekranu demo (`screen=3` / `4`) urządzenie odświeża bieżący ekran, jak dotąd.

**Do sprawdzenia na płytce (`@manual`):** kryteria 1–4 — formularz i upload z obcej strony
(403, ustawienia bez zmian, brak ekranu OTA, linia w logu), normalna praca panelu z hasłem i bez
(zapis, PL/EN na tej samej zakładce, upload danych, przywracanie, OTA z zakładki i
`pio run -e cyd-ota -t upload`), `curl -i .../lang` → 405, `curl -si -o /dev/null -D - http://cyd-sky.local/` z nagłówkami, panel
w `<iframe>` zablokowany, `tools/screenshots.py --device` → ekran sprzed zrzutu, po restarcie
ekran wybrany ręcznie.
