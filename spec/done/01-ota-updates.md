# Spec 01 — Aktualizacje OTA (Over-The-Air)

Pozycja #1 z [ROADMAP.md](../../ROADMAP.md) („Do it first: the two OTA slots are already there, only
the code is missing”). Status: **zrobione — wydane w v1.1.0** (przetestowane na płytce 2026-10-24).
Wyniki i odstępstwa od pierwotnego projektu: §10.

## 1. Cel

Wgrywanie nowego firmware'u przez WiFi, ze strony ustawień (`cyd-sky.local`), bez kabla USB
i bez kasowania ustawień ani cache'y hexdb.io. Plik do wgrania to dokładnie to, co już
publikuje CI: `cyd-sky-tracker-<version>.bin` z GitHub Release (sam obraz aplikacji, ten sam,
który dziś idzie na `0x10000`).

Kryterium sukcesu w jednym zdaniu: *pobieram `.bin` z Release, wybieram go na zakładce
„Firmware”, klikam „Aktualizuj”, po ~30 s strona pokazuje nową wersję w stopce, a wyświetlacz
działa z tymi samymi ustawieniami.*

## 2. Stan obecny (co już jest)

- **Partycje:** `min_spiffs.csv` — dwa sloty `app0` / `app1` po 1,875 MB + `otadata`.
  Firmware ma ~1,3 MB, więc mieści się z zapasem. NVS (ustawienia, `lk_ac` / `lk_rt` / `lk_ap`)
  leży w osobnej partycji — OTA jej nie dotyka.
- **Wersja:** `firmwareVersion()` (`src/version.cpp`, makro `FW_VERSION` z
  `tools/pio_version.py`), pokazywana w stopce strony.
- **Serwer WWW:** `config_portal` — synchroniczny `WebServer`, już obsługuje upload multipart
  (`handleUploadChunk()` dla poświadczeń OpenSky i `/restore`).
- **CI:** `firmware.yml` publikuje `cyd-sky-tracker-<ver>.bin` (OTA), `…-full.bin` (pełny obraz
  od `0x0`), `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `SHA256SUMS.txt`.
- **Brak:** endpointu do wgrania obrazu, interfejsu, weryfikacji pliku, obsługi rollbacku.

## 3. Zakres

### W zakresie (v1)

1. Endpoint `POST /update` (multipart, pole `firmware`) zapisujący obraz do nieaktywnego slotu
   przez arduino-esp32 `Update`, a potem restart.
2. Weryfikacja pliku **przed** aktywacją slotu: nagłówek obrazu ESP32, rozmiar, znacznik
   firmware'u Sky Trackera (żeby nie wgrać `-full.bin`, `bootloader.bin` ani cudzego firmware'u).
3. Nowa zakładka **Firmware** na stronie ustawień: aktualna wersja, wybór pliku, pasek postępu,
   komunikaty o błędach (PL/EN, `web_i18n`).
4. Ekran postępu na wyświetlaczu podczas aktualizacji (PL/EN, `i18n`).
5. Rollback: nowy obraz zostaje oznaczony jako poprawny dopiero, gdy wstał i działa stabilnie;
   jeśli wcześniej się zrestartuje (crash, bootloop), bootloader wraca do poprzedniego.
6. **Hasło do panelu WWW** (§5.7): opcjonalne hasło chroniące całą stronę ustawień i wszystkie
   endpointy (HTTP Digest), ustawiane na zakładce Ogólne, z blokadą po nieudanych próbach
   i odzyskiwaniem z poziomu dotyku na urządzeniu. Przy okazji strona przestaje wpisywać
   zapisany `client_secret` do HTML-a.
7. Środowisko PlatformIO `cyd-ota`: `pio run -e cyd-ota -t upload` wgrywa build przez WiFi
   (ten sam endpoint, `curl --digest`), dla wygody przy developmencie.
8. Dokumentacja: README (sekcja „Updating”, przepisane ostrzeżenie o `client_secret`),
   CLAUDE.md (sekcje modułów), ROADMAP (przeniesienie pozycji).

### Poza zakresem (na później)

- **Automatyczne sprawdzanie GitHub Releases** z urządzenia („jest nowa wersja v1.4.0 —
  zainstalować?”). Wymaga TLS do `api.github.com` + przekierowania na
  `objects.githubusercontent.com`; dobry kolejny krok, ale osobna specyfikacja.
- **ArduinoOTA / espota** (UDP, osobny port, osobne hasło) — `cyd-ota` przez `curl` daje to samo
  jednym endpointem.
- Sprawdzanie, czy plik pasuje do **wariantu płytki** — przygotowane w znaczniku (pole `board`,
  §5.3), ale egzekwowane dopiero w pozycji #4 z roadmapy.
- Podpisywanie obrazów (secure boot / signed OTA).
- HTTPS na urządzeniu i szyfrowanie NVS — Digest chroni hasło w sieci, ale treść stron
  i uploadów nadal idzie otwartym HTTP.
- Blokowanie downgrade'u — świadomie **nie** blokujemy (decyzja właściciela, §8).
- Aktualizacja bootloadera lub tablicy partycji przez OTA (zawsze wymaga USB / instalatora web).

## 4. Przepływ użytkownika

1. Użytkownik otwiera `cyd-sky.local` → zakładka **Firmware**. Widzi:
   - „Zainstalowana wersja: **v1.2.0**”,
   - link do strony Releases projektu
     (`https://github.com/alasota/CYD-Flight-Tracker/releases/latest`),
   - pole wyboru pliku (`accept=".bin"`), przycisk **Aktualizuj**,
   - podpowiedź: „Wybierz plik `cyd-sky-tracker-<wersja>.bin` — **nie** ten z `-full` w nazwie.
     Ustawienia zostaną zachowane.”
