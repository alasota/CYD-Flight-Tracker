# Specyfikacje

- [`done/`](done/) — zaimplementowane i przetestowane na płytce.
- [`planned/`](planned/) — w trakcie i czekające. Stan każdej w linii „Status” na górze pliku.

Numery są stałe (kolejność powstania, nie priorytet); priorytet jest w [ROADMAP.md](../ROADMAP.md).
Gotowa specyfikacja przechodzi do `done/` (`git mv`) razem z ostatnim commitem implementacji;
wtedy też poprawić odnośniki `spec/planned/…` w `src/`, CLAUDE.md i testach.

Implementacja: `/implement NN` (albo `/implement #<issue>`) — agenci na osobnej gałęzi, aż do
PR (CLAUDE.md „Spec → PR harness”). Do czasu wydania kolumna „Stan” i linia „Status” mówią
`vNEXT`; `/release` wpisuje numer wersji i przenosi do `done/` to, co jeszcze zostało w
`planned/`. Obok specyfikacji leży jej `NN-nazwa.feature`: scenariusze
BDD (Gherkin, słowa kluczowe po angielsku), każdy z `@test=<suite>/<plik>.cpp:<funkcja>` albo
`@manual`; `python tools/check_bdd.py` pilnuje, że testy istnieją. Issue bez specyfikacji
dostaje ją od agenta (`NN` = kolejny wolny numer, „Źródło: issue #N”).

| Nr | Specyfikacja | Katalog | Stan |
|---|---|---|---|
| 01 | [Aktualizacje OTA i hasło panelu](done/01-ota-updates.md) | done | v1.1.0 |
| 02 | [Nowe wersje z GitHuba](done/02-github-release-check.md) | done | v1.2.0 |
| 03 | [Jasność i tryb nocny](done/03-brightness-night-mode.md) | done | v1.3.0 |
| 04 | [Dwa rdzenie](done/04-dual-core.md) | done | v1.4.0 |
| 05 | [Ograniczone cache hexdb.io](done/05-bounded-lookup-cache.md) | done | v1.4.3 |
| 06 | [Błąd logowania OpenSky → tryb anonimowy](done/06-opensky-login-fallback.md) | done | v1.6.0 |
| 07 | [Reorganizacja testów](done/07-test-reorganization.md) | done | v1.4.2 |
| 08 | [Ostrzeżenia urządzenia („!”)](done/08-device-alerts.md) | done | v1.5.1 |
| 09 | [Odporność: zrzut ekranu, komendy, WWW w trakcie lookupów](done/09-concurrency-robustness.md) | done | v1.8.0 |
| 10 | [Bezpieczeństwo panelu WWW](planned/10-web-panel-security.md) | planned | czeka |
| 11 | [Porządki: martwy kod, komentarze, dokumentacja](planned/11-cleanup-dead-code-docs.md) | planned | czeka |
| 12 | [Reorganizacja kodu (DDD)](planned/12-code-reorganization-ddd.md) | planned | czeka (po 07, 11) |
| 13 | [Strona projektu: historia zmian, zrzuty panelu, opis funkcji](done/13-project-page-layout.md) | done | v1.7.0 |
