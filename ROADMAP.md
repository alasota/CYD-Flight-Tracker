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
| 1 | Over-the-air (OTA) firmware updates <!-- pl: Aktualizacje OTA (Over-The-Air Update) --> | high | small | low | ⭐⭐⭐⭐⭐ | Do it first: the two OTA slots are already there, only the code is missing. <!-- pl: Zrobić najpierw: dwa sloty OTA już są, brakuje tylko kodu. --> |
| 2 | Automatic brightness and night mode <!-- pl: Automatyczna zmiana jasności i tryb nocny --> | medium | small | low | ⭐⭐⭐⭐ | Quick win: the CYD has a light sensor on GPIO34 and a PWM backlight. <!-- pl: Szybki efekt: CYD ma czujnik światła na GPIO34 i podświetlenie z PWM. --> |
| 3 | Use the ESP32's second core: networking / API on core 0, TFT drawing and touch on core 1 <!-- pl: Wykorzystanie drugiego rdzenia ESP32: sieć / API na rdzeniu 0, ekran TFT i dotyk na rdzeniu 1 --> | high | medium–large | medium | ⭐⭐⭐⭐ | Fixes the 1–3 s UI freezes during HTTPS requests; worth doing before more data sources. <!-- pl: Usuwa zamrożenia interfejsu na 1–3 s podczas zapytań HTTPS; warto przed kolejnymi źródłami danych. --> |
| 4 | Support for your own local ADS-B feeder (waiting for an RTL-SDR receiver) <!-- pl: Wsparcie dla własnego lokalnego feedera ADS-B (czekam na odbiornik RTL-SDR) --> | high | medium | low | ⭐⭐⭐⭐ | When the RTL-SDR arrives: no credits, ~1 s updates. Build a data-source layer first. <!-- pl: Gdy przyjdzie RTL-SDR: zero kredytów, odświeżanie co ~1 s. Najpierw warstwa źródeł danych. --> |
| 5 | Support for alternative APIs (ADS-B Exchange / FlightRadar24 / FlightAware) <!-- pl: Wsparcie dla alternatywnych API (ADS-B Exchange / FlightRadar24 / FlightAware) --> | medium | medium | medium | ⭐⭐⭐ | Together with the feeder, on the same data-source layer. FR24 and FlightAware are paid or restrictive. <!-- pl: Razem z feederem, na tej samej warstwie źródeł danych. FR24 i FlightAware są płatne albo restrykcyjne. --> |
| 6 | Home Assistant integration (MQTT) <!-- pl: Integracja z Home Assistant (MQTT) --> | medium | medium | low | ⭐⭐⭐ | Nice automations ("plane overhead in 10 s"), but it's out of the MVP scope for now. <!-- pl: Fajne automatyzacje („samolot nad domem za 10 s”), ale na razie poza zakresem MVP. --> |
| 7 | Home location from the network (IP geolocation) or an external GPS module <!-- pl: Pobieranie pozycji GPS z sieci (IP Geolocation) lub z zewnętrznego modułu --> | low | small / large | — | ⭐⭐ | IP location is off by kilometres, GPS is overkill indoors. Better: the browser's geolocation via the project page. <!-- pl: Lokalizacja z IP myli się o kilometry, GPS w domu to przesada. Lepiej: geolokalizacja przeglądarki przez stronę projektu. --> |