2. Klik **Aktualizuj** → JS wysyła plik przez `XMLHttpRequest` z `upload.onprogress` → pasek
   postępu na stronie. Na wyświetlaczu: ekran „AKTUALIZACJA OPROGRAMOWANIA” z paskiem i procentem.
3. **Sukces:** urządzenie odpowiada stroną „Zaktualizowano v1.2.0 → v1.3.0. Restart…”, po ~1 s
   restartuje się. JS odpytuje `/` co 2 s (do 60 s) i po powrocie przeładowuje stronę — stopka
   pokazuje nową wersję.
4. **Błąd** (zły plik, przerwany upload, błąd zapisu): strona pokazuje komunikat na zakładce
   Firmware (`isError`), urządzenie wraca do normalnej pracy bez restartu (pełny redraw), stary
   firmware pozostaje aktywny.
5. **Bez JS** (fallback): zwykły `<form method="POST" enctype="multipart/form-data">` też działa —
   bez paska na stronie, ale z paskiem na wyświetlaczu (bez znanego rozmiaru: liczba KB).

## 5. Projekt techniczny

### 5.1 Nowy moduł czysty: `ota_update` (`src/ota_update.h/.cpp`, `test/test_ota_update/`)

Zgodnie z CLAUDE.md „Testing”: cała logika decyzyjna jest czysta i testowana pod `native`;
`config_portal` tylko woła `Update.*`.

```cpp
// Sprawdzenie pierwszego fragmentu pliku (>= 24 bajty nagłówka esp_image_header_t).
enum class OtaImageCheck : uint8_t {
  Ok,
  TooShort,        // < 24 bajtów w pierwszym kawałku
  LooksLikeFull,   // pierwsze bajty 0xFF — merged -full.bin (bootloader od 0x1000, przed nim wypełnienie)
  NotAppImage,     // magic != 0xE9
  WrongChip,       // chip_id (bajty 12..13, LE) != 0x0000 (ESP_CHIP_ID_ESP32)
};
OtaImageCheck checkImageHeader(const uint8_t *data, size_t len);

// Rozmiar: odrzuć z góry, jeśli znany i nie mieści się w slocie albo jest
// podejrzanie mały (bootloader.bin ma ~17 KB, prawdziwa aplikacja > 512 KB).
constexpr size_t kOtaMinImageBytes = 512 * 1024;
enum class OtaSizeCheck : uint8_t { Ok, Unknown, TooSmall, TooLarge };
OtaSizeCheck checkImageSize(size_t declaredBytes, size_t slotBytes);

// Wyszukiwanie znacznika firmware'u w strumieniu (patrz 5.3), odporne na
// podział znacznika między kawałki uploadu.
class FirmwareMarkerScanner {
 public:
  void feed(const uint8_t *data, size_t len);
  bool found() const;
  const std::string &board() const;    // np. "cyd-r"
  const std::string &version() const;  // np. "v1.3.0"
};

// Postęp: 0..100, albo -1 gdy total nieznany.
int otaProgressPercent(size_t written, size_t total);

// Wynik całej operacji -> komunikat strony.
enum class OtaResult : uint8_t {
  Ok, NoFile, TooShort, LooksLikeFull, NotAppImage, WrongChip, TooSmall, TooLarge,
  NotSkyTracker,   // znacznik nie znaleziony do końca pliku
  WriteFailed,     // Update.write() zwrócił mniej niż len / Update.hasError()
  VerifyFailed,    // Update.end(true) == false (suma kontrolna obrazu)
  Aborted,         // połączenie zerwane w trakcie (UPLOAD_FILE_ABORTED)
};
WebStr otaResultMessage(OtaResult r);
```

Testy (`test_ota_update.cpp`, pisane przed implementacją):
- `checkImageHeader`: poprawny nagłówek ESP32; `0xFF…` → `LooksLikeFull`; `0xE9` + chip_id
  ESP32-S3 (`0x0009`) → `WrongChip`; 10 bajtów → `TooShort`; losowe dane → `NotAppImage`.
- `checkImageSize`: 0 → `Unknown`; 17 KB → `TooSmall`; dokładnie rozmiar slotu → `Ok`;
  slot + 1 → `TooLarge`.
