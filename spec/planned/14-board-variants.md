# Spec 14 — Jeden firmware dla płytek R i R „2USB”: wykrywanie wyświetlacza przy starcie

Źródło: ROADMAP „What's next” (dawne #2 „osobne buildy na warianty” i #6 „wykrywanie przy
starcie”, połączone 2026-11-01). Status: **planowane**.

Pod nazwą ESP32-2432S028R sprzedawane są dwie płytki, które z wierzchu różnią się tylko
złączami:

| Płytka | Jak ją poznać | Wyświetlacz | Dotyk | Dziś |
|---|---|---|---|---|
| R, klasyczna | jedno micro-USB | ILI9341 | XPT2046 (SPI) | działa |
| R „2USB” | micro-USB + USB-C | **ST7789** | XPT2046, te same piny | negatyw: firmware wysyła panelowi sekwencję ILI9341 |

Właściciel ma obie płytki, więc obie da się sprawdzić na sprzęcie.

Cel: **jeden plik firmware, który sam rozpoznaje płytkę przy starcie**, bez żadnego wyboru po
stronie użytkownika. Ta sama nazwa pliku w wydaniu, ten sam instalator, te same aktualizacje.

Rozważany wariant „osobne środowisko PlatformIO i plik na płytkę” odrzucony: wymagałby macierzy
buildów w CI, wyboru płytki w instalatorze i kontroli „plik dla innej płytki” przy OTA. Wszystko
to znika, gdy plik jest jeden (docs/history.md).

## 1. Wykrywanie

Linia MISO wyświetlacza (pin 12) jest podłączona, bo z niej czyta zrzut ekranu
(`readable = true`). Przy starcie firmware pyta więc sterownik panelu o identyfikator:

1. `tft.init()` z konfiguracją ILI9341, tak jak dziś.
2. Odczyt `RDDID` (komenda `0x04`, 3 bajty po bajcie pustym) przez `tft.readCommand()`.
3. Czysta funkcja decyduje: identyfikator ST7789 (pierwszy bajt, czyli producent, `0x85`) →
   płytka 2USB. **Wszystko inne** → klasyczna R: ILI9341, `00 00 00`, `FF FF FF`, odczyt
   nieudany, nieznany panel.
4. Dla 2USB: `setPanel()` na panel ST7789 i ponowne `init()`.

- Jedna decyzja na start, bez zapisu do NVS: odczyt trwa ułamek milisekundy, a płytka nie
  zmienia się w trakcie pracy.
- Błędne rozpoznanie nie odcina urządzenia. W najgorszym razie kolory wychodzą jak negatyw, ale
  WiFi, strona ustawień i OTA działają, więc da się wgrać poprawkę.
- Surowy identyfikator idzie do logu przy starcie (`[board] RDDID=858552 -> cyd-2usb`). Na obu
  płytkach trzeba go zapisać w docs: to jest dowód, że wykrywanie działa, a nie tylko się
  kompiluje.
- **Plan B**, tylko jeśli sprawdzenie na płytce pokaże, że 2USB nie odpowiada na `0x04` (np.
  niepodłączone MISO): spróbować `0xD3` (ILI9341 odpowiada `93 41`, a brak tej odpowiedzi
  oznacza „nie ILI9341”). Dopiero gdy i to zawiedzie, ustawienie „Wyświetlacz:
  auto / ILI9341 / ST7789” w panelu. Nie implementować z góry.

## 2. Kod

- **Czysta logika** (`src/display/core/board_variant.{h,cpp}`, warstwy wg spec 12):
  - `enum class BoardVariant { ClassicR, R2Usb }`;
  - `BoardVariant boardFromPanelId(uint32_t rddid)`: decyzja z p. 1.3;
  - `const char *boardVariantName(BoardVariant)`: `"cyd-r"` / `"cyd-2usb"`, do logu i panelu;
  - kalibracja dotyku dla danej płytki: na 2USB mierzona z logu `[touch]`; jeśli zgadza się z R,
    to jeden zestaw.
