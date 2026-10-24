# Spec 02 — Sprawdzanie nowych wersji na GitHubie i instalacja z urządzenia

Kontynuacja [spec 01](01-ota-updates.md) (§3 „Poza zakresem”: *„Automatyczne sprawdzanie GitHub
Releases z urządzenia („jest nowa wersja v1.4.0 — zainstalować?”). Wymaga TLS do
`api.github.com` + przekierowania na `objects.githubusercontent.com`; dobry kolejny krok, ale
osobna specyfikacja.”*). Status: **zrobione i przetestowane na płytce 2026-10-24** (wyniki i
odstępstwa: §10). W [ROADMAP.md](../ROADMAP.md) jako pozycja #2 w „Done”.

## 1. Cel

Urządzenie samo dowiaduje się, że na GitHubie jest nowsze wydanie, mówi o tym (na stronie
ustawień i raz na wyświetlaczu) i po **jednym potwierdzeniu** pobiera
`cyd-sky-tracker-<tag>.bin` prosto z GitHub Release i instaluje go tą samą ścieżką co OTA ze
spec 01 (kontrola nagłówka, znacznika, rollback). Bez laptopa, bez pobierania pliku ręcznie.

Kryterium sukcesu w jednym zdaniu: *wydaję `v1.4.0` (tag + CI), najpóźniej w ciągu doby
wyświetlacz pyta „DOSTĘPNA WERSJA v1.4.0 — ZAINSTALOWAĆ?”, stukam TAK, po ~1 min urządzenie
działa na v1.4.0 z tymi samymi ustawieniami.*

**Nigdy bez pytania.** Urządzenie tylko *sprawdza* samo; instaluje wyłącznie po potwierdzeniu
(dotyk TAK na wyświetlaczu albo przycisk na stronie). Automatyczna instalacja jest poza
zakresem (§3).

**Dwa kanały:** **wydania** (zwykłe tagi `vX.Y.Z`) — widoczne wszędzie, pytanie na
wyświetlaczu; **wersje deweloperskie** (pre-release na GitHubie, tagi `vX.Y.Z-rc.N` /
`-beta.N` / `-dev.N`) — **tylko w panelu admina**, i tylko gdy admin zaznaczy „Pokazuj wersje
deweloperskie”. Wyświetlacz nigdy o nie nie pyta.

## 2. Stan obecny (co już jest)

- **OTA ze spec 01:** `ota_update` — `OtaSession` (start/feed/finish/abort) nad interfejsem
  `OtaWriter`; nagłówek obrazu, rozmiar, znacznik `CYDSKY-FW1|board=…|v=…`; `Update.end()`
  dopiero po znalezieniu znacznika. Adapter `UpdateWriter` i statyczna sesja żyją dziś
  w `config_portal.cpp` (do przeniesienia, §5.5).
- **Ekran serwisowy:** `service_screen` — `drawOtaScreen` / `drawOtaProgress` / `drawOtaResult`,
  `otaVersionLine()`; przyciski TAK/NIE (`resetButtonRect`, `resetConfirmHit`) z resetu hasła.
- **Rollback:** `verifyRollbackLater()` + `shouldMarkAppValid()` — działa bez zmian także dla
  obrazu pobranego z GitHuba.
- **Wersja:** `firmwareVersion()` = `git describe --tags --always --dirty`, czyli `v1.1.0`
  (czysty tag), `v1.1.0-3-g1a2b3c4` (3 commity po tagu), `1a2b3c4-dirty`, `dev` (native).
- **Wydania:** `release.yml` na tag `v*` tworzy Release z plikami z `firmware.yml`, m.in.
  `cyd-sky-tracker-v1.1.0.bin` (sam obraz aplikacji — ten, który chcemy), `…-full.bin`,
  `SHA256SUMS.txt`. Tagi: `v1.0.4`, `v1.0.5`, `v1.1.0`.
- **HTTPS dziś:** `opensky_client`, `aircraft_lookup`, `route_lookup` — `WiFiClientSecure`
  z `setInsecure()` (bez weryfikacji certyfikatu), `HTTPClient`, limity czasu z `net_config.h`.
  Dla danych o samolotach to akceptowalne; **dla firmware'u nie** (§5.3).
- **Czas:** `time_sync::isTimeSynced()` — potrzebne do weryfikacji dat ważności certyfikatów.
- **Sterta:** ~213 KB wolne po pollingu (spec 01 §10); jedna sesja TLS to ~40–50 KB.
- **Brak:** zapytań do GitHuba, weryfikacji TLS, sumy SHA-256 przy OTA, porównywania wersji.

## 3. Zakres

### W zakresie (v1)

1. Nowy moduł **`release_check`**: czysta logika (wersje, wybór pliku z wydania, parsowanie
   odpowiedzi GitHuba, harmonogram, dozwolone hosty, decyzja o pokazaniu pytania) + adapter
   HTTP (sprawdzenie i pobranie) za `#ifdef ARDUINO`.
2. Sprawdzanie `GET /repos/{owner}/{repo}/releases/latest` **raz na dobę** (pierwsze ~2 min po
   starcie) i na żądanie („Sprawdź teraz” na stronie). Warunkowe zapytania z `ETag`.
3. **TLS z weryfikacją certyfikatu** dla `api.github.com`, `github.com` i hosta plików
   wydań (pierwsze takie połączenia w projekcie). Lista dozwolonych hostów przy
   przekierowaniu.
4. **Pobranie i instalacja** obrazu strumieniowo do nieaktywnego slotu przez `OtaSession`, plus
   nowa kontrola: **SHA-256** całego pliku zgodna z tą z GitHuba, sprawdzana *przed*
   `Update.end()`.
5. **Strona ustawień, zakładka Firmware:** karta „Aktualizacje z GitHuba” — stan, data
   sprawdzenia, „Sprawdź teraz”, „Zainstaluj v1.4.0”, link „Co nowego”, przełącznik
   „Sprawdzaj automatycznie”.
6. **Wyświetlacz:** jednorazowe pytanie „DOSTĘPNA WERSJA v1.4.0 — ZAINSTALOWAĆ?” TAK / NIE
   (raz na wersję), ekran postępu pobierania (istniejący ekran OTA).
7. Ustawienia `update_check_enabled` (domyślnie **włączone**), `update_show_prerelease`
   (domyślnie **wyłączone**) i „pominięta wersja” `update_skipped_tag` w `config_store`, kopia
   zapasowa, `web_i18n` / `i18n`.
8. **Wersje deweloperskie** (§5.10): `release.yml` oznacza tagi z sufiksem jako pre-release;
   panel admina pokazuje je i pozwala zainstalować po zaznaczeniu checkboxa.
9. CI: krok sprawdzający, że wbudowane certyfikaty CA dalej pasują do GitHuba (§5.3).
10. **Zrzuty ekranu generowane z testów** (§5.11): pytanie o aktualizację na wyświetlaczu
    (`docs/screenshots/update_prompt.png`) i karta aktualizacji na stronie ustawień
    (`docs/screenshots/web_firmware.png`) — z tych samych danych testowych, bez płytki.
11. **Strona projektu** (`site/`, §5.12): sekcja o aktualizacjach (OTA ze strony, pytanie
    z GitHuba, wersje deweloperskie) PL/EN, nowe zrzuty.
12. Dokumentacja: README („Updating” — trzeci sposób + zrzuty), CLAUDE.md (sekcja OTA + moduł),
    ROADMAP.

### Poza zakresem (na później)

- **Automatyczna instalacja bez pytania** (np. nocą) — tylko gdy v1 się sprawdzi; wymagałaby
  okna serwisowego i pewności rollbacku.
- **Wersje deweloperskie na wyświetlaczu** — świadomie nigdy (decyzja §8.6).
- **Buildy z gałęzi `main`** (artefakty `firmware.yml` z każdego pusha) jako wersje
  deweloperskie — artefakty Actions wymagają tokena do pobrania; wersja deweloperska = tag
  pre-release, czyli świadome „wypuszczam do testów”.
- **Downgrade z GitHuba** (wybór starszego wydania z listy) — starsze wgrywa się ręcznie
  przez zakładkę Firmware jak dziś.
- **Podpisywanie obrazów** (secure boot / własny klucz). SHA-256 z GitHuba chroni przed
  uszkodzeniem i podmianą *w drodze*, nie przed przejęciem konta / repozytorium (§5.3).
- **Token GitHuba** na urządzeniu — limit anonimowy (60 zapytań / h / IP) wystarcza z dużym
  zapasem.
- Zmiana pozostałych połączeń (OpenSky, hexdb.io) na zweryfikowany TLS — osobna decyzja;
  ta spec przygotowuje do tego mechanizm (§5.3), ale go tam nie włącza.