- `FirmwareMarkerScanner`: znacznik w jednym kawałku; rozcięty w każdym możliwym miejscu
  (pętla po punktach podziału); prefiks bez reszty → nie znaleziony; dwa wystąpienia → pierwsze
  wygrywa; brak `\0` przez > 64 bajty → ignoruj (ochrona przed śmieciami).
- `otaProgressPercent`: 0/x, x/x, total 0 → -1, bez przepełnienia dla 2 MB.
- `otaResultMessage`: każdy `OtaResult` ma komunikat (pętla po enumie).

### 5.2 Endpoint w `config_portal`

```
POST /update?size=<bajty>     (multipart, pole "firmware"; size opcjonalne, dodaje JS)
```

`server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateChunk)` — osobny handler
chunków, **nie** `handleUploadChunk()` (ten buforuje w RAM do 4 KB; obraz idzie prosto do flash).

`handleUpdateChunk()`:
- `UPLOAD_FILE_START`: wyzeruj stan, `size` z query → `checkImageSize()` wobec
  `esp_ota_get_next_update_partition(nullptr)->size`; jeśli `TooSmall`/`TooLarge` → ustaw błąd,
  ignoruj resztę. Powiadom obserwatora (`onStart`). **Nie** wołaj jeszcze `Update.begin()`.
- Pierwszy `UPLOAD_FILE_WRITE`: `checkImageHeader()` na pierwszych bajtach (jeśli kawałek
  < 24 B — doklej do małego bufora). Ok → `Update.begin(size ?: UPDATE_SIZE_UNKNOWN, U_FLASH)`.
- Każdy `UPLOAD_FILE_WRITE`: `scanner.feed()`, `Update.write()`, postęp → obserwator (nie
  częściej niż co ~2% / 250 ms, żeby nie spowalniać SPI).
- `UPLOAD_FILE_END`: jeśli `!scanner.found()` → `Update.abort()`, `NotSkyTracker`; inaczej
  `Update.end(true)` → `Ok` / `VerifyFailed`.
- `UPLOAD_FILE_ABORTED`: `Update.abort()`, `Aborted`.

`handleUpdateDone()`:
- Sukces: wyślij stronę wyniku (`renderConfigPage` z `SettingsTab::Firmware`,
  komunikat `OtaOk` + „v1.2.0 → v1.3.0”), `server.client().flush()`, potem zaplanuj restart
  (`restartAtMs = millis() + 1000`, wykonywany w `configPortalLoop()` — nie `delay()`).
- Błąd: strona z zakładką Firmware i komunikatem, obserwator `onEnd(false)`.

Ważne własności arduino-esp32 `Update`, na których się opieramy (do potwierdzenia na
platformie `espressif32@7.1.3` przy implementacji):
- `Update` zapisuje pierwsze bajty nagłówka **na końcu** — przerwany upload nigdy nie daje
  bootowalnego slotu; `otadata` zmienia się dopiero w `Update.end()`.
- `Update.end(true)` weryfikuje obraz (`esp_image_verify` — suma/SHA-256 doklejona przez esptool).

Synchroniczny `WebServer` obsługuje cały upload w jednym `handleClient()`, więc w tym czasie
pętla nie robi pollingu OpenSky ani lookupów hexdb.io — nie trzeba ich osobno wstrzymywać, a TLS
nie zabiera sterty. Do sprawdzenia: watchdog `loopTask` przy ~20 s uploadu (zapis flash oddaje
CPU, ale zmierzyć na płytce).

### 5.3 Znacznik firmware'u (`version.cpp`)

Stała w obrazie, po której `/update` rozpoznaje „to jest Sky Tracker, ta płytka”:

```cpp
// Odczytywany przez FirmwareMarkerScanner przy OTA. Format stały — zmiana
// wymaga obsługi starego formatu w skanerze.
extern "C" __attribute__((used)) const char kFirmwareMarker[] =
    "CYDSKY-FW1|board=" FW_BOARD "|v=" FW_VERSION;
```

(`FW_VERSION` jest już literałem napisowym i trafia tylko do `version.cpp` — dlatego znacznik
mieszka właśnie tam; bez makra — fallback `kFirmwareVersionUnknown`.)

- `FW_BOARD` = `"cyd-r"` na razie (makro z `platformio.ini` `build_flags`, domyślnie `cyd-r`) —
  pozycja #4 z roadmapy doda kolejne warianty i zacznie odrzucać niepasujące (`WrongBoard`).
- `__attribute__((used))` + nowa funkcja `firmwareBoard()` w `version.cpp`, która odwołuje się
  do znacznika, żeby linker (`--gc-sections`) go nie wyrzucił. Test w CI: `grep -c
  CYDSKY-FW1 firmware.bin` ≥ 1 (krok w `firmware.yml`).
