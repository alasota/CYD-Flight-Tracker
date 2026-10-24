# Roadmap

What might come next. A hobby project, so no dates, no sprints, no burndown charts. But a
roadmap still needs managing, so every item is rated, and the rows are in priority order.

This is not a democracy. The main stakeholder decides. Friends at the grill may submit
feedback; it will be carefully reviewed and put in the backlog.

<!--
How this file is used: tools/build_site.py turns it into the "What's next" section of the
project page (site/). Keep the format:
  "## Planned" / "## In progress" / "## Done" as section headings, each followed by a table
  with the columns below, rows in priority order.
  Item and Verdict cells: English text, then an HTML comment starting with "pl:" holding the
  Polish text (hidden on GitHub, shown on the page in Polish).
  Value: high / medium / low. Effort: small / medium / large (or two joined by "/" or "–").
  Risk: low / medium / high, or "—". Rating: 1-5 stars (⭐). The page translates these words.
-->

## Planned

| # | Item | Value | Effort | Risk | Rating | Verdict |
|---|---|---|---|---|---|---|
| 1 | Use the ESP32's second core: networking / API on core 0, TFT drawing and touch on core 1 <!-- pl: Wykorzystanie drugiego rdzenia ESP32: sieć / API na rdzeniu 0, ekran TFT i dotyk na rdzeniu 1 --> | high | medium–large | medium | ⭐⭐⭐⭐ | Fixes the 1–3 s UI freezes during HTTPS requests (and the 3–4 s daily GitHub check); worth doing before more data sources. <!-- pl: Usuwa zamrożenia interfejsu na 1–3 s podczas zapytań HTTPS (i 3–4 s codziennego sprawdzania GitHuba); warto przed kolejnymi źródłami danych. --> |
| 2 | Builds for other CYD variants: one PlatformIO env per board (classic R = ILI9341, R "2USB" = ST7789 with inverted colours, C = capacitive CST820 touch), #if in LGFX_CYD.hpp, a CI build matrix, a board picker in the web installer, and an OTA check that the file matches the board <!-- pl: Wersje dla innych wariantów CYD: osobne środowisko PlatformIO na płytkę (klasyczny R = ILI9341, R „2USB” = ST7789 z odwróconymi kolorami, C = pojemnościowy dotyk CST820), #if w LGFX_CYD.hpp, macierz buildów w CI, wybór płytki w instalatorze na stronie i sprawdzanie przy OTA, czy plik pasuje do płytki --> | high | medium | medium | ⭐⭐⭐⭐ | Fixes "colours inverted / touch does nothing" for anyone else who builds one. Start with 2USB (a small change); C waits until someone with that board confirms the touch pins. The update check already picks the file by board name. <!-- pl: Rozwiązuje „odwrócone kolory / dotyk nie działa” dla każdego, kto zbuduje sobie taki wyświetlacz. Najpierw 2USB (mała zmiana); C czeka, aż ktoś z tą płytką potwierdzi piny dotyku. Sprawdzanie aktualizacji już wybiera plik po nazwie płytki. --> |
| 3 | Support for your own local ADS-B feeder (waiting for an RTL-SDR receiver) <!-- pl: Wsparcie dla własnego lokalnego feedera ADS-B (czekam na odbiornik RTL-SDR) --> | high | medium | low | ⭐⭐⭐⭐ | When the RTL-SDR arrives: no credits, ~1 s updates. Build a data-source layer first. <!-- pl: Gdy przyjdzie RTL-SDR: zero kredytów, odświeżanie co ~1 s. Najpierw warstwa źródeł danych. --> |
| 4 | Support for alternative APIs (ADS-B Exchange / FlightRadar24 / FlightAware) <!-- pl: Wsparcie dla alternatywnych API (ADS-B Exchange / FlightRadar24 / FlightAware) --> | medium | medium | medium | ⭐⭐⭐ | Together with the feeder, on the same data-source layer. FR24 and FlightAware are paid or restrictive. <!-- pl: Razem z feederem, na tej samej warstwie źródeł danych. FR24 i FlightAware są płatne albo restrykcyjne. --> |
| 5 | Home Assistant integration (MQTT) <!-- pl: Integracja z Home Assistant (MQTT) --> | medium | medium | low | ⭐⭐⭐ | Nice automations ("plane overhead in 10 s"), but it's out of the MVP scope for now. <!-- pl: Fajne automatyzacje („samolot nad domem za 10 s”), ale na razie poza zakresem MVP. --> |
| 6 | One firmware for every CYD variant: detect the board at boot (display controller ID, CST820 on I2C 0x15) <!-- pl: Jeden firmware na wszystkie warianty CYD: rozpoznanie płytki przy starcie (ID sterownika wyświetlacza, CST820 na I2C 0x15) --> | medium | medium | high | ⭐⭐ | Nicer for users (no picking the right file), but can't be tested without every board on the desk. Only after the per-variant builds work. <!-- pl: Wygodniejsze dla użytkownika (nie trzeba wybierać pliku), ale nie da się tego przetestować bez każdej płytki na biurku. Dopiero gdy działają osobne buildy na warianty. --> |
| 7 | Home location from the network (IP geolocation) or an external GPS module <!-- pl: Pobieranie pozycji GPS z sieci (IP Geolocation) lub z zewnętrznego modułu --> | low | small / large | — | ⭐⭐ | IP location is off by kilometres, GPS is overkill indoors. Better: the browser's geolocation via the project page. <!-- pl: Lokalizacja z IP myli się o kilometry, GPS w domu to przesada. Lepiej: geolokalizacja przeglądarki przez stronę projektu. --> |

