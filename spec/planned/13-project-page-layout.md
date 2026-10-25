# Spec 13 — Strona projektu: historia zmian, zrzuty panelu, opis funkcji

Z przeglądu strony (https://alasota.github.io/CYD-Flight-Tracker/) 2026-10-25. Status: **w trakcie**.
Dotyczy tylko strony projektu (`site/`, `tools/build_site.py`) — firmware bez zmian,
żadnego wpływu na płytkę.

## Problem

Zmierzone na zbudowanej stronie (`python tools/build_site.py --out _site`, headless Edge, szerokość
1280 px): strona ma **~12 300 px** wysokości, z czego ok. 5 000 px to trzy fragmenty, które
czyta się źle:

1. **Historia zmian** (~1 500 px): 20 kart, po jednej na każdy tag `v*`. Większość ma 1–2 zmiany
   i tę samą datę (wszystkie wydania powstały w 8 dni). Treść to surowe tematy commitów pisane
   czcionką o stałej szerokości (`/release: stale docs trigger…`, `Tests: readable assertions in
   test_admin (spec 07 step 3)`), więc użytkownik nie dowie się z nich, co się zmieniło na
   wyświetlaczu. To samo jest już opisane ludzkim językiem w tabeli „Done” w `ROADMAP.md` („Done
   in v1.6.0: …”), więc ta informacja pojawia się na stronie dwa razy.
2. **Zrzuty panelu ustawień** (~900 px): 6 obrazków w siatce 5-kolumnowej (`.web-shots`), więc
   szósty spada sam do drugiego rzędu. Wysokości różnią się od 1 424 do 4 512 px, co daje
   schodki. Przy 1/5 szerokości tekstu na nich nie da się przeczytać. Nie mają podpisów, nie da się
   ich powiększyć i stoją pod blokiem „ustaw hasło”, bez związku z treścią obok.
3. **Bloki „Nowość:”** w sekcji „Pierwsze uruchomienie” (~2 000 px): pod 3 krokami konfiguracji
   stoi 6 długich kart prozy („!”, aktualizacje, jasność/noc, dwa rdzenie, kopia, hasło). Do
   konfiguracji pasują tylko kopia i hasło. Etykieta „Nowość” się starzeje: dwa rdzenie weszły w
   v1.4.0. Treść karty „dwa rdzenie” jest już w „Dla dociekliwych”.

Cel: strona **≤ 9 500 px** przy 1280 px (ten sam pomiar), bez utraty treści — długie opisy
zostają, tylko schowane pod „Więcej”.

## 1. Historia zmian: grupy po wersji minor, opis z roadmapy

Zmiana w `tools/build_site.py` (`releases()`, `changelog_html()`), generowane, więc nie rozjedzie
się przy kolejnych wydaniach.

### 1.1 Grupowanie

- Jeden wpis na wersję **minor**: `v1.6` = v1.6.0 … v1.6.4. Nagłówek: `v1.6` + zakres
  (`v1.6.0 – v1.6.4`, przy jednym wydaniu tylko jego numer) + data najnowszego wydania w grupie +
  liczba zmian.
- Wersje deweloperskie (tag z `-`) jak dziś: bez własnego wpisu, ich commity trafiają do
  następnego wydania.
- Kolejność: najnowsza grupa pierwsza. Linki „wydanie” (najnowszy tag grupy) i „różnice”
  (`compare/<ostatni tag poprzedniej grupy>...<najnowszy tag grupy>`).

### 1.2 Opis z roadmapy

- `build_site.py` już parsuje `ROADMAP.md`. Nowa czysta funkcja zbiera z tabeli „Done” wiersze,
  których Werdykt zaczyna się od `Done in vX.Y.Z` (PL: `Zrobione w vX.Y.Z`), i przypisuje je do
  grupy `vX.Y`.
- Grupa z takimi wierszami pokazuje najpierw **listę ich kolumny „Item”** (PL/EN, zwykłą
  czcionką). To jest „co się zmieniło” dla użytkownika. Na dziś: v1.6 → 1 pozycja, v1.5 → 1,
  v1.4 → 3, v1.3, v1.2, v1.1 → po 1.
- Grupa bez wierszy w roadmapie (np. v1.0) pokazuje tylko commity, jak dziś.
- Werdykt z roadmapy **nie** jest kopiowany (za długi). Pozycja linkuje do `#roadmap`.

### 1.3 Commity schowane

- Lista commitów grupy jest w zagnieżdżonym `<details>` „Commity (N)” / „Commits (N)”, domyślnie
  zwiniętym, nadal monospace'em, nadal z limitem `CHANGELOG_MAX_ITEMS` i linkiem „…i jeszcze N”.
- Commity „zza kulis” (testy, dokumentacja, spec/plan, harness, release) są w tej liście
  wyszarzone (`li.chore`), a nie ukryte. Rozpoznaje je jedna stała regex w `build_site.py`, na
  start:
  `^(Tests?\b|Docs?\b|Plan and BDD|Specs? ?\d|Spec/|/release|/fix|Release plan|Harness|Python bytecode|fix$)`.
  Liczba w nagłówku grupy liczy wszystkie commity, jak dziś.
- Intro sekcji (żart o code review) zostaje.

### 1.4 Ile otwarte

- Otwarte są **2 najnowsze grupy** (ich lista pozycji z roadmapy), reszta zwinięta do jednej
  linii: `v1.2 · 24.10.2026 · Urządzenie samo sprawdza nowe wersje…` (pierwsza pozycja z
  roadmapy, przycięta CSS-em `text-overflow: ellipsis`, albo „zmian: N”, gdy brak pozycji).
- Szacunek: ~7 wpisów zamiast 20, ok. 600 px zamiast 1 500.

### 1.5 Testy

- Nowy `tools/test_build_site.py` (unittest, w stylu `tools/test_release_plan.py`), dopisany do
  kroku CI w `firmware.yml` obok `test_release_plan.py`. Logika git zostaje w `releases()`.
  Grupowanie, przypisanie roadmapy i klasyfikacja commitów to czyste funkcje na listach
  (`group_by_minor(rels)`, `roadmap_done_by_version(sections)`, `is_chore(subject)`), testowane
  bez gita.
- Przypadki: patche i wersje deweloperskie w jednej grupie; „Done in v1.4.0/1.4.2/1.4.3” → jedna
  grupa v1.4 z 3 pozycjami w kolejności wersji; grupa bez roadmapy; `vNEXT` w roadmapie (jeszcze
  niewydane) nie trafia do żadnej grupy; `v1.10` po `v1.9` (sortowanie wersji, nie
  alfabetyczne); każdy przykładowy commit z listy w §1.3 ma właściwą klasyfikację; tekst
  escapowany (`<`, `&` w temacie commita).

## 2. Zrzuty panelu ustawień: przeglądarka z zakładkami

Zmiana w `site/index.html`, `site/style.css`, `site/site.js`. Pliki PNG bez zmian.

- Zamiast `.web-shots` (siatka 5 kolumn): blok **„Panel ustawień”** z rzędem przycisków
  odwzorowujących zakładki panelu: Lokalizacja · OpenSky · Ogólne · Kopia · Firmware · „!”
  (EN: Location · OpenSky · General · Backup · Firmware · „!”). Pod nimi **jeden** zrzut naraz
  i podpis 1–2 zdania PL/EN: co na tej zakładce ustawisz.
- Pliki: `web_location`, `web_opensky`, `web_general`, `web_backup`, `web_firmware`, `web_alert`
  (ostatni pod „!”: ramka „Wymaga uwagi”). `web_firmware_dev.png` — nie pokazujemy (szczegół).
- **Ramka o stałej wysokości**: `max-height: 640px` (desktop), `overflow-y: auto`, zrzut w
  pełnej szerokości ramki (max ~520 px, czyli tekst czytelny). Długa zakładka Ogólne przewija
  się wewnątrz ramki zamiast wydłużać stronę. Klik w obrazek otwiera pełny PNG w nowej karcie.
- Desktop (≥ 860 px): ramka po lewej, podpis po prawej. Wąski ekran: podpis pod ramką,
  przyciski zawijają się, ramka `max-height: 70vh`.
- Dostępność: `role="tablist"` / `role="tab"` / `role="tabpanel"`, `aria-selected`, strzałki
  lewo/prawo przełączają zakładki. Kolory jak reszta strony (aktywny przycisk peach `--accent`).
- **Bez JS**: wszystkie 6 zrzutów widocznych jeden pod drugim z podpisami (JS dodaje klasę na
  `<html>` i dopiero wtedy chowa nieaktywne). Obrazki `loading="lazy"`, `width`/`height`
  zostają.
- Miejsce: na końcu nowej sekcji „Co potrafi” (§3), z linkiem w menu górnym „Panel” / „Panel”
  (`#panel`).

## 3. „Nowości” → sekcja „Co potrafi”

- Nowa sekcja `#features` po „Trzy ekrany” (przed „Instalacja”): tytuł EN „What else it does”,
  PL „Co jeszcze potrafi”. Siatka **2×2** kafelków (na wąskim ekranie 1 kolumna):
  1. „!”, gdy coś wymaga uwagi (miniatura `alert_screen.png`),
  2. Sam pyta o aktualizację (miniatura `update_prompt.png`),
  3. Przyciemnia się i zna zachód słońca,
  4. Kopia zapasowa ustawień.
- Kafelek: tytuł **bez** „Nowość:”, 2 zdania streszczenia, miniatura (jeśli jest, 320×240,
  `image-rendering: pixelated`, jak zrzuty ekranów) i `<details>` „Więcej” / „More” z **pełnym
  dotychczasowym tekstem** (PL i EN bez skracania, tylko przeniesione). Ton strony (lekki,
  z humorem) zostaje.
- Karta „Nowość: ekran już nie czeka na internet” — **usunięta**. Jej sedno jest już punktem w
  „Dla dociekliwych”. Dopisać tam tylko liczbę („najdłuższa przerwa ~0,1 s zamiast do 5,7 s”)
  i `cyd-sky.local/perf.json`.
- Sekcja „Pierwsze uruchomienie” zostaje z: 3 krokami + karta „Ustaw hasło do panelu
  (zalecane)” (bez zmian treści). Kopia zapasowa przechodzi do kafelka 4.
- Kotwice: `id="night-mode"` przechodzi na kafelek 3 (mógł być linkowany z zewnątrz).
  `id="dual-core"` znika (nic w repo go nie linkuje — sprawdzone grepem).
- Menu górne: dodać „Funkcje” / „Features” (`#features`) i „Panel” (`#panel`). Na wąskim ekranie
  menu jest i tak ukryte (bez zmian).

## 4. Roadmapa: tabela „Done” krótsza

Po §1 opis zrobionych pozycji jest w historii zmian. Sekcja „Done” w `roadmap_html()` pokazuje
tylko kolumny `# | Item | Verdict-wersja`: zamiast pełnego Werdyktu sam numer wersji
(„v1.6.0”) jako link do `#changelog`. „Planned” / „In progress” bez zmian. `ROADMAP.md` się nie
zmienia (pełny werdykt zostaje na GitHubie). Test w `test_build_site.py`: w sekcji Done brak
długiego werdyktu, jest wersja. Poza Done werdykt jest pełny.

## 5. Poza zakresem

- Zmiana wyglądu/kolorów strony, hero, FAQ, instalatora.
- Nowe zrzuty (`tools/screenshots.py`) i przycinanie istniejących PNG.
- Przepisywanie tematów commitów albo ręczny CHANGELOG.md. Źródłem dalej są git + ROADMAP.md.
- Tłumaczenie tematów commitów (jak dziś: pokazywane tak, jak są).

## 6. Kryteria akceptacji

1. `python tools/test_build_site.py`, `python tools/test_release_plan.py`,
   `python tools/check_bdd.py` zielone. `test_build_site.py` uruchamiany w CI (`firmware.yml`).
2. `python tools/build_site.py --out _site` działa na pełnej historii tagów. Historia zmian ma
   jeden wpis na wersję minor, wpisy z roadmapy pokazują pozycje „Item”, commity są zwinięte.
3. Wysokość strony przy 1280 px ≤ 9 500 px (pomiar jak w „Problem”). Zrzut całej strony
   przed/po w opisie PR.
4. Przy 375 px szerokości: brak poziomego przewijania strony, zakładki panelu się zawijają,
   ramka zrzutu przewija się w środku.
5. Bez JS (wyłączony w przeglądarce) widać wszystkie zrzuty panelu i pełną treść „Więcej”
   (`<details>` działa bez JS).
6. Przełącznik PL/EN działa we wszystkich nowych elementach (każdy tekst jako para
   `.en` / `.pl`). Żaden tekst z dotychczasowych kart „Nowość” nie zginął (jest w `<details>`
   albo w „Dla dociekliwych”).
7. CLAUDE.md, sekcja „Versioning & CI” → „Project page”: opis grupowania historii zmian
   (minor + pozycje z „Done in vX”) i nowego `test_build_site.py`.

## 7. BDD

`13-project-page-layout.feature` pisze `spec-analyst`. Uwaga: `tools/check_bdd.py` przyjmuje
dziś tylko `@test=<suite>/<plik>.cpp:<funkcja>`. Scenariusze dla `build_site.py` będą
testowane w Pythonie. **Decyzja właściciela przed implementacją:**
- (a) rozszerzyć `check_bdd.py` o `@test=tools/<plik>.py:<Klasa>.<metoda>` (z testem w
  `tools/test_check_bdd.py`) — polecane, przyda się też dla `release_plan`;
- (b) oznaczyć te scenariusze `@manual` i polegać na `test_build_site.py` w CI.

Wygląd (§2, §3, kryteria 3–5) jest i tak `@manual`: sprawdzany na zbudowanej stronie w
przeglądarce, ze zrzutami w PR.