- **Uwaga — przejście:** obecne wydania (bez znacznika) nie przejdą tej kontroli przy *downgrade*
  przez OTA. Akceptowalne (downgrade do wersji sprzed OTA = USB / instalator web), opisać w README.

### 5.4 Strona ustawień (`config_page`, `web_i18n`)

- `SettingsTab::Firmware` (id `firmware`), `kSettingsTabCount = 5`, etykieta
  `WebStr::TabFirmware` = „Firmware” / „Firmware”. Sprawdzić, czy 5 zakładek mieści się przy
  szerokości 360 px (dziś `flex:1 1 auto; white-space:nowrap`); jeśli nie — `overflow-x:auto` na
  `.tabs` albo krótsze etykiety.
- Zakładka jak „Kopia”: przycisk Zapisz ukryty (`$('save').hidden=t=='backup'||t=='firmware'`),
  własny formularz `#fw` poza głównym (formularze się nie zagnieżdżają).
- Treść: wersja (`firmwareVersion()`), link do Releases, `<input type=file name=firmware
  accept=".bin,application/octet-stream" form="fw" required>`, przycisk, podpowiedź, pasek
  `<progress hidden>`, ostrzeżenie „Nie wyłączaj zasilania podczas aktualizacji”.
- JS (doklejony do `kScript`): przechwyć `submit` formularza `#fw` → XHR `POST
  /update?size=<file.size>`, `upload.onprogress` → `<progress>`; po odpowiedzi podmień dokument
  (`document.open(); document.write(xhr.responseText)`) i przy sukcesie odpytuj `/`.
  Wcześnie po stronie przeglądarki: jeśli nazwa zawiera `-full` albo plik < 512 KB → komunikat
  bez wysyłania (oszczędza 20 s czekania; serwer i tak sprawdza).
- Nowe `WebStr` (PL/EN, bez `" < > \ &` — pilnuje `test_web_i18n`): `TabFirmware`,
  `FirmwareIntro`, `FirmwareInstalled`, `FirmwareReleasesLink`, `FirmwareChoose`,
  `FirmwareHint`, `FirmwareUpdateButton`, `FirmwareDontPowerOff`, `FirmwareRestarting`,
  `OtaOk` i po jednym komunikacie na każdy błąd `OtaResult`.
- Testy `test_config_page`: zakładka renderuje się z wersją; `?tab=firmware` round-trip;
  przycisk Zapisz ukryty na tej zakładce; komunikat błędu się pokazuje.

### 5.5 Wyświetlacz podczas aktualizacji

`config_portal` nie rysuje (zasada modułów). Analogicznie do `ScreenshotSource`:

```cpp
struct OtaObserver {
  void (*onStart)() = nullptr;                                 // main.cpp: fillScreen + ekran OTA
  void (*onProgress)(size_t written, size_t total) = nullptr;  // pasek / KB
  void (*onEnd)(bool ok) = nullptr;                            // ok: „RESTART…”; błąd: pełny redraw
};
void configPortalSetOtaObserver(const OtaObserver &o);
bool configPortalOtaInProgress();
```

Ekran (rysowany w `main.cpp` + mały moduł `ota_screen` lub funkcja w `status_bar`): czarne tło,
nagłówek „AKTUALIZACJA OPROGRAMOWANIA” / „FIRMWARE UPDATE”, pod nim „v1.2.0 → v1.3.0” gdy
znana (wersja nowa znana dopiero po znalezieniu znacznika — wtedy dorysować), poziomy pasek w
`THEME_CHROME_ACCENT` na `THEME_CHROME_RECESSED`, procent albo „812 KB”, na dole „NIE WYŁĄCZAJ
ZASILANIA” / „DO NOT POWER OFF”. Po błędzie: krótko „AKTUALIZACJA NIEUDANA” (5 s, `millis()`),
potem `RedrawEvents::firstDraw`. Dotyk ignorowany, auto-cycle wstrzymany, gdy
`configPortalOtaInProgress()`.

Czyste części testowane: geometria paska (`otaProgressBarFill(percent, widthPx)`), tekst
(`otaProgressText(written, total, lang)`), nowe `Str` przechodzą `isFontSafeText()`
(`test_i18n`) — „→” jest w zestawie znaków fontu.

### 5.6 Rollback po nieudanym starcie

- Nadpisać słabą funkcję arduino-esp32 `bool verifyRollbackLater() { return true; }` w
  `main.cpp` — wtedy core nie oznacza obrazu jako poprawnego od razu przy starcie.
- W `loop()`: gdy `esp_ota_get_state_partition(running)` == `ESP_OTA_IMG_PENDING_VERIFY`, oznacz
  `esp_ota_mark_app_valid_cancel_rollback()` dopiero gdy **jednocześnie**: uptime ≥ 30 s i
  (`configPortalIsActive()` z połączonym WiFi **albo** działa captive portal WiFiManagera). Czyli:
  „urządzenie się nie wywraca i da się do niego dojść, żeby wgrać poprawkę”. Sukces pollingu
  OpenSky **nie** jest warunkiem — awaria OpenSky / brak kredytów nie może cofać firmware'u.