- **Adapter** (`src/display/adapters/lgfx/LGFX_CYD.hpp`): w klasie `LGFX` oba panele (`Panel_ILI9341`,
  `Panel_ST7789`) i metoda `beginDetected()` z krokami 1–4. Piny SPI, podświetlenie (21) i
  XPT2046 bez zmian i wspólne dla obu paneli. Dla ST7789 `invert` i `rgb_order` ustawione na
  płytce; oczekiwane `invert = true`. Kryterium: kolory motywu LCARS takie jak na R
  (porównać z `docs/screenshots/`). Konfiguracja jest w `src/display/adapters/lgfx/` (przeniesiona
  w etapie 4 spec 12).
- `main.cpp` (`setup()`) woła `tft.beginDetected()` zamiast `tft.init()`. Wynik trafia do
  `core_link` **przed** startem `net_task`, bo zapis przed utworzeniem zadania jest widoczny dla
  rdzenia 0 bez blokady.
- Panel ustawień, stopka: `v1.12.0 · cyd-2usb`. `/perf.json`: pole `board`.
- `FW_BOARD` / marker `board=cyd-r`, nazwa pliku w wydaniu i `releaseAssetName()` **bez
  zmian**. To nadal jeden firmware dla „rodziny R”, a płytki z v1.11 dalej znajdą swój plik.
- Rozmiar: drugi sterownik panelu to kilka KB; firmware musi się dalej mieścić w slocie OTA
  (1.875 MB).

## 3. GitHub Actions, instalator, aktualizacje

- **Workflowy bez zmian logiki.** Jeden build `cyd`, jeden artefakt, jeden plik w wydaniu.
  `firmware.yml` dalej sprawdza marker. Tekst w opisie wydania deweloperskiego (`pr.yml`) i
  w notce wydania: „R i R 2USB”.
- **Aktualizacje** (z GitHuba, ręczny upload, `cyd-ota`) dostają ten sam plik na obie płytki,
  więc nie ma czego pomylić. Kontrola `board=` z markera (już parsowana przez `OtaSession`)
  zostaje na przyszłość, gdy pojawi się płytka wymagająca osobnego pliku.
- **Instalator i README:** „R albo R 2USB, firmware sam rozpozna wyświetlacz” zamiast „jeśli
  kolory są odwrócone, możesz mieć inny wariant”. Płytka C (dotyk pojemnościowy) nadal
  nieobsługiwana, z wyraźną adnotacją.

## 4. Testy

- `test_display/board_variant_test.cpp`:
  - `0x858552` (ST7789) → `R2Usb`;
  - identyfikator odczytany z klasycznej R (wartość z logu `[board]`) → `ClassicR`;
  - `0x000000` (brak odpowiedzi, linia ściągnięta w dół) → `ClassicR`;
  - `0xFFFFFF` (linia wisi wysoko, brak odpowiedzi) → `ClassicR`;
  - inny nieznany identyfikator → `ClassicR`;
  - nazwy płytek;
  - kalibracja dotyku dla obu wariantów przez `mapRawTouch()`, jeśli się różnią.
  
  Identyfikatory w testach są te **zmierzone na płytkach** (log `[board]`), nie tylko te z
  dokumentacji sterowników.
- `test_admin`: stopka strony i `/perf.json` zawierają nazwę płytki.
- `@manual` na obu płytkach:
  - 2USB: kolory jak na R, dotyk przy krawędziach, zrzut ekranu (`readRect` na ST7789), jasność
    i tryb nocny;
  - R: zachowanie bez zmian, aktualizacja z v1.11 przez GitHuba;
  - obie: log `[board]` z identyfikatorem.

## 5. Dokumentacja

- CLAUDE.md „Hardware”: dwie płytki, wykrywanie, `beginDetected()`.
- `docs/screens.md` (kalibracja, jeśli osobna), `docs/architecture.md` (start, `core_link`,
  `/perf.json`).
- `docs/history.md`: dlaczego wykrywanie, a nie osobne buildy.
- README i strona projektu.
- ROADMAP: pozycja przechodzi do „Done” z wersją wydania (placeholder wersji jak zwykle).

## Poza zakresem

- Płytka C (dotyk pojemnościowy CST820 na I2C): osobna pozycja ROADMAP. Wykrywanie dałoby się
  dopisać (próba adresu `0x15` przed startem XPT2046, podświetlenie na 27 zamiast 21), ale
  wymaga płytki do sprawdzenia pinów.
- Konfigurowalna nazwa mDNS: dwie płytki w jednej sieci dalej obie ogłaszają `cyd-sky.local`.
- Inne rozmiary CYD (2.4", 3.5").