- Przeniesienie sieci na drugi rdzeń (ROADMAP #2) — pobieranie blokuje pętlę jak upload
  w spec 01; gdy #2 powstanie, `release_check` przechodzi na rdzeń sieciowy razem z resztą.

## 4. Przepływ użytkownika

### 4.1 Sprawdzanie (w tle)

1. Po starcie, gdy jest WiFi, zsynchronizowany czas i minęło ≥ 120 s (po pierwszym pollingu
   OpenSky — §5.6), urządzenie pyta GitHuba o najnowsze wydanie. Potem co 24 h.
2. Wynik w logu: `[update] latest v1.4.0 (installed v1.3.0) -> available, asset 1372160 B` /
   `[update] latest v1.3.0 -> up to date` / `[update] check failed: tls (-9984), retry in 1 h`.
3. Nic nie pojawia się na ekranie, jeśli nie ma nic nowego.

### 4.2 Pytanie na wyświetlaczu

1. Gdy jest nowsza wersja, nie jest pominięta i nie trwa ważny moment (§5.7), urządzenie
   pokazuje pełnoekranowe pytanie (raz na wersję na uruchomienie):
   ```
   DOSTĘPNA NOWA WERSJA
        v1.3.0 → v1.4.0
   ZAINSTALOWAĆ?   (ok. 1 min, ustawienia zostaną)
   [  TAK  ]   [  NIE  ]
   ```
2. **TAK** → ekran OTA („POBIERANIE Z GITHUBA”, pasek, %, „NIE WYŁĄCZAJ ZASILANIA”) →
   sprawdzenie → restart → nowa wersja.
3. **NIE** → zapamiętuje `update_skipped_tag = v1.4.0`, wyświetlacz więcej o nią nie pyta
   (strona dalej ją pokazuje z przyciskiem). Nowsza `v1.4.1` znowu zapyta.
4. **Brak reakcji przez 30 s** → „później”: powrót do ekranów, pytanie wróci po następnym
   uruchomieniu (nie po kolejnym sprawdzeniu w tej samej sesji — jedno pytanie na boot).

### 4.3 Strona ustawień (zakładka Firmware, nad ręcznym uploadem)

Karta „Aktualizacje z GitHuba”:
- „Zainstalowana: **v1.3.0** · Najnowsza na GitHubie: **v1.4.0** (opublikowana 20.10.2026)”
  + link „Co nowego →” (`html_url` wydania).
- Stan: „Dostępna nowsza wersja” / „Masz najnowszą wersję” / „Wersja deweloperska — porównanie
  niemożliwe” / „Nie udało się sprawdzić: …” + „sprawdzono 24.10.2026 09:11”.
- Przyciski: **Sprawdź teraz** (`POST /update/check`), **Zainstaluj v1.4.0**
  (`POST /update/install`, tylko gdy jest co instalować).
- Checkbox na zakładce Ogólne (zapisywany wspólnym „Zapisz”): „Sprawdzaj nowe wersje na
  GitHubie raz na dobę”.
- Checkbox na karcie (osobny mały formularz, zapis od razu + ponowne sprawdzenie):
  **„Pokazuj wersje deweloperskie”**. Zaznaczony → karta dostaje drugi wiersz:
  „Najnowsza deweloperska: **v1.5.0-rc.1** (22.10.2026) — [Zainstaluj v1.5.0-rc.1]” z
  ostrzeżeniem „Wersja testowa — może zawierać błędy. Powrót: zainstaluj wydanie powyżej.”
  Pokazywany tylko, gdy deweloperska jest nowsza od najnowszego wydania (inaczej nie ma po co).
- Gdy zainstalowana jest wersja deweloperska (`v1.5.0-rc.1`, albo build lokalny), a najnowsze
  wydanie jest od niej „starsze” (`v1.4.0`), przycisk wydania brzmi „Wróć do wydania v1.4.0”
  — jedyny przypadek, w którym GitHub proponuje niższą wersję, i tylko w panelu.
- Po „Zainstaluj”: strona „Pobieranie v1.4.0 z GitHuba… płytka uruchomi się ponownie, strona
  odświeży się sama” — JS jak przy restarcie po OTA (odpytuje `/` co 2 s, tu do 180 s).
- Wynik nieudanej instalacji (urządzenie nie restartuje): przy następnym wejściu na zakładkę
  komunikat „Instalacja v1.4.0 nie powiodła się: suma SHA-256 się nie zgadza” (z RAM).

## 5. Projekt techniczny

### 5.1 GitHub — co wołamy i co dostajemy

Repozytorium jest stałą firmware'u: `kUpdateRepo = "alasota/CYD-Flight-Tracker"`, nadpisywalną
flagą `-DFW_UPDATE_REPO=\"owner/repo\"` (forki). Nigdy z ustawień ani z sieci — inaczej
ktokolwiek z dostępem do panelu przekierowałby aktualizacje na własne repo.

**Sprawdzenie:**
```
GET https://api.github.com/repos/alasota/CYD-Flight-Tracker/releases/latest
Accept: application/vnd.github+json
X-GitHub-Api-Version: 2022-11-28
User-Agent: cyd-sky-tracker/<firmwareVersion()>      ← wymagane przez GitHub (bez niego 403)
If-None-Match: "<etag z poprzedniej odpowiedzi>"      ← gdy jest
```
- `200` — JSON (pola, których używamy; reszta odfiltrowana, w tym `body` z notatkami, który
  bywa długi):
  ```json
  {"tag_name":"v1.4.0","html_url":"https://github.com/.../releases/tag/v1.4.0",
   "published_at":"2026-10-20T18:02:11Z","draft":false,"prerelease":false,
   "assets":[{"name":"cyd-sky-tracker-v1.4.0.bin","size":1372160,
              "digest":"sha256:9f86d0…",
              "browser_download_url":"https://github.com/alasota/CYD-Flight-Tracker/releases/download/v1.4.0/cyd-sky-tracker-v1.4.0.bin"},
             {"name":"SHA256SUMS.txt", …}, …]}
  ```
- `304 Not Modified` — nic się nie zmieniło; **nie liczy się do limitu** GitHuba.
- `404` — repo nie ma jeszcze żadnego wydania → stan `NoRelease`.
- Gdy włączone „Pokazuj wersje deweloperskie”, **drugie** zapytanie w tym samym sprawdzeniu
  (osobny `ETag`): `GET /repos/{owner}/{repo}/releases?per_page=10` — lista najnowszych
  wydań, w tym pre-release (bez draftów — te widzi tylko właściciel z tokenem). Z listy
  bierzemy najwyższą wersję z `prerelease:true` (`pickLatestPrerelease()`); jej zasób i suma
  jak niżej. Odpowiedź jest większa (10 × notatki) — filtr strumieniowy zostawia tylko pola
  z §5.2, więc RAM się nie zmienia. Dwa zapytania na dobę wciąż mieszczą się w limicie z
  ogromnym zapasem.
- `403` / `429` z `X-RateLimit-Remaining: 0` — limit; czekaj do `X-RateLimit-Reset` (epoch s)
  albo `Retry-After`, nie krócej niż 1 h.
- Parsowanie strumieniowe z filtrem ArduinoJson (`useHTTP10(true)`, jak w `opensky_client`,
  żeby uniknąć `Transfer-Encoding: chunked`).

**Plik do pobrania:** zasób o nazwie dokładnie `releaseAssetName(tag, firmwareBoard())`:
- dla `cyd-r`: `cyd-sky-tracker-<tag>.bin` (obecna nazwa — zgodność z istniejącymi wydaniami);
- inne płytki (ROADMAP #3): `cyd-sky-tracker-<board>-<tag>.bin` — decyzja o nazwie należy do
  #3, ale ma powstać w tej jednej funkcji. Brak pasującego zasobu → stan `NoAsset` („wydanie
  nie ma pliku dla tej płytki”), nic nie instalujemy.
- Nigdy `…-full.bin` (i tak odrzuciłby go `checkImageHeader`).

**Suma SHA-256:** z pola `digest` zasobu (`"sha256:<64 hex>"`) — GitHub dodał je do API zasobów
w 2025 r., **do potwierdzenia w kroku 1**. Gdy pola brak (starsze wydania / zmiana API):
pobierz mały zasób `SHA256SUMS.txt` (format `sha256sum`: `<hex>  <nazwa>`) i weź linię z naszą
nazwą. Gdy nie ma żadnego źródła sumy → **nie instalujemy** (`NoChecksum`), strona odsyła do
ręcznego uploadu.

**Pobranie:**
```
GET https://github.com/alasota/CYD-Flight-Tracker/releases/download/v1.4.0/cyd-sky-tracker-v1.4.0.bin
→ 302 Location: https://release-assets.githubusercontent.com/github-production-release-asset/…?sp=r&sv=…&sig=…
```
- Host docelowy: dziś `release-assets.githubusercontent.com` (GitHub przeniósł pliki wydań
  w 2025 r. z `objects.githubusercontent.com`). Dozwolone **oba**, do potwierdzenia w kroku 1.
- URL po przekierowaniu jest podpisany i długi (~0,5–1,5 KB) i ważny kilka minut — nie
  zapisujemy go, za każdym razem idziemy od `browser_download_url`.
- Przekierowania obsługujemy **ręcznie** (`HTTPC_DISABLE_FOLLOW_REDIRECTS`, `collectHeaders
  ({"Location"})`, nowy klient TLS na nowy host), nie `HTTPC_FORCE_FOLLOW_REDIRECTS`:
  `HTTPClient` 2.0.17 przy zmianie hosta używa tego samego `WiFiClientSecure` i nie pozwala
  sprawdzić hosta przed połączeniem. Maks. 3 przeskoki, każdy URL przez `isAllowedUpdateUrl()`.

### 5.2 Nowy moduł: `release_check` (`src/release_check.h/.cpp`, `test/test_release_check/`)

Czysta część (testy pisane przed implementacją):

```cpp
// ---- Wersje. Tagi wydań: "v<major>.<minor>.<patch>", deweloperskie z sufiksem
// "-dev.N" / "-beta.N" / "-rc.N" (kolejność: dev < beta < rc < wydanie, jak w semver).
enum class PreKind : uint8_t { Dev, Beta, Rc, None };  // None = wydanie, najwyższe
struct FwVersion {
  bool valid = false;      // dało się odczytać major.minor.patch
  uint16_t major = 0, minor = 0, patch = 0;
  PreKind pre = PreKind::None;
  uint16_t preNumber = 0;     // "-rc.2" -> 2
  uint16_t commitsAhead = 0;  // "v1.3.0-3-g1a2b3c4" -> 3 (także po "-rc.1")
  bool dirty = false;         // "...-dirty"
};
// "v1.3.0", "v1.4.0-rc.1", "v1.3.0-3-gabc", "v1.4.0-rc.1-2-gabc-dirty", "abc-dirty", "dev"
FwVersion parseFwVersion(const std::string &s);
// <0 / 0 / >0 po (major, minor, patch, pre, preNumber); commitsAhead/dirty nie biorą udziału.
int compareFwVersions(const FwVersion &a, const FwVersion &b);
// Wersja deweloperska zainstalowana: sufiks pre, commity po tagu, dirty albo nieczytelna.
bool isDevelopmentBuild(const FwVersion &installed);

enum class UpdateOffer : uint8_t {
  Newer,        // latest > installed — proponuj
  UpToDate,     // latest == installed (czysty tag) albo installed nowszy (lokalny build po tagu)
  BackToRelease,// installed deweloperska i nowsza od latest — panel: „Wróć do wydania”
  DevBuild,     // installed nieczytelny / dirty / "dev" — porównanie niemożliwe
  BadLatest,    // tag_name nie pasuje do formatu — ignoruj wydanie
};
UpdateOffer decideOffer(const std::string &installed, const std::string &latestTag);

// ---- Wydanie z GitHuba
struct ReleaseInfo {
  std::string tag;          // "v1.4.0"
  std::string htmlUrl;
  std::string publishedAt;  // ISO 8601, do pokazania jako DD.MM.YYYY
  std::string assetUrl;     // browser_download_url pasującego zasobu, "" gdy brak
  size_t assetSize = 0;
  std::string sha256;       // 64 małe hex, "" gdy brak digest
  std::string sumsUrl;      // SHA256SUMS.txt, "" gdy brak
  bool prerelease = false;
};
std::string releaseAssetName(const std::string &tag, const std::string &board);
// false: niepoprawny JSON / draft / prerelease / brak tag_name.
bool parseLatestRelease(const std::string &json, const std::string &board, ReleaseInfo &out);
// Z listy /releases: najwyższa wersja z prerelease:true i poprawnym tagiem; false gdy brak.
// Drafty i tagi spoza formatu pomijane. Pre-release z tagiem bez sufiksu (ktoś zaznaczył
// ręcznie) też się liczy — rozstrzyga flaga GitHuba, nie nazwa.
bool pickLatestPrerelease(const std::string &json, const std::string &board, ReleaseInfo &out);
// Wersja strumieniowa dla adaptera: ten sam filtr, ten sam wynik (test: obie zgodne).
// (deklaracja z szablonem Stream / ArduinoJson Reader — w implementacji)

// "sha256:<64 hex>" -> 64 małe hex; "" gdy format inny.
std::string parseAssetDigest(const std::string &digest);
// Linia "<hex>  <name>" (także "<hex> *<name>") z pliku SHA256SUMS.txt.
std::string findSha256InSums(const std::string &sums, const std::string &assetName);

// ---- Dozwolone adresy (https, host z listy, ścieżka repo dla github.com).
//   api.github.com              /repos/<kUpdateRepo>/releases/...
//   github.com                  /<kUpdateRepo>/releases/download/...
//   release-assets.githubusercontent.com, objects.githubusercontent.com  (dowolna ścieżka)
bool isAllowedUpdateUrl(const std::string &url);
constexpr int kMaxUpdateRedirects = 3;

// ---- Harmonogram (millis(); odporny na przepełnienie jak AuthThrottle)
constexpr uint32_t kUpdateFirstCheckAfterMs = 120000;          // 2 min po starcie
constexpr uint32_t kUpdateCheckIntervalMs = 24UL * 3600 * 1000;
constexpr uint32_t kUpdateRetryAfterFailureMs = 3600UL * 1000; // błąd sieci/TLS: 1 h
constexpr uint32_t kUpdateManualMinGapMs = 60000;              // „Sprawdź teraz” max raz na minutę
enum class CheckOutcome : uint8_t { Ok, NotModified, NoRelease, RateLimited, Failed };
// Kiedy następne sprawdzenie (ms od teraz); rateLimitWaitS z nagłówków (0 = brak).
uint32_t nextCheckDelayMs(CheckOutcome o, uint32_t rateLimitWaitS);
// Czy wolno teraz zacząć zapytanie: sprawdzanie włączone, WiFi, czas zsynchronizowany,
// nie trwa OTA / restart, do następnego pollingu OpenSky >= kUpdateMinPollGapMs (5 s),
// wolny największy blok sterty >= kUpdateMinFreeBlock (50 KB).
struct CheckGate {
  bool enabled, wifi, timeSynced, otaBusy;
  uint32_t msUntilNextPoll, maxFreeBlock;
};
bool canStartNetworkTask(const CheckGate &g);

// ---- Stan widoczny dla strony i main.cpp
enum class UpdateStatus : uint8_t {
  NeverChecked, UpToDate, Available, DevBuild, NoRelease, NoAsset, NoChecksum,
  CheckFailed, RateLimited, Disabled,
};
struct UpdateState {
  UpdateStatus status = UpdateStatus::NeverChecked;
  ReleaseInfo latest;
  ReleaseInfo latestPre;         // tag "" gdy brak / checkbox wyłączony / nie nowsza od latest
  std::string etagPre;
  time_t checkedAt = 0;          // epoka, 0 = nigdy (pokazywane przez formatDate/formatTime)
  std::string etag;              // tylko RAM
  int lastError = 0;             // kod HTTP albo błąd TLS mbedTLS, do komunikatu
  OtaResult lastInstall = OtaResult::NoFile;  // wynik ostatniej nieudanej instalacji w tej sesji
};
WebStr updateStatusMessage(UpdateStatus s);

// ---- Pytanie na wyświetlaczu (§5.7)
struct PromptGate {
  UpdateStatus status; std::string latestTag, skippedTag;
  bool alreadyPromptedThisBoot;
  int currentScreen; bool cpaFound; int tCpaSeconds;  // jak shouldDeferAutoSwitch
  bool touchActive;  // palec na ekranie — nie wyskakuj pod palcem
};
bool shouldShowUpdatePrompt(const PromptGate &g);
constexpr uint32_t kUpdatePromptTimeoutMs = 30000;
```

Testy `test_release_check.cpp`:
- `parseFwVersion`: `v1.3.0`; `v1.3.0-3-g1a2b3c4` (ahead 3); `v1.3.0-3-g1a2b3c4-dirty`;
  `1a2b3c4`; `1a2b3c4-dirty`; `dev`; `""`; `v1.3`; `v1.3.0.1`; `1.3.0` (bez `v` — niepoprawny);
  `v65536.0.0` (przepełnienie → niepoprawny); `v01.2.3` (dozwolone wiodące zera? — przyjąć
  i przetestować jedną decyzję: dozwolone).
- `compareFwVersions` / `decideOffer`: `v1.3.0`→`v1.4.0` Newer; `v1.3.9`→`v1.10.0` Newer
  (porównanie liczbowe, nie napisowe); `v1.4.0`→`v1.4.0` UpToDate; `v1.4.0-2-gabc`→`v1.4.0`
  UpToDate; `v1.4.0-2-gabc`→`v1.4.1` Newer; `v1.5.0`→`v1.4.0` UpToDate (nie proponuj starszej);
  `abc-dirty`→… DevBuild; `v1.3.0-dirty`→`v1.4.0` DevBuild; latest `nightly` BadLatest.
- Wersje deweloperskie: `v1.4.0-dev.1` < `v1.4.0-beta.1` < `v1.4.0-rc.1` < `v1.4.0-rc.2` <
  `v1.4.0` < `v1.4.1-rc.1`; `v1.4.0-rc.10` > `v1.4.0-rc.9` (liczbowo); `v1.4.0-rc` (bez
  numeru), `v1.4.0-alpha.1`, `v1.4.0-rc.1.2` → niepoprawne; `v1.4.0-rc.1-2-gabc` = rc.1 +
  2 commity. `decideOffer`: `v1.5.0-rc.1`→`v1.4.0` BackToRelease; `v1.4.0-rc.2`→`v1.4.0`
  Newer; `isDevelopmentBuild` dla każdego wariantu.
- `pickLatestPrerelease`: fixture z listy 10 wydań (mieszane wydania / pre-release, nie po
  kolei) → najwyższa pre-release; brak pre-release → false; pre-release bez pliku dla płytki →
  pomijana na rzecz następnej.
- `parseLatestRelease`: prawdziwa odpowiedź GitHuba zapisana jako fixture
  (`test/test_release_check/fixtures/latest.json`, z długim `body` i 7 zasobami); wybór
  zasobu po nazwie (nie `-full.bin`, nie `SHA256SUMS.txt`); brak zasobu → `assetUrl` puste;
  `draft:true` / `prerelease:true` → false; brak `digest` → `sha256` puste, `sumsUrl` ustawione;
  śmieci / obcięty JSON → false; nazwa dla płytki `cyd-c` (przyszłość) → inny zasób.
- `parseAssetDigest`: poprawny; wielkie litery → małe; `sha512:…`; 63 znaki; nie-hex.
- `findSha256InSums`: linia ze spacją i z `*`; nazwa będąca prefiksem innej
  (`…v1.4.0.bin` vs `…v1.4.0-full.bin`); CRLF; brak linii.
- `isAllowedUpdateUrl`: każdy dozwolony; `http://` (bez TLS) — nie; `api.github.com.evil.com`;
  `evil.com/api.github.com/...`; `github.com/innyuser/CYD-Flight-Tracker/...`; `user@host`;
  port (`:443` ok, inny nie); wielkość liter hosta.
- `nextCheckDelayMs`: każdy `CheckOutcome`; `RateLimited` z 10 s → 1 h (minimum), z 2 h → 2 h,
  z absurdalnym 10 dni → przycięte do 24 h.
- `canStartNetworkTask`: każdy warunek z osobna; granice 4 999 / 5 000 ms i 50 KB.
- `shouldShowUpdatePrompt`: tylko `Available`; pominięta wersja = nie; nowsza niż pominięta =
  tak; już pokazane = nie; ekran TERAZ z `tCpaSeconds` w (−5, 60) = nie (czekaj); dotyk = nie;
  zainstalowana wersja deweloperska = nie; `latestPre` ustawione (checkbox włączony) nie
  zmienia niczego — pytanie dotyczy wyłącznie `latest`.
- `updateStatusMessage`: każdy `UpdateStatus` ma tekst (pętla po enumie).

### 5.3 Bezpieczeństwo: TLS z weryfikacją i lista hostów

Dlaczego tu inaczej niż dla OpenSky: `setInsecure()` + instalacja kodu = każdy, kto podsłucha
lub podstawi DNS w sieci (hotspot, ruter), może podać własny „`v9.9.9`” i plik. Kontrola
znacznika chroni przed pomyłką, nie przed złośliwym autorem; SHA-256 z odpowiedzi API pobranej
bez weryfikacji też nic nie daje. Dlatego:

- **Certyfikaty CA w firmware:** nowy plik `src/github_ca.h` (adapter, `#ifdef ARDUINO` nie
  potrzebny — sam literał) z PEM certyfikatów **głównych** (nie pośrednich — te GitHub zmienia
  częściej), sklejonych w jeden napis dla `WiFiClientSecure::setCACert()` (mbedTLS czyta
  wiele PEM z jednego bufora). Lista do ustalenia w kroku 1 poleceniem
  `openssl s_client -connect <host>:443 -showcerts` dla `api.github.com`, `github.com`,
  `release-assets.githubusercontent.com`, `objects.githubusercontent.com`. Oczekiwane (stan
  wiedzy, do sprawdzenia): *USERTrust ECC Certification Authority*, *USERTrust RSA
  Certification Authority* (Sectigo — github.com / api.github.com), *DigiCert Global Root G2*
  i *DigiCert Global Root CA* (githubusercontent). Plus zapasowo *ISRG Root X1*. Każdy z datą
  ważności w komentarzu.
- Alternatywa rozważona i odrzucona na v1: pakiet CA Mozilli (`setCACertBundle()` +
  `board_build.embed_files`, ~60–100 KB flash) — działa „ze wszystkim”, ale wymaga generowania
  pakietu w buildzie; wrócić do niej, jeśli lista ręczna zacznie się często psuć albo gdy
  OpenSky / hexdb.io też przejdą na weryfikację.
- **Czas:** zapytania tylko po `isTimeSynced()` — mbedTLS sprawdza daty ważności; z czasem
  z 1970 r. każdy certyfikat jest „jeszcze nieważny”.
- **Hosty:** każdy URL (początkowy i każdy `Location`) przez `isAllowedUpdateUrl()` *przed*
  połączeniem. Tylko `https`. Repo stałe w firmware (§5.1).
- **Integralność:** `OtaSession` liczy SHA-256 wszystkich przyjętych bajtów i porównuje
  z oczekiwaną *przed* `writer.end()` (§5.4). Niezgodność → `abort()`, slot nieaktywowany.
- **Rozmiar:** `assetSize` z API i `Content-Length` pobrania muszą się zgadzać i przejść
  `checkImageSize()`; inaczej nie zaczynamy.
- **Znacznik:** bez zmian — wersja w znaczniku pobranego pliku musi być **równa** `tag`
  wydania (nowy wynik `OtaResult::VersionMismatch`), plansza `board` = `firmwareBoard()` (to
  wymuszenie zostaje w gestii ROADMAP #3; tu tylko zapisujemy w logu).
- **CI pilnuje CA:** krok w `firmware.yml` (nie blokujący builda, `continue-on-error: true`
  + adnotacja `::warning::`), który wyciąga PEM-y z `src/github_ca.h` i robi
  `openssl s_client -verify_return_error -CAfile …` do czterech hostów. Ostrzeżenie w CI
  przychodzi wcześniej niż „aktualizacje przestały działać” na urządzeniach.
- **Gdy CA się zmieni i weryfikacja zawiedzie:** stan `CheckFailed` z komunikatem „nie udało
  się bezpiecznie połączyć z GitHubem — pobierz plik ręcznie”; nic się nie psuje, ręczny upload
  działa. Nowe CA trafiają do kolejnego wydania instalowanego ręcznie.
- **CSRF:** formularze `/update/install` / `/update/check` są za tym samym hasłem Digest co reszta.
  Obca strona w przeglądarce z zapamiętanym logowaniem mogłaby co najwyżej uruchomić instalację
  *oficjalnego najnowszego wydania z tego repo* — akceptowalne. `install` wymaga pola `tag`
  równego `state.latest.tag` (żadnych dowolnych URL-i z formularza).
- **Dotyk TAK bez hasła:** jak przy resecie hasła — fizyczny dostęp do płytki = prawo do
  zainstalowania oficjalnego wydania.

### 5.4 Zmiany w `ota_update`

- `OtaSession::expectSha256(const std::string &hex)` — opcjonalnie, przed `start()`; pusty =
  bez kontroli (ręczny upload jak dziś).
- `OtaSession::expectVersion(const std::string &tag)` — opcjonalnie; po znalezieniu znacznika
  porównuje `marker().version()`.
- Liczenie skrótu przez interfejs (natywne testy nie mają mbedTLS):
  ```cpp
  class OtaDigest {
   public:
    virtual ~OtaDigest() = default;
    virtual void reset() = 0;
    virtual void update(const uint8_t *data, size_t len) = 0;
    virtual std::string finishHex() = 0;  // 64 małe hex
  };
  OtaSession(OtaWriter &writer, size_t slotBytes, OtaDigest *digest = nullptr);
  ```
  Adapter na płytce: `mbedtls_sha256_*` (w ESP32 sprzętowy akcelerator). W testach: fałszywy
  „skrót” (np. suma bajtów jako hex) + jeden test z prawdziwym SHA-256 tylko w adapterze —
  nie pod `native`.
- `finish()`: znacznik → wersja → skrót → `writer.end()`. Nowe `OtaResult`:
  `ChecksumMismatch` (400), `VersionMismatch` (400), `DownloadFailed` (502 — zerwane
  pobieranie / zły kod HTTP / `Content-Length` niezgodny). `kOtaResultCount` = 16; komunikaty
  `WebStr` dla każdego (pętla w teście już to pilnuje).
- Adapter `UpdateWriter` i tworzenie sesji (rozmiar slotu) przenieść z `config_portal.cpp` do
  `ota_update.cpp` pod `#ifdef ARDUINO`: `OtaSession &otaFlashSession()` — współdzielone przez
  `/update` i `release_check`. Jedna sesja = nie da się uruchomić dwóch aktualizacji naraz
  (`otaBusy()`).
- Testy w `test_ota_update`: zgodny skrót → `Ok`; niezgodny → `ChecksumMismatch` i
  `writer.end()` **nie** wywołane (fałszywy writer liczy wywołania); bez `expectSha256` jak
  dziś; wersja zgodna / niezgodna; kolejność kontroli (znacznik przed skrótem).

### 5.5 Adapter: sprawdzenie i pobranie (`release_check.cpp`, `#ifdef ARDUINO`)

```cpp
void releaseCheckBegin(const Config &cfg);           // z setup()
void releaseCheckLoop(const CheckGate &gate);         // z loop(), szybkie gdy nic do roboty
void releaseCheckRequestNow();                        // „Sprawdź teraz” (z config_portal)
// false: tag nie jest ani latest.tag, ani latestPre.tag (ten drugi tylko przy włączonym
// checkboxie) / trwa inna aktualizacja.
bool releaseInstallRequest(const std::string &tag);
const UpdateState &releaseUpdateState();
void releaseSetOtaObserver(const OtaObserver &o);     // te same callbacki co config_portal
bool releaseInstallInProgress();
```

**Sprawdzenie** (`runCheck()`), jedno zapytanie, blokujące ~1–3 s (jak polling OpenSky):
`WiFiClientSecure` + `setCACert(kGithubRootCAs)` + `HTTPClient`, `setConnectTimeout
(kHttpConnectTimeoutMs)`, `setTimeout(kGithubHttpTimeoutMs = 8000)` (nowa stała w
`net_config.h`), nagłówki z §5.1, `collectHeaders({"ETag","X-RateLimit-Remaining",
"X-RateLimit-Reset","Retry-After"})`. 200 → filtr JSON ze strumienia → `ReleaseInfo` →
`decideOffer` → stan. Jeśli `sha256` puste i jest `sumsUrl` — sumy pobierane dopiero przy
instalacji (nie przy każdym sprawdzeniu). Błąd TLS: `client.lastError()` do logu i
`state.lastError`.

**Instalacja** (`runInstall()`) — **nie** w handlerze HTTP: handler `/update/install` tylko
ustawia flagę i odpowiada stroną, właściwa praca rusza w następnym `releaseCheckLoop()`
(odpowiedź musi wyjść do przeglądarki, zanim pętla zablokuje się na ~30–60 s).
1. Odśwież wydanie (`runCheck()` bez ETag) — URL/rozmiar/skrót muszą być aktualne; jeśli
   `latest.tag` się zmienił, instaluj tylko gdy to wciąż ten sam tag, inaczej przerwij
   (`state` pokaże nową wersję do potwierdzenia).
2. Brak `sha256` → pobierz `SHA256SUMS.txt` (≤ 2 KB, z przekierowaniem, `findSha256InSums`);
   dalej brak → `NoChecksum`, koniec.
3. `GET browser_download_url` → ręczne przekierowania (§5.1) → `200`, `Content-Length` ==
   `assetSize`.
4. `OtaSession`: `expectSha256`, `expectVersion(tag)`, `start(size)`, obserwator `onStart`;
   czytanie `getStreamPtr()` po 4 KB (bufor statyczny, nie na stosie `loopTask`) z limitem
   bez danych 15 s i limitem całości 180 s; `feed()`, postęp co 16 KB do obserwatora.
5. `finish()` → `Ok`: ekran „GOTOWE · RESTART…”, 1,5 s, `ESP.restart()` (ten sam mechanizm
   co `configPortalRestartPending()` — wyciągnąć do wspólnego `scheduleRestart()`).
   Błąd: `abort()`, `state.lastInstall = r`, ekran „AKTUALIZACJA NIEUDANA” 5 s, pełny redraw.
6. Log: `[update] install v1.4.0: 1372160 B in 23 s, sha256 ok, marker v1.4.0 -> restart` /
   `[update] install v1.4.0 failed: ChecksumMismatch (got 1a2b…, want 9f86…)`.

Wolna sterta przed i po (`ESP.getFreeHeap()`, `ESP.getMaxAllocHeap()`) do logu — przy
pierwszym wdrożeniu porównać z OTA ze strony.

### 5.6 `main.cpp`

- `releaseCheckBegin(cachedCfg)` w `setup()` po `configPortalBegin()`; obserwator OTA ten sam
  co dla portalu (`otaOnStart` / `otaOnProgress` / `otaOnEnd`), tylko tytuł ekranu inny —
  `drawOtaScreen(tft, from, OtaSource::GitHub)` („POBIERANIE Z GITHUBA” / „DOWNLOADING FROM
  GITHUB”).
- W `loop()`, po obsłudze portalu i pollingu: `releaseCheckLoop(gate)`, gdzie `gate` budują
  dane, które `main.cpp` już ma (czas do następnego pollingu, `isTimeSynced()`, WiFi, sterta).
  Polling OpenSky i lookupy hexdb.io w tej samej iteracji co zapytanie do GitHuba — nie (jedna
  sesja TLS naraz).
- Po zapisie ustawień (`configPortalSaveCount()`): `releaseCheckBegin(cachedCfg)` ponownie
  (włączenie / wyłączenie sprawdzania).
- Podczas instalacji: dotyk ignorowany, auto-cycle wstrzymany (jak `configPortalOtaInProgress()`
  — rozszerzyć warunek o `releaseInstallInProgress()`).

### 5.7 Pytanie na wyświetlaczu (`service_screen`, `main.cpp`)

- Pokazywane, gdy `shouldShowUpdatePrompt()`; `alreadyPromptedThisBoot` ustawiane od razu.
  Nie przerywa „ważnego momentu” na ekranie TERAZ (ta sama logika co
  `shouldDeferAutoSwitch()`, szerzej: aż do minięcia samolotu) — czeka, aż warunek minie.
- Rysowanie: `drawUpdatePromptScreen(tft, from, to)` — tytuł, linia wersji
  (`otaVersionLine()`), podpowiedź, przyciski TAK / NIE z istniejących `resetButtonRect()` /
  `resetConfirmHit()` (przemianować na ogólne `confirmButtonRect()` / `confirmHit()`, bo to już
  nie tylko reset hasła — z testami bez zmian).
- Odliczanie 30 s jako cienki pasek pod przyciskami (`serviceProgressFillPx`), żeby było
  widać, że samo zniknie.
- TAK → `releaseInstallRequest(tag)`. NIE → `cfg.update_skipped_tag = tag`, zapis
  pojedynczego klucza (`saveUpdateSkippedTag()`, jak `saveLastScreen()`), pełny redraw.
  Timeout → pełny redraw, bez zapisu.
- Pierwszy dotyk, który *otworzył* ekran, nie może od razu nacisnąć przycisku: dotyk musi się
  zacząć po wyświetleniu (≥ 300 ms przerwy) — test w `test_service_screen` dla funkcji decyzji.
- Nowe `Str` (PL/EN, `isFontSafeText()` w `test_i18n`): `UpdateAvailableTitle`
  („DOSTĘPNA NOWA WERSJA” / „NEW VERSION AVAILABLE”), `UpdateAskInstall` („ZAINSTALOWAĆ?” /
  „INSTALL?”), `UpdateAskHint` („ok. 1 min · ustawienia zostaną” / „about 1 min · settings are
  kept”), `UpdateYes`, `UpdateNo` (lub ponowne użycie `ResetYes` / `ResetNo`, jeśli tekst ten
  sam), `OtaDownloadingTitle`. Długości: tytuł ≤ 20 znaków przy fontcie 16 px (320 px / 8 px
  ≈ 40, ale z marginesem — sprawdzić pomiarem jak `statusBarZones()`).

### 5.8 Strona ustawień (`config_page`, `config_portal`, `web_i18n`)

- `ConfigPageModel` dostaje `UpdateState` (kopię) i `bool updateCheckEnabled`.
- Zakładka **Firmware**: nowa karta nad uploadem (§4.3). Daty przez `formatDate` /
  `formatTime` z `time_sync` (z `checkedAt`) i z `publishedAt` (pierwsze 10 znaków ISO →
  `DD.MM.YYYY`, czysta funkcja `formatIsoDate()` w `release_check`, test).
- Zakładka **Ogólne**: checkbox `update_check` („Sprawdzaj nowe wersje na GitHubie raz na
  dobę (tylko sprawdza — instaluje dopiero po potwierdzeniu)”).
- Formularze poza głównym (jak `#fw`): `#uc` → `POST /update/check`, `#ui` →
  `POST /update/install` z ukrytym `tag`. Oba odpowiadają **303 → `/?tab=firmware`** (check:
  po wykonaniu sprawdzenia synchronicznie, ~1–3 s; install: od razu, strona z
  `model.restarting`-podobnym stanem `model.downloading` i JS odpytującym `/`).
- Link „Co nowego” tylko gdy `htmlUrl` przechodzi `isAllowedUpdateUrl`-podobną kontrolę hosta
  `github.com` (nie wstawiamy do HTML-a dowolnych napisów z sieci; tag/daty tylko
  `[A-Za-z0-9.\-:]`, reszta odrzucona w `parseLatestRelease` — test).
- Nowe `WebStr` (bez `" < > \ &` — `test_web_i18n`): `UpdateCardTitle`, `UpdateInstalled`,
  `UpdateLatest`, `UpdatePublished`, `UpdateChecked`, `UpdateNever`, `UpdateCheckNow`,
  `UpdateInstallButton`, `UpdateWhatsNew`, `UpdateDownloading`, `UpdateAutoCheck`, po jednym
  na każdy `UpdateStatus` i na nowe `OtaResult`.
- Checkbox „Pokazuj wersje deweloperskie”: formularz `#up` → `POST /update/prerelease`
  (`on=0|1`), zapis jednego klucza (`saveUpdateShowPrerelease()`), ponowne sprawdzenie
  (z limitem `kUpdateManualMinGapMs`), 303 → `/?tab=firmware`.
- Nowe `WebStr` dla kanału deweloperskiego: `UpdateShowPrerelease`, `UpdateLatestPre`,
  `UpdateInstallPre`, `UpdatePreWarning`, `UpdateBackToRelease`.
- Testy `test_config_page`: każdy `UpdateStatus` renderuje się z właściwym tekstem; przycisk
  Zainstaluj tylko przy `Available`, `DevBuild` i `BackToRelease` (z tekstem „Wróć do
  wydania”); ukryty `tag` = `latest.tag`; checkbox na Ogólne round-trip; tag ze znakami spoza
  zestawu nie trafia do HTML-a; wiersz deweloperski tylko przy zaznaczonym checkboxie
  **i** ustawionym `latestPre`, z ostrzeżeniem i własnym ukrytym `tag`.

### 5.9 `config_store`, `config_backup`

- `Config::update_check_enabled` (bool, domyślnie `true`, NVS `updchk`).
- `Config::update_skipped_tag` (string, domyślnie `""`, NVS `updskip`; `sanitizeConfig()`
  czyści, gdy nie jest `vX.Y.Z`). `saveUpdateSkippedTag()` — zapis jednego klucza.
- `Config::update_show_prerelease` (bool, domyślnie `false`, NVS `updpre`).
  `saveUpdateShowPrerelease()` — zapis jednego klucza.
- `config_backup`: `update_check_enabled` i `update_show_prerelease` w grupie `general`;
  `update_skipped_tag` **nie** trafia do kopii (stan urządzenia, nie ustawienie).
  `format_version` zostaje 1.
- Testy: `test_config_store` (domyślne, sanitize), `test_config_backup` (round-trip, stary
  plik bez klucza zachowuje wartość, zły typ → pominięty).

### 5.10 Wersje deweloperskie — po stronie wydań (`release.yml`, `pio_version.py`)

- **Tag = kanał.** `v1.5.0-rc.1` / `-beta.N` / `-dev.N` → GitHub Release z `prerelease: true`;
  zwykły `v1.5.0` → wydanie. W `release.yml`:
  `prerelease: ${{ contains(github.ref_name, '-') }}` w kroku `softprops/action-gh-release`.
- **Strona projektu i instalator web** dalej biorą tylko wydania: `site.yml` czyta
  „latest GitHub Release”, a `/releases/latest` pomija pre-release. Ale `release.yml` dziś
  **zawsze** woła `site.yml` z artefaktem — dla pre-release ten krok trzeba pominąć
  (`if: ${{ !contains(github.ref_name, '-') }}`), inaczej instalator na stronie dostałby rc.
  Test ręczny w §9.
- Changelog na stronie (`build_site.py`, wpisy per tag `v*`): pre-release'y **nie** dostają
  osobnego wpisu (filtr tagów z `-`); ich commity trafiają do wpisu najbliższego wydania.
  Test dla `build_site.py` (jeśli są testy Pythona; jeśli nie — sprawdzenie ręczne na
  wygenerowanym `_site/`).
- `pio_version.py` bez zmian — `git describe` z tagu `v1.5.0-rc.1` daje dokładnie ten napis,
  a znacznik `CYDSKY-FW1|…|v=v1.5.0-rc.1` pasuje do `expectVersion(tag)`.
- CLAUDE.md „Versioning & CI”: dopisać „Wersja deweloperska: `git tag v1.5.0-rc.1 && git push
  origin v1.5.0-rc.1`”.

### 5.11 Zrzuty ekranu z testów (bez płytki)

Cel: `docs/screenshots/update_prompt.png` (pytanie na wyświetlaczu, 320×240) i
`docs/screenshots/web_firmware.png` (karta aktualizacji na stronie ustawień) generowane
z **tych samych danych, na których działają testy** — powtarzalnie, bez łapania momentu na
żywym urządzeniu, i odświeżane jednym poleceniem, gdy zmieni się wygląd.

**Wspólne dane:** `test/fixtures/update_demo.h` — `UpdateState` „dostępna v1.4.0”
(zainstalowana v1.3.0, opublikowana 20.10.2026, sprawdzono 24.10.2026 09:11) oraz wariant
z wersją deweloperską `v1.5.0-rc.1`. Te same struktury używane w `test_release_check`,
`test_config_page` i `test_service_screen`, więc zrzut pokazuje dokładnie stan, który testy
sprawdzają.

**Wyświetlacz — `update_prompt.png`:**
- Nowe środowisko `[env:native-render]` (host, `platform = native`) z LovyanGFX
  skompilowanym na komputer: rysowanie do `LGFX_Sprite` 320×240 w RAM (bez panelu), tymi
  samymi funkcjami `drawUpdatePromptScreen()` co na płytce — żadnej kopii kodu rysującego.
  Test `test/test_render_screens/` (`pio test -e native-render`) rysuje ekran z danych
  `update_demo.h`, zapisuje BMP przez istniejący moduł `screenshot` (`writeBmpHeader`,
  `bmpRowStride`), a `tools/screenshots.py` (Pillow) zamienia go na PNG w
  `docs/screenshots/`. Asercje w teście: obraz nie jest pusty, przyciski TAK/NIE mają kolor
  `THEME_CHROME_ACCENT` w środku `confirmButtonRect()` (próbka pikseli) — czyli test łapie
  też regresje wyglądu, nie tylko produkuje obrazek.
- **Do sprawdzenia w kroku 1:** czy LovyanGFX 1.2.30 kompiluje się na hosta bez SDL2 (sam
  `LGFX_Sprite`; w bibliotece jest platforma `sdl`, ale sprite nie potrzebuje panelu) — na
  Windows (MinGW / MSVC z PlatformIO) i w CI (Ubuntu). Rysowanie musi przejść przez te same
  fonty (`src/fonts/lcars_efont.c`, efont U8g2) co na płytce.
- **Plan B**, jeśli LovyanGFX na hosta okaże się zbyt kłopotliwe: na płytce
  `/screenshot.bmp?screen=update-prompt` (nowa wartość w `parseScreenshotScreenArg`) rysuje
  ekran z tymi samymi danymi `update_demo.h` (wkompilowanymi), zrzuca go i wraca do normalnego
  ekranu; `tools/screenshots.py --device cyd-sky.local` pobiera i konwertuje. Wymaga płytki,
  ale dalej używa danych z testów, nie żywego GitHuba.

**Strona ustawień — `web_firmware.png`:**
- `config_page` jest czysty i renderuje się pod `native` już dziś. Test
  `test_config_page` (osobny przypadek `render_firmware_tab_demo`) zapisuje wygenerowany HTML
  zakładki Firmware z `update_demo.h` do `.pio/screenshots/web_firmware.html` (tylko gdy
  ustawione `SCREENSHOT_OUT`, żeby zwykły `pio test` nie pisał plików).
- `tools/screenshots.py` otwiera ten plik w Chromium bez okna (Playwright), szerokość 500 px,
  `device_scale_factor 2` (jak istniejące `web_*.png` — 1000 px szerokości), zrzut całej strony.
  Dwa warianty: tylko wydanie i z wersją deweloperską (`web_firmware_dev.png`, do README).
- Jedno polecenie: `python tools/screenshots.py` (uruchamia oba testy z `SCREENSHOT_OUT`,
  potem konwersję). Wymagania (`pip install pillow playwright`) opisane w nagłówku skryptu.
  Nie w CI na każdy push — zrzuty commitujemy, gdy zmienia się wygląd (jak dziś).

### 5.12 Strona projektu (`site/index.html`, README)

- Nowa sekcja „Aktualizacje” / „Updates” (pary `.en` / `.pl`, lekki ton jak reszta strony):
  1. ze strony ustawień — plik `.bin` z Releases (już działa, v1.1.0);
  2. **samo pyta** — raz na dobę sprawdza GitHuba, pyta na ekranie, TAK = ~1 min i gotowe;
     nieudana wersja sama się wycofuje;
  3. wersje deweloperskie — tylko dla odważnych, z panelu, checkbox.
  Zrzuty: `screenshots/update_prompt.png` (320×240, jak pozostałe ekrany) i
  `screenshots/web_firmware.png` (w galerii zakładek panelu obok `web_general.png` itd.).
- Galeria panelu dziś nie ma zakładki Firmware — dodać `web_firmware.png` (przy okazji
  uzupełnia brak z v1.1.0).
- Notka prywatności jednym zdaniem: urządzenie raz na dobę pyta GitHuba o numer wersji;
  można wyłączyć.
- ROADMAP → Done generuje się sam (`build_site.py`), changelog z tagów też — bez ręcznej
  edycji.
- README: sekcja „Updating” (trzy drogi), zrzut `update_prompt.png` w „Screenshots”,
  `web_firmware.png` w galerii panelu, opis tagowania wersji deweloperskiej.
- Sprawdzenie: `python tools/build_site.py` lokalnie → `_site/index.html` w przeglądarce
  PL i EN, szerokość 360 px; obrazki mają `width`/`height` i `alt` w obu językach jak reszta.

## 6. Endpointy i kody HTTP

| Ścieżka | Sytuacja | Kod |
|---|---|---|
| `POST /update/check` | sprawdzono (dowolny wynik, też błąd GitHuba) | 303 → `/?tab=firmware` |
| | „Sprawdź teraz” < 60 s od poprzedniego | 303, bez zapytania, stan bez zmian |
| | sprawdzanie wyłączone w ustawieniach | 303 — ręczne „Sprawdź teraz” **działa** mimo to |
| `POST /update/install` | `tag` == latest (lub latestPre przy włączonym checkboxie), przyjęte | 200 strona „Pobieranie…” |
| | `tag` inny / pre-release przy wyłączonym checkboxie / trwa inna aktualizacja | 409 strona z komunikatem |
| | brak sumy SHA-256 (`NoChecksum`) | 409 strona z komunikatem |
| `POST /update/prerelease` | zapisano checkbox (+ sprawdzenie) | 303 → `/?tab=firmware` |
| wszystkie | hasło ustawione, brak / złe Digest | 401 (jak reszta) |
| wszystkie | blokada `AuthThrottle` | 429 + `Retry-After` |

Wynik samej instalacji nie wraca w odpowiedzi HTTP (pobieranie trwa po wysłaniu strony):
sukces = restart i nowa wersja w stopce, błąd = komunikat `lastInstall` na zakładce Firmware.

## 7. Plan prac (kolejność, każdy krok z testami `pio test -e native`)

0. ROADMAP: nowa pozycja w „In progress” (PL w komentarzu `<!-- pl: … -->`).
1. **Weryfikacja na żywo** (bez kodu produkcyjnego, ~1 h), wynik dopisać do §10:
   - `curl -sI` / `curl -s` na `/releases/latest`: pola, `ETag`, czy jest `digest` przy
     zasobach, nagłówki limitu, `304` z `If-None-Match`, `403` bez `User-Agent`;
   - `curl -sI` na `browser_download_url`: host w `Location`, długość URL-a, ile przeskoków,
     `Content-Length`, czy `release-assets…` czy `objects…`;
   - `openssl s_client -showcerts` dla czterech hostów → lista CA głównych;
   - szkic na płytce: `WiFiClientSecure` + `setCACert` do `api.github.com` i
     `release-assets…`: czy handshake przechodzi, czas, sterta (`getMaxAllocHeap`) przed/po;
     zachowanie `HTTPClient` 2.0.17 z `HTTPC_DISABLE_FOLLOW_REDIRECTS` + `Location`;
   - `/releases?per_page=10`: rozmiar odpowiedzi, czy pre-release są na liście bez tokena;
   - LovyanGFX na hosta (§5.11): czy `LGFX_Sprite` + efont kompilują się pod `native` na
     Windows i Ubuntu — decyzja: `native-render` czy plan B (zrzut z płytki).
2. `release_check` czysta część — testy, potem implementacja (wersje z sufiksami pre-release,
   parsowanie `latest` i listy z fixture, skróty, hosty, harmonogram, bramki, pytanie);
   wspólne dane `test/fixtures/update_demo.h`.
3. `ota_update`: `OtaDigest`, `expectSha256`, `expectVersion`, nowe `OtaResult`; przeniesienie
   `UpdateWriter` / sesji z `config_portal`; `scheduleRestart()` wspólne.
4. `src/github_ca.h` + krok CI sprawdzający CA.
5. Adapter `release_check`: sprawdzenie (log, stan), potem pobranie i instalacja.
6. `config_store` / `config_backup`: trzy pola.
7. `web_i18n` + `config_page` + `config_portal` (karta, kanał deweloperski, formularze,
   endpointy).
8. `i18n` + `service_screen` + `main.cpp` (pytanie, ekran pobierania, bramki, wstrzymanie
   dotyku/auto-cycle).
9. `release.yml` / `site.yml` / `build_site.py`: pre-release dla tagów z `-`, bez publikacji
   strony i bez wpisu w changelogu dla pre-release (§5.10).
10. **Zrzuty z testów** (§5.11): `native-render` (albo plan B), przypadek renderujący w
    `test_config_page`, `tools/screenshots.py`; wygenerować `update_prompt.png`,
    `web_firmware.png`, `web_firmware_dev.png` i obejrzeć je (PL — jak pozostałe zrzuty).
11. Test na płytce (§9) z prawdziwym wydaniem: wypuścić `v1.2.0` z tą funkcją (instalowane
    ręcznie / OTA), potem `v1.2.1-rc.1` (kanał deweloperski) i `v1.2.1` (np. tylko zmiana
    changelogu) jako cele.
12. **Strona projektu** (§5.12): sekcja „Aktualizacje” PL/EN, nowe zrzuty, galeria panelu
    z zakładką Firmware; `python tools/build_site.py` i przegląd `_site/` (PL, EN, 360 px).
13. Dokumentacja: README („Updating” — „the device asks by itself”, zrzuty, tag
    deweloperski), CLAUDE.md (sekcja OTA: nowa podsekcja + `release_check` w „Code
    conventions”, `native-render` i `tools/screenshots.py` w „Commands”), ROADMAP → Done.

## 8. Decyzje właściciela (2026-10-24)

1. **Sprawdzanie domyślnie włączone** — tylko zapytanie o metadane raz na dobę (GitHub widzi
   IP i `User-Agent` z wersją), instalacja zawsze z potwierdzeniem; można wyłączyć.
2. **Pytanie na wyświetlaczu, raz na wersję** (NIE = pomiń tę wersję na wyświetlaczu).
3. **TAK na dotyku bez hasła** — fizyczny dostęp = zgoda, jak reset hasła.
4. **Weryfikowany TLS tylko dla aktualizacji**; OpenSky / hexdb.io zostają na `setInsecure()`.
5. **Brak sumy SHA-256 = brak instalacji z GitHuba** (ręczny upload dalej działa). Wydania
   z obecnego `firmware.yml` mają `SHA256SUMS.txt` (starsze — sprawdzić w kroku 1), a i tak
   instalujemy tylko najnowsze.
6. **Wersje deweloperskie instaluje tylko admin.** Panel pokazuje wydania; checkbox
   „Pokazuj wersje deweloperskie” (domyślnie wyłączony) dokłada najnowszą pre-release
   z przyciskiem instalacji (§4.3, §5.10). Wyświetlacz nigdy nie pyta o wersję deweloperską
   i nie pyta wcale, gdy taka jest zainstalowana; powrót na wydanie — z panelu („Wróć do
   wydania”).
7. **Do zrobienia w ramach tej pracy:** aktualizacja strony projektu (§5.12) i zrzut ekranu
   z pytaniem o aktualizację generowany z danych testowych (§5.11).

## 9. Kryteria akceptacji (test ręczny na płytce)

- [ ] Po starcie z `v1.2.0`, gdy na GitHubie jest `v1.2.1`: w ciągu ~2–3 min log `available`,
      na wyświetlaczu pytanie z „v1.2.0 → v1.2.1”.
- [ ] TAK → ekran „POBIERANIE Z GITHUBA” z paskiem, restart, `v1.2.1` w stopce; ustawienia,
      język, ostatni ekran, cache hexdb.io zachowane; po 30 s `marked valid`.
- [ ] NIE → po restarcie pytanie nie wraca; strona dalej pokazuje „Dostępna v1.2.1”
      z przyciskiem.
- [ ] Brak reakcji 30 s → powrót do ekranów; po restarcie pytanie wraca.
- [ ] Pytanie nie wyskakuje w trakcie odliczania „nad tobą” na ekranie TERAZ.
- [ ] „Zainstaluj” ze strony → strona „Pobieranie…”, po restarcie sama się przeładowuje.
- [ ] Najnowsza wersja zainstalowana → „Masz najnowszą wersję”, żadnego pytania.
- [ ] Kolejne „Sprawdź teraz” → w logu `304 not modified`.
- [ ] Wyłączony checkbox → brak jakichkolwiek zapytań do GitHuba w logu przez dobę (poza
      ręcznym „Sprawdź teraz”).
- [ ] Zerwanie WiFi w połowie pobierania → „AKTUALIZACJA NIEUDANA”, stary firmware działa,
      strona pokazuje błąd; ponowna próba działa.
- [ ] Podmieniona suma (test: build z celowo złym `expectSha256`) → `ChecksumMismatch`,
      brak restartu, slot nieaktywowany.
- [ ] Zły CA (test: build z pustym / cudzym CA) → `CheckFailed` z kodem TLS, nic się nie
      instaluje, ręczny upload działa.
- [ ] `isAllowedUpdateUrl` blokuje przekierowanie poza listę (test: tymczasowy URL zasobu
      na inny host — sprawdzić w logu, że nie było połączenia).
- [ ] Hasło ustawione: `/update/check` i `/update/install` bez logowania → 401.
- [ ] Sterta: największy wolny blok przed pobraniem ≥ 50 KB; po restarcie i pierwszym
      pollingu porównywalna z wersją sprzed.
- [ ] Brak WiFi / brak NTP przy starcie → brak prób (log), pierwsza próba po synchronizacji.
- [ ] Krok CI z CA zielony; z celowo złym PEM — ostrzeżenie, build przechodzi.
- [ ] Tag `v1.2.1-rc.1` → GitHub Release oznaczony jako pre-release; strona projektu
      **nie** opublikowana ponownie, instalator dalej daje `v1.2.0`, changelog bez wpisu rc.
- [ ] Checkbox „Pokazuj wersje deweloperskie” wyłączony → panel nie wspomina o rc, wyświetlacz
      milczy, `POST /update/install` z `tag=v1.2.1-rc.1` → 409.
- [ ] Checkbox włączony → panel pokazuje `v1.2.1-rc.1` z ostrzeżeniem; instalacja działa;
      wyświetlacz dalej o nic nie pyta (ani przed, ani po instalacji rc).
- [ ] Na `v1.2.1-rc.1`, po wydaniu `v1.2.1` → wyświetlacz nie pyta (zainstalowana deweloperska),
      panel proponuje `v1.2.1` (Newer); z `v1.3.0-rc.1` i najnowszym wydaniem `v1.2.1` → panel
      „Wróć do wydania v1.2.1”, działa.
- [ ] `python tools/screenshots.py` generuje `update_prompt.png` (320×240) i `web_firmware.png`
      (+ `_dev`) bez płytki (albo z płytką w planie B) i wyglądają jak na urządzeniu /
      w przeglądarce.
- [ ] Strona projektu: sekcja „Aktualizacje” PL i EN, oba zrzuty, czytelna na 360 px.
- [ ] `pio test -e native` zielone, `release_check` i zmiany w `ota_update` pokryte.

## 10. Wyniki implementacji (2026-10-24)

**Krok 1 — weryfikacja na żywo:**
- `/releases/latest`: 13,6 KB (notatki wydania odcina filtr), `ETag` jest, limit anonimowy 60/h
  (`X-RateLimit-*`). **Pole `digest` istnieje** przy każdym zasobie (`sha256:…`), więc
  `SHA256SUMS.txt` jest tylko zapasem. `/releases?per_page=10`: 88 KB, bez pre-release (żadnych
  jeszcze nie ma).
- `browser_download_url` → **302** na `release-assets.githubusercontent.com` (URL ~940 znaków,
  podpisany), jeden przeskok.
- **CA — inne niż zakładał projekt:** github.com / api.github.com = Sectigo (*Public Server
  Authentication Root E46*, krzyżowo podpisany przez *USERTrust ECC*); release-assets /
  objects.githubusercontent.com = **Let's Encrypt** (*Root YR* krzyżowo przez *ISRG Root X1*),
  nie DigiCert. `src/github_ca.h`: USERTrust ECC/RSA, Sectigo E46/R46, ISRG X1/X2 —
  `openssl -verify_return_error` OK dla wszystkich czterech hostów (ten sam krok w CI).
- LovyanGFX na PC wymaga SDL2 (albo Linux framebuffer) — na tej maszynie brak, więc zrzut
  wyświetlacza robi **plan B** (§5.11): `/screenshot.bmp?screen=3` rysuje pytanie z
  `updateDemoState()` — tych samych danych, które sprawdzają testy.

**Test na płytce** (build z wymuszonym `FW_VERSION=v1.0.9`, żeby prawdziwe v1.1.0 z GitHuba było
„nowsze” — bez publikowania czegokolwiek):
- Pierwsze sprawdzenie 2 min po starcie: `http=200 latest=v1.1.0 (installed v1.0.9)
  status=Available`, 3,2–4 s, sterta ~195–205 KB. Pytanie na wyświetlaczu, po 30 s samo znika,
  polling OpenSky działa dalej. Kolejne: `http=304` (ETag).
- Panel: zakładka Firmware z kartą (stan, data, Zainstaluj/Sprawdź), `/update/install` z obcym
  tagiem → 409, bez hasła → 401, przełącznik wersji deweloperskich w obie strony (lista
  `/releases` pobrana, `pre=` puste — brak pre-release na GitHubie).
- **Instalacja z panelu:** `installing v1.1.0` → świeże sprawdzenie → 302 → 1 360 144 B w 15 s,
  SHA-256 `f7d1a9f3…` zgodne, znacznik `v=v1.1.0` zgodny → restart → `running from app1, pending
  verify` → `v1.1.0 marked valid after 30 s` → stopka panelu `v1.1.0`. Potem z powrotem build
  bieżący (USB): `-dirty` → status „wersja deweloperska”, bez pytania na ekranie, przycisk
  instalacji tylko w panelu.

**Znalezione i naprawione na płytce:**
1. **Przepełnienie stosu `loopTask`** (Guru Meditation „Unhandled debug exception” przy starcie
   instalacji; najpewniej też jeden wcześniejszy niewyjaśniony restart w trakcie testów panelu).
   Uścisk TLS z weryfikacją łańcucha (ECDSA P-384 w mbedTLS) zjada ~7,8 KB z 8 KB domyślnego
   stosu. `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` w `main.cpp`; log `[update] … stack left` —
   zmierzone ~7,6–8,6 KB zapasu przy sprawdzeniu i instalacji.
2. **Połączenie TCP z api.github.com czasem > 5 s** (wspólne `kHttpConnectTimeoutMs`) →
   `TLS error -1`, stan „nie udało się połączyć”. Osobne `kGithubConnectTimeoutMs = 15 s`
   (+ limit uścisku 30 s) i jedna ponowna próba po braku odpowiedzi.
3. Pytanie na wyświetlaczu nie zamykało się, gdy instalację zlecono z panelu (czekało 30 s) —
   teraz zamyka się od razu i wchodzi ekran pobierania.

**Odstępstwa od projektu:**
- Zamiast `expectSha256()` / `expectVersion()`: `OtaSession::start(size, OtaExpectations{sha256,
  version})` — oczekiwania nie mogą „przeciec” do następnego ręcznego uploadu (test).
  `UpdateWriter` + sesja → `otaFlashSession()` w `ota_update`.
- `updateDemoState()` w `release_check` (nie `test/fixtures/update_demo.h`), bo z tych samych
  danych korzysta firmware (zrzut planu B). Fixture'y JSON z prawdziwych odpowiedzi GitHuba:
  `test/test_release_check/github_fixtures.h`.
- Nowy `UpdateStatus::BackToRelease` (projekt miał go tylko w `UpdateOffer`).
- Zrzuty stron: Chrome/Edge headless z linii poleceń zamiast Playwrighta (bez dodatkowych
  zależności); skrypt ponawia `pio test`, gdy Windows zablokuje świeży plik testu (WinError 4551
  — to też przyczyna sporadycznych `ERRORED` w pełnym `pio test -e native`).

**Nieprzetestowane na płytce** (wymaga człowieka / opublikowania): stuknięcie TAK / NIE na
ekranie (logika pokryta testami `acceptPromptTap`, `confirmHit`, `shouldShowUpdatePrompt`),
kanał deweloperski z prawdziwym pre-release (wymaga tagu `v…-rc.N` na GitHubie), zerwanie WiFi
w trakcie pobierania, kroki `release.yml` / CI (zadziałają przy następnym tagu / pushu; krok CA
sprawdzony lokalnie). `pio test -e native`: 638 testów zielonych + 1 pominięty (render zrzutu).