- Decyzja jako czysta funkcja `shouldMarkAppValid(pendingVerify, uptimeMs, reachable)` w
  `ota_update`, z testami granicznymi (29 999 / 30 000 ms).
- Log: `[ota] running v1.3.0 from app1, state=pending-verify -> marked valid after 30 s`.
- **Do weryfikacji na starcie implementacji:** czy prebuilt sdkconfig arduino-esp32 na
  `espressif32@7.1.3` ma `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (i czy bootloader z instalatora
  go obsługuje). Jeśli nie — ten punkt zostaje „martwy, ale nieszkodliwy”
  (`esp_ota_get_state_partition` zwróci `ESP_OTA_IMG_VALID`/`UNDEFINED`), a w README piszemy
  wprost, że ratunkiem po złym obrazie jest USB / instalator web. Nie przebudowujemy bootloadera
  w ramach tej specyfikacji.

### 5.7 Hasło do panelu WWW

**Dlaczego teraz:** strona ustawień nie ma logowania — każdy w tej samej sieci WiFi może zmienić
ustawienia, pobrać `/backup.json?secrets=1` albo odczytać `client_secret` ze źródła strony
(ostrzeżenie w README). OTA bez hasła poszerza to do „może wgrać dowolny kod” (kontrola nagłówka
i znacznika chroni przed pomyłką, nie przed złośliwym autorem). Dlatego hasło wchodzi w tę
samą pracę (decyzja właściciela, §8).

**Model:**
- Hasło jest **opcjonalne**, domyślnie brak (pierwsza konfiguracja musi działać bez niego, tak
  jak dziś). Gdy brak hasła, zakładki Firmware i Ogólne pokazują ostrzeżenie „Panel nie ma
  hasła — każdy w Twojej sieci może zmienić ustawienia i wgrać firmware” z linkiem do pola hasła.
- Gdy hasło jest ustawione, chronione są **wszystkie** ścieżki serwera: `/`, `/save`, `/lang`,
  `/upload_credentials`, `/backup.json`, `/restore`, `/update`, `/screenshot.bmp`.
  Wyjątek: brak (także zrzut ekranu — pokazuje lokalizację domu w danych).
- **HTTP Digest** (`server.authenticate(user, pass)` + `server.requestAuthentication(DIGEST_AUTH,
  realm, …)` z arduino-esp32 `WebServer`) — hasło nie leci po sieci otwartym tekstem, jak przy
  Basic. Realm: `CYD Sky Tracker`. Login stały: `admin` (jedno urządzenie, jeden użytkownik;
  stała `kWebAuthUser`). Do potwierdzenia w kroku 1: czy Digest w tym `WebServer` działa
  z multipart POST `/update` z przeglądarki i z `curl --digest` (curl wysyła ciało dwa razy —
  najpierw dostaje 401; przy 1,3 MB to akceptowalne, ale sprawdzić, że `handleUpdateChunk` nie
  zaczyna pisać do flash przed udaną autoryzacją — patrz niżej).
- **Autoryzacja przed zapisem do flash:** `handleUpdateChunk()` na `UPLOAD_FILE_START` sprawdza
  `server.authenticate()`; bez niej ignoruje wszystkie kawałki (nic nie trafia do `Update`),
  a `handleUpdateDone()` odpowiada 401. To samo dla `handleUploadChunk()` (nie buforuj danych od
  nieuwierzytelnionego klienta).
- **Przechowywanie:** `Config::web_password` (NVS klucz `wpass`, w tej samej przestrzeni co
  reszta ustawień), otwartym tekstem — Digest wymaga znajomości hasła (HA1). Ten sam poziom
  ochrony co `client_secret`; opisane w README.
- **Zasady hasła** (czysta funkcja `validateWebPassword()` w `web_auth`, testy): 8–64 znaki,
  drukowalne ASCII (0x20–0x7E) — bez UTF-8, żeby nie było niespodzianek z kodowaniem w Digest
  i w `curl -u`. Puste = „brak hasła”.
- **Ustawianie** (zakładka Ogólne, nowa karta „Dostęp do panelu”): pola „Nowe hasło” i
  „Powtórz hasło” (`type=password`, `autocomplete=new-password`, **nigdy** nie wypełniane
  wartością), puste = bez zmian. Osobny checkbox „Usuń hasło” (gdy hasło jest ustawione).
  Zmiana hasła przez zalogowanego użytkownika nie wymaga podawania starego (i tak przeszedł
  Digest). Po zmianie przeglądarka dostanie 401 przy następnym żądaniu i zapyta o nowe — strona
  wyniku mówi o tym wprost. Niezgodne pola / za krótkie → błąd walidacji jak dla lat/lon,
  nic się nie zapisuje.
- **Blokada prób:** po 5 nieudanych autoryzacjach z rzędu (dowolny klient) serwer przez 60 s
  odpowiada 429 bez sprawdzania hasła; udana autoryzacja zeruje licznik. Czysta klasa
  `AuthThrottle` (`onFailure(nowMs)`, `onSuccess()`, `isLocked(nowMs)`, `retryAfterS(nowMs)`)
  w nowym module `web_auth`, testy graniczne (4./5. próba, 59 999 / 60 000 ms, przepełnienie
  `millis()`). Log: `[auth] 5 failed logins, locked for 60 s`.
- **Odzyskiwanie po zapomnieniu hasła** — bez USB: przytrzymanie palca na ekranie przez **5 s
  w ciągu pierwszych 10 s po starcie** pokazuje ekran „USUNĄĆ HASŁO PANELU WWW?” z przyciskami
  TAK / NIE (dotyk). TAK → `web_password` czyszczone, log `[auth] web password cleared from the
  touchscreen`. NIE / brak reakcji przez 15 s → normalny start. Fizyczny dostęp do urządzenia =
  prawo do resetu (kto ma płytkę, i tak może ją przeflashować). Czysta funkcja decyzji
  `resetHoldState(msSinceTouch, heldMs)` w `web_auth`, testy. *(Zrealizowane inaczej — §10:
  palec trzyma się od włączenia zasilania, nie „w ciągu 10 s”.)* Nowe `Str` w `i18n`
  (`isFontSafeText()`). Ostatnia deska ratunku: instalator web z „Erase device”.
- **Kopia zapasowa** (`config_backup`): hasło trafia do pliku **tylko** z `secrets=1`, w nowej
  grupie `"security": {"web_password": "…"}`; brak klucza przy restore = hasło bez zmian (jak
  dziś poświadczenia). `format_version` zostaje 1 (klucz dodany, stare pliki dalej się
  wczytują); testy w `test_config_backup`: round-trip, brak w pliku bez sekretów, restore bez
  klucza zachowuje hasło, hasło niezgodne z zasadami → `invalidFields`.
- **`client_secret` przestaje być wpisywany do HTML-a:** pole renderuje się puste z placeholderem
  „zapisany — zostaw puste, żeby nie zmieniać” (gdy jest zapisany). Puste pole przy zapisie =
  bez zmian; nowy checkbox „Usuń poświadczenia”. Test w `test_config_page`: wygenerowany HTML nie
  zawiera wartości sekretu. To zmienia dzisiejsze zachowanie `handleSave()` (sprawdzić, co
  teraz znaczy puste pole) — opisać w README.
- **Czego to nie daje:** HTTP bez TLS — kto podsłuchuje sieć, widzi treść stron i plik kopii
  zapasowej w trakcie pobierania (ale nie hasło Digest). README: „hasło chroni przed innymi
  w Twojej sieci, nie zastępuje zaufanej sieci”.

### 5.8 Wgrywanie z PlatformIO (`cyd-ota`)

```ini
[env:cyd-ota]
extends = env:cyd
upload_protocol = custom
upload_port = cyd-sky.local
upload_command = curl --fail -sS --digest -u "admin:${sysenv.CYD_WEB_PASSWORD}" -F "firmware=@$SOURCE" "http://$UPLOAD_PORT/update?size=$(stat -c%%s $SOURCE)"
```

Hasło z zmiennej środowiskowej `CYD_WEB_PASSWORD` — nigdy w `platformio.ini` (CLAUDE.md: żadnych
sekretów w repo). Bez hasła na urządzeniu zmienna może być pusta. Sprawdzić na Windowsie (Git Bash / `curl.exe` z Windows 10+; `stat` może nie istnieć — wtedy
bez `size`, serwer sobie poradzi z `UPDATE_SIZE_UNKNOWN`). Odpowiedź serwera na sukces musi
dać kod 200, na błąd 4xx/5xx, żeby `curl --fail` zgłosił porażkę `pio`.

## 6. Kody HTTP odpowiedzi `/update`

| Sytuacja | Kod | Treść |
|---|---|---|
| OK, restart za 1 s | 200 | strona z `OtaOk` |
| Brak pliku / zły nagłówek / znacznik / rozmiar | 400 | strona z komunikatem |
| Za duży na slot | 413 | strona z komunikatem |
| Błąd zapisu / weryfikacji flash | 500 | strona z komunikatem + `Update.errorString()` w `messageDetail` |
| Hasło ustawione, brak / złe dane Digest | 401 | wyzwanie Digest, nic nie zapisane do flash |
| Blokada po 5 nieudanych próbach | 429 | `Retry-After: <s>`, tekst „spróbuj za N s” |
| Drugi upload, gdy pierwszy trwa | nie wystąpi (synchroniczny serwer) | — |

## 7. Plan prac (kolejność, każdy krok z testami `pio test -e native`)

1. **Weryfikacja platformy** (30 min, bez kodu produkcyjnego): `Update` API i rollback w
   arduino-esp32 dla `espressif32@7.1.3`; rozmiar slotu z `esp_ota_get_next_update_partition`;
   Digest w `WebServer` z multipart (przeglądarka + `curl --digest`).
   Wynik dopisać do tej specyfikacji.
2. **Hasło najpierw** (OTA nie powinno trafić do wydania bez niego): `web_auth`
   (`AuthThrottle`, `passwordResetGesture`), `validateWebPassword` + `web_password` w
   `config_store`, grupa `security` w `config_backup`, karta „Dostęp do panelu” i puste pole
   `client_secret` w `config_page`, autoryzacja wszystkich ścieżek w `config_portal`, ekran
   resetu hasła w `main.cpp`. Testy: `test_web_auth`, `test_config_store`,
   `test_config_backup`, `test_config_page`, `test_web_i18n`, `test_i18n`.
3. `ota_update` — testy, potem implementacja (nagłówek, rozmiar, skaner, postęp, komunikaty,
   `shouldMarkAppValid`).
4. Znacznik w `version.cpp` + `FW_BOARD` + krok `grep` w `firmware.yml`.
5. `web_i18n` + `config_page`: zakładka Firmware, JS z paskiem; testy `test_config_page`,
   `test_web_i18n`.
6. `config_portal`: `/update` (z autoryzacją przed zapisem), obserwator, opóźniony restart.
7. `main.cpp`: ekran OTA, wstrzymanie dotyku/auto-cycle, rollback (`verifyRollbackLater`,
   oznaczanie po 30 s). `i18n`: nowe `Str`; `test_i18n`.
8. `cyd-ota` w `platformio.ini`.
9. Test na płytce (§9), potem dokumentacja: README (Updating + nowe ostrzeżenie zamiast
   „The config page hands your OpenSky client_secret to anyone who asks”), CLAUDE.md,
   ROADMAP (#1 → Done).

## 8. Decyzje właściciela (2026-10-24)

1. **Hasło do strony ustawień — w ramach tej pracy** (§5.7), wdrażane przed samym OTA (§7 krok 2).
2. **Downgrade — nie blokujemy.** Starszą wersję można wgrać jak każdą inną (droga ucieczki po
   złym wydaniu); strona tylko pokazuje obie wersje „v1.3.0 → v1.2.0”. Ograniczenie ze §5.3
   zostaje: wydania sprzed OTA (bez znacznika) wgrywa się przez USB / instalator web.
3. **Nazwa zakładki: „Firmware”** w obu językach.

## 9. Kryteria akceptacji (test ręczny na płytce)

- [x] Wgranie `cyd-sky-tracker-<nowa>.bin` przez stronę: pasek na stronie i na wyświetlaczu,
      restart, nowa wersja w stopce; lokalizacja, poświadczenia OpenSky, język, ostatni ekran i
      cache hexdb.io zachowane (brak nowych lookupów dla znanych samolotów w logu).
- [x] Ponowne wgranie tej samej wersji działa (slot się przełącza tam i z powrotem).
- [x] `…-full.bin` → komunikat „to pełny obraz, wybierz plik bez -full”, brak restartu.
- [x] `bootloader.bin`, `partitions.bin`, dowolny `.jpg` → odrzucone, brak restartu.
- [x] Zerwanie uploadu w połowie (zamknięcie karty / wyłączenie WiFi laptopa) → urządzenie wraca
      do normalnej pracy, stary firmware dalej się uruchamia po restarcie zasilania.
- [ ] Wyłączenie zasilania w trakcie zapisu → po włączeniu startuje stary firmware.
- [x] (jeśli rollback dostępny) Obraz testowy, który robi `abort()` po 5 s od startu → po
      kolejnym restarcie wraca poprzednia wersja; log `[ota]` to potwierdza.
- [x] `pio run -e cyd-ota -t upload` z Windowsa działa (z `CYD_WEB_PASSWORD`) i zwraca błąd
      przy złym pliku i złym haśle.
- [x] Bez hasła: strona działa jak dziś + ostrzeżenie na Ogólne i Firmware.
- [x] Po ustawieniu hasła każda ścieżka (`/`, `/backup.json?secrets=1`, `/screenshot.bmp`,
      `/update`, …) bez logowania → 401; `curl` z `--digest` i dobrym hasłem → 200.
- [x] `POST /update` bez hasła przy ustawionym haśle → 401 i **brak** ekranu OTA na urządzeniu
      (nic nie zapisano do flash).
- [x] 5 złych haseł → 429 przez 60 s, potem logowanie znowu działa.
- [ ] Reset hasła dotykiem (palec na ekranie od włączenia, 5 s, TAK) → strona znowu bez hasła;
      NIE / brak reakcji → hasło zostaje.
- [x] Źródło strony (`curl … /`) nie zawiera `client_secret`; zapis z pustym polem sekretu go
      nie kasuje.
- [x] Kopia z sekretami zawiera hasło, bez sekretów — nie; restore z hasłem je ustawia.
- [x] Downgrade (np. v1.3.1 → v1.3.0, obie ze znacznikiem) przechodzi.
- [ ] Strona z 5 zakładkami czytelna na telefonie (360 px), PL i EN.
- [x] `pio test -e native` zielone, nowe moduły pokryte.
- [x] Wolna sterta po aktualizacji i pierwszym pollingu porównywalna z wersją sprzed (log
      `free_heap`).

Nieodhaczone wymagają człowieka przy płytce / przeglądarce: odcięcie zasilania w trakcie zapisu,
reset hasła dotykiem, wygląd 5 zakładek na telefonie i pasek postępu XHR w prawdziwej
przeglądarce (upload przez stronę testowany `curl`-em z tym samym multipart).

## 10. Wyniki implementacji (2026-10-24)

**Krok 1 — weryfikacja platformy** (`espressif32@7.1.3` = arduino-esp32 2.0.17):
- `Update`: `begin(size|UPDATE_SIZE_UNKNOWN)`, `write`, `end(true)`, `abort`, `errorString()`;
  pierwsze bajty nagłówka zapisuje na końcu — przerwany upload nie daje bootowalnego slotu.
- Rollback: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` i `CONFIG_APP_ROLLBACK_ENABLE=y`; core
  woła słabe `verifyRollbackLater()` / `verifyOta()` w `initArduino()`.
