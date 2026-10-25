# Specyfikacje

- [`done/`](done/) — zaimplementowane i przetestowane na płytce.
- [`planned/`](planned/) — w trakcie i czekające. Stan każdej w linii „Status” na górze pliku.

Numery są stałe (kolejność powstania, nie priorytet); priorytet jest w [ROADMAP.md](../ROADMAP.md).
Gotowa specyfikacja przechodzi do `done/` (`git mv`) razem z ostatnim commitem implementacji;
wtedy też poprawić odnośniki `spec/planned/…` w `src/`, CLAUDE.md i testach.

| Nr | Specyfikacja | Katalog | Stan |
|---|---|---|---|
| 01 | [Aktualizacje OTA i hasło panelu](done/01-ota-updates.md) | done | v1.1.0 |
| 02 | [Nowe wersje z GitHuba](done/02-github-release-check.md) | done | v1.2.0 |
| 03 | [Jasność i tryb nocny](done/03-brightness-night-mode.md) | done | v1.3.0 |
| 04 | [Dwa rdzenie](done/04-dual-core.md) | done | v1.4.0 |
| 05 | [Ograniczone cache hexdb.io](planned/05-bounded-lookup-cache.md) | planned | czeka |
| 06 | [Błąd logowania OpenSky → tryb anonimowy](planned/06-opensky-login-fallback.md) | planned | czeka (po 08) |
| 07 | [Reorganizacja testów](planned/07-test-reorganization.md) | planned | zaimplementowane, czeka na CI (`native-asan`) |
| 08 | [Ostrzeżenia urządzenia („!”)](planned/08-device-alerts.md) | planned | czeka |
| 09 | [Odporność: zrzut ekranu, komendy, WWW w trakcie lookupów](planned/09-concurrency-robustness.md) | planned | czeka |
| 10 | [Bezpieczeństwo panelu WWW](planned/10-web-panel-security.md) | planned | czeka |
| 11 | [Porządki: martwy kod, komentarze, dokumentacja](planned/11-cleanup-dead-code-docs.md) | planned | czeka |
| 12 | [Reorganizacja kodu (DDD)](planned/12-code-reorganization-ddd.md) | planned | czeka (po 07, 11) |
