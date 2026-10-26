# Spec 11 — Porządki: martwy kod, nieaktualne komentarze, dokumentacja

Z przeglądu kodu 2026-10-25, sekcja 2. Status: **w trakcie**. Robić **przed** spec 12
(reorganizacja) — mniej plików do przenoszenia i mniej fałszywych zależności.

Zasada z CLAUDE.md (przypadek `computeStardate()`): nieużywany kod usuwa się razem z testami,
a nie zostawia „na później”. Historia jest w gicie.

## 1. Martwy kod do usunięcia

Sprawdzone grepem 2026-10-25 (przed usunięciem sprawdzić ponownie — `grep -rn <nazwa> src test`):

| Co | Gdzie | Stan |
|---|---|---|
| `viewToggleButtonBounds()`, `drawViewToggleButton()` | `lcars_theme` | przycisk przełączania widoku z wersji 2-ekranowej; tylko definicja + test |
| `drawElbowFrame()`, `elbowArcPoint()`, `ElbowArcPoint` | `lcars_theme` | ramka usunięta na prośbę; CLAUDE.md każe ją zostawić jako „unused primitive” — **decyzja: usunąć**, przywrócić z gita w razie potrzeby |
| `drawVerticalDivider()` | `lcars_theme` | separator ekranu 3 usunięty na prośbę |
| `drawPanel()` | `lcars_theme` | nieużywany |
| `phaseIcon(Phase) -> char`, `formatSummaryIdentity()` | `lcars_theme`, `aircraft_summary` | ASCII `^ v > -` sprzed ikon pikselowych; `formatSummaryIdentity` woła tylko test, ekrany używają `fitSummaryIdentity()` |
| `LCARS_MAGENTA`, `LCARS_CYAN`, `LCARS_AMBER`, `LCARS_ROSE`, `LCARS_PALE_BLUE` | `lcars_theme.h` | nierysowane; tylko definicja + test palety |
| `LCARS_ORANGE` | `lcars_theme.h` | **zostaje** — spec 08 używa go jako `THEME_WARNING` |
| `NetCommandType::RefreshNow` | `core_link` | nigdy nie wysyłane; domyślną wartość `NetCommand::type` zmienić na nowe `None` |
| komentarz o `kMaxNewLookupsPerPoll` | `lookup_budget.h:24` | stała już nie istnieje, komentarz opisuje historię — skrócić |

`flight_screen.h` ma `uint16_t color = LCARS_CYAN` jako domyślną wartość pola — zamienić na rolę
z motywu (`THEME_CONTENT_SECONDARY`), zanim usunie się stałą.

## 2. Nieaktualne komentarze

- ~20 komentarzy mówi „main.cpp reloads / main.cpp keeps / main.cpp compares…”, a od spec 04 robi
  to `net_task`: `config_portal.cpp` (407, 613, 750), `config_portal.h` (84, 89, 125, 129),
  `aircraft_lookup.h:78`, `route_lookup.h:96`, `opensky_client.h:132`, `status_bar.h:42`,
  `table_view.h:55`, `lookup_budget.h:15,25`, `cpa_predictor.h:48`, `release_check.h:146`.
  Każdy poprawić na właściwy moduł albo usunąć, jeśli opisuje historię, a nie stan.
- `opensky_client.cpp`: „loopTask's stack is only 8KB” — kod działa w `net_task` (16 KB).
- Opisy ramek/separatorów/kolorów w nagłówkach `featured_panel.h`, `flight_screen.h`,
  `radar_view.h`, `status_bar.h` (np. „inside an LCARS elbow frame (LCARS_MAGENTA)”,
  „LCARS_ORANGE vertical divider”, współrzędne `y:28..78` z czasu 25 px nagłówka) —
  przepisać na obecny układ albo odesłać do CLAUDE.md, zamiast powtarzać liczby.
- Skrypt kontrolny (opcjonalny, w CI): `tools/check_comments.py` szuka `main.cpp` w komentarzach
  poza `main.cpp` i nazw usuniętych symboli — ostrzega, nie blokuje.

## 3. Dokumentacja

CLAUDE.md ma ~700 linii i miesza trzy rzeczy: zasady pracy, specyfikację ekranów i historię
(„pierwsza implementacja zrobiła X”). Fragmenty sobie przeczą (nagłówek 25 px i 15 px,
„orange block” w opisie `status_bar`, stary spec Screen 2 obok „Current behaviour”,
`rssiToBars()` opisane jako istniejące — **w kodzie go nie ma**).

Nowy podział:

| Plik | Zawartość |
|---|---|
| `CLAUDE.md` | projekt w 5 zdaniach, sprzęt (pinout), zasady (testy, moduły, redraw, i18n, dwa rdzenie — reguły, nie opis), komendy, linki do reszty. Cel: < 250 linii. |
| `docs/screens.md` | **obecny** wygląd i zachowanie ekranów 1–3, nagłówka, paska zakładek (jedna wersja, bez „było/jest”) |
| `docs/data-sources.md` | OpenSky (OAuth, limity, kredyty), hexdb.io (endpointy, cache, budżety) |
| `docs/architecture.md` | po spec 12: konteksty, warstwy, reguły zależności, słownik pojęć |
| `docs/history.md` | decyzje i ich powody („dlaczego nie trójkąty”, „dlaczego HTTP/1.0 dla /states/all”) — z obecnych akapitów historycznych |
| `docs/review-notes-2026.md` | obecny `review-notes.md` z katalogu głównego |

- `rssiToBars()`: usunąć wzmiankę z CLAUDE.md i `status_bar.h` (nie ma implementacji ani miejsca
  w układzie).
- `test/README` (szablon PlatformIO) → zastępuje go `test/README.md` ze spec 07.
- `include/README`, `lib/README` (szablony PlatformIO) — usunąć albo zastąpić jednym zdaniem.

## 4. Kryteria akceptacji

1. `pio run` i `pio test -e native` zielone; rozmiar firmware'u nie rośnie.
2. `grep` każdego symbolu z §1 (poza `LCARS_ORANGE`) w `src/` i `test/` → pusto.
3. Żaden komentarz poza `main.cpp` nie przypisuje `main.cpp` pracy, którą robi `net_task`.
4. CLAUDE.md < 250 linii, każdy usunięty fragment jest w jednym z plików `docs/` (albo był
   nieaktualny — wtedy wymieniony w opisie commita).