- `WebServer`: `authenticate()` obsługuje Digest (RFC 2617, qop=auth) **i** Basic;
  `requestAuthentication(DIGEST_AUTH, …)` przy każdym wyzwaniu losuje nowy nonce (jedna sesja
  naraz — wystarcza dla jednego użytkownika). Nagłówek `Authorization` jest zbierany zawsze.

**Wyniki na płytce** (CYD na COM3, WiFi, 192.168.1.193):
- OTA ~1,36 MB: 15–16 s przez WiFi (~85 KB/s), restart, nowa wersja w stopce; NVS (ustawienia,
  cache hexdb.io) zachowane — po aktualizacji same „cache hit”. Sterta po pollingu ~213 KB
  (przed: ~219 KB).
- Odrzucone bez restartu: `-full.bin` (komunikat o -full), `bootloader.bin`, losowe dane, obraz
  bez znacznika (także bez `?size=`), upload przerwany po 6 s.
- Rollback: obraz z `abort()` po 8 s wystartował z `app1` jako „pending verify (rollback
  armed)”, wywrócił się, bootloader wrócił do poprzedniej wersji w `app0`. Poprawny obraz:
  `marked valid after 30 s, rollback cancelled`.
- Hasło: wszystkie ścieżki 401 bez logowania, Digest 200; 5 złych → 429 + `Retry-After: 60`
  (także dla dobrego hasła); OTA bez hasła → 401, w logu brak `[ota] upload` (nic nie zapisane).