## Done

| # | Item | Value | Effort | Risk | Rating | Verdict |
|---|---|---|---|---|---|---|
| 3 | Automatic brightness and night mode <!-- pl: Automatyczna zmiana jasności i tryb nocny --> | medium | small | low | ⭐⭐⭐⭐ | Done in v1.3.0: the light sensor drives the backlight, the night runs from sunset to sunrise (worked out on the board) or by the clock, 0 % = dark until a tap, and OpenSky requests can pause at night. <!-- pl: Zrobione w v1.3.0: czujnik światła steruje podświetleniem, noc trwa od zachodu do wschodu słońca (liczone na płytce) albo według godzin, 0 % = ciemno do dotknięcia, a zapytania do OpenSky mogą w nocy czekać. --> |
| 2 | The device checks GitHub for new versions and asks on the screen ("new version v1.3.0 — install it?"); development versions for the admin only <!-- pl: Urządzenie samo sprawdza nowe wersje na GitHubie i pyta na ekranie („nowa wersja v1.3.0 — zainstalować?”); wersje deweloperskie tylko dla admina --> | high | medium | medium | ⭐⭐⭐⭐ | Done in v1.2.0: verified HTTPS to GitHub, SHA-256 and version checked before switching, never installs without a YES. Pre-releases only on the settings page, behind a checkbox. <!-- pl: Zrobione w v1.2.0: zweryfikowane HTTPS do GitHuba, SHA-256 i wersja sprawdzone przed przełączeniem, nic bez TAK. Wersje testowe tylko na stronie ustawień, za checkboxem. --> |
| 1 | Over-the-air (OTA) firmware updates, plus a password for the settings page <!-- pl: Aktualizacje OTA (Over-The-Air Update) i hasło do panelu ustawień --> | high | small | low | ⭐⭐⭐⭐⭐ | Done in v1.1.0: the Firmware tab takes the release .bin, a bad build rolls itself back, the settings page can have a password. <!-- pl: Zrobione w v1.1.0: zakładka Firmware przyjmuje plik .bin z wydania, wadliwa wersja sama się wycofuje, panel ustawień może mieć hasło. --> |