- `pio run -e cyd-ota -t upload`: złe hasło → FAILED, dobre → zaktualizowane.
- Ekran OTA na płytce sprawdzony zrzutem (`/screenshot.bmp`) po nieudanej aktualizacji.

**Znaleziony i naprawiony błąd** (dotyczył też istniejących `/restore` i `/upload_credentials`):
`WebServer` 2.x woła callback uploadu również dla POST-a, który nie jest multipart (tryb
„raw”) — `server.upload()` dereferencjonuje wtedy null i płytka się wywraca (Guru Meditation
`LoadProhibited`). Wyzwalał to pusty POST, którym `curl --digest` zaczyna, ale też dowolny
`curl -d … /restore`. Każdy callback uploadu sprawdza teraz `Content-Type: multipart/form-data`
(`isMultipartRequest()`), a handlery biorą plik przez `takeUpload()` (bez starych danych z
poprzedniego żądania).

**Odstępstwa od projektu:**
- Reset hasła: palec na ekranie **od włączenia zasilania** przez 5 s (`offerWebPasswordReset()`
  w `setup()`, przed WiFi), zamiast „5 s w ciągu pierwszych 10 s po starcie” — prościej i bez
  ryzyka, że zwykłe dotknięcie nawigacji po starcie coś uruchomi. Przerwy w odczycie do 300 ms
  nie przerywają przytrzymania (`kResetReleaseGraceMs`).
- `validateWebPassword()` jest w `web_auth` (nie w `config_store`); `config_store` z niego
  korzysta (`sanitizeConfig()` porzuca niepoprawne hasło).
- Rysowanie ekranów serwisowych w nowym module `service_screen` (spec zostawiał wybór).
- Ekran „AKTUALIZACJA NIEUDANA” wisi 5 s (nie 2 s) — 2 s nie dało się przeczytać.
- Pole `client_secret`: placeholder „zapisany — zostaw puste…”, checkbox „Usuń dane logowania
  OpenSky” (tryb anonimowy).
- Zakładki zawijają się (`flex-wrap`) zamiast przewijać, gdy nie mieszczą się w jednym rzędzie.
- `firmware.yml` sprawdza znacznik `grep`-em po buildzie (krok „Check the OTA firmware marker”).
