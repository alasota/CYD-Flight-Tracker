# Spec 05 — Ograniczone cache hexdb.io w RAM

Z przeglądu kodu 2026-10-25, punkt 1.1. Status: **do zrobienia**.

## 1. Problem

Cache wyników hexdb.io w RAM to trzy zwykłe `std::map` bez limitu i bez usuwania:
`cache` w [aircraft_lookup.cpp](../../src/aircraft_lookup.cpp) (po `icao24`), `routeCache` i
`airportCache` w [route_lookup.cpp](../../src/route_lookup.cpp). Każdy samolot widziany od startu
zostaje w RAM-ie na zawsze (wpis `AircraftInfo` = 4 × `std::string` + węzeł mapy ≈ 150–250 B).

Nad Warszawą to kilkaset nowych `icao24` na dobę. Przy ~143 KB wolnego heapu i największym
bloku 57 KB po kilku dniach pracy heap się kurczy i fragmentuje; gdy największy blok spadnie
poniżej 50 KB, `release_check` po cichu przestaje sprawdzać aktualizacje (`canStartNetworkTask()`),
a w końcu może zabraknąć pamięci na TLS do OpenSky.

RAM nie musi trzymać wszystkiego: każdy potwierdzony wynik jest już w NVS (`lookup_store`),
a odczyt z NVS nie zużywa budżetu lookupów na poll.

## 2. Cel

Pamięć zajęta przez cache jest **ograniczona stałą**, niezależnie od czasu pracy. Zachowanie
widoczne dla użytkownika się nie zmienia: brak dodatkowych zapytań do hexdb.io, brak `...`
dla samolotów, które już raz były rozpoznane.

## 3. Projekt

### 3.1 Nowy czysty szablon `LookupCache<V>` (`src/lookup_cache.h`, header-only)

LRU z kluczem `std::string`, pojemność podana w konstruktorze:

```cpp
template <typename V>
class LookupCache {
 public:
  explicit LookupCache(size_t capacity);
  const V *find(const std::string &key);      // trafienie = przesuwa na początek (najświeższy)
  bool contains(const std::string &key) const; // bez zmiany kolejności (dla *IsCached / budżetu)
  void put(const std::string &key, V value);  // nadpisuje albo dodaje; przy przepełnieniu
                                              // usuwa najdawniej używany wpis
  size_t size() const;
  size_t capacity() const;
  void clear();
};
```

Implementacja: `std::list<std::pair<std::string, V>>` + `std::unordered_map<std::string,
list::iterator>`. Bez alokacji przy trafieniu. `contains()` jest `const` i nie zmienia kolejności,
bo `aircraftKnown` / `routeKnown` / `airportKnown` w `net_task` pytają o każdy samolot przy
każdym pollu, a nie każde pytanie jest użyciem.

### 3.2 Pojemności (`src/lookup_cache.h`)

| Cache | Stała | Wartość | Uzasadnienie |
|---|---|---|---|
| samoloty | `kAircraftCacheCapacity` | 96 | kilka razy więcej niż samolotów w zasięgu przy ±2.5° (zwykle 10–40) |
| trasy | `kRouteCacheCapacity` | 96 | jak wyżej, klucz = callsign |
| lotniska | `kAirportCacheCapacity` | 64 | w praktyce kilkanaście powtarzających się lotnisk |

Pojemność musi być **≥ zbioru roboczego jednego pollu** (wszystkie samoloty w zasięgu), inaczej
cache „mieli” się w kółko i każdy poll czyta NVS. Assert w teście: pojemność ≥ 2 × typowa
liczba samolotów w zasięgu (`kAircraftCacheCapacity >= 64`).

Szacunek pamięci: 96 + 96 + 64 wpisów × ~250 B ≈ **64 KB w najgorszym przypadku**, zwykle dużo
mniej (krótkie stringi mieszczą się w SSO). Jeśli pomiar na płytce (§5) pokaże więcej niż
~40 KB, zmniejszyć pojemności, zamiast dokładać kod.

### 3.3 Zmiany w modułach

- `aircraft_lookup.cpp`, `route_lookup.cpp`: `std::map` → `LookupCache<…>`. Publiczne API bez zmian
  (`lookupAircraftWithFetcher`, `aircraftLookupIsCached`, `…SeedCache`, `…ClearCacheForTesting`).
- Ścieżka wypadnięcia z RAM-u jest już obsłużona: `lookupAircraft()` przy braku w RAM-ie czyta
  NVS (`storeLoadAircraft`) i seeduje cache, a `productionPorts()` w `net_task` liczy wpis
  z NVS jako „znany” (budżet go nie liczy). To zostaje.
- Nowe `aircraftLookupCacheSize()` / `routeLookupCacheSize()` / `airportLookupCacheSize()` do
  `[perf]` i `/perf.json` (pole `cache: {ac, rt, ap}`), żeby było widać na płytce, że liczby stoją.
- Memo per poll w `net_task.cpp` (`memoAircraft` itd.) zostaje: jest czyszczone po każdym pollu,
  więc nie rośnie.

## 4. Testy (`test_enrichment` po spec 07, plik `lookup_cache_test.cpp`)

- `put` / `find` / `contains` podstawowe; `put` istniejącego klucza nadpisuje bez wzrostu `size`.
- Przepełnienie usuwa **najdawniej użyty**, nie najdawniej dodany (`find` odświeża).
- `contains()` **nie** odświeża (wpis sprawdzany tylko przez `contains` wypada pierwszy).
- `size()` nigdy nie przekracza `capacity()` — 10 000 unikalnych kluczy.
- Pojemność 1 i pojemność 0 (0 = nic nie trzyma, `find` zawsze nullptr; bez crasha).
- `test_aircraft_lookup` / `test_route_lookup`: po wypadnięciu wpisu kolejne
  `lookup…WithFetcher` woła fetcher ponownie (dowód, że cache naprawdę wyrzuca); potwierdzony
  brak (`found=false`) też podlega LRU.
- `test_poll_pipeline`: symulacja 200 pollów z rotującym zbiorem 30 samolotów + 5 nowych na poll
  — rozmiar cache ograniczony, a samoloty z bieżącego pollu nie powodują ponownych fetchy.

## 5. Kryteria akceptacji (płytka)

1. `pio test -e native` zielone.
2. `/perf.json` po 24 h pracy: rozmiary cache ≤ pojemności, `heap` i `maxBlock` stoją w miejscu
   (różnica między 1. a 24. godziną < 5 KB), `maxBlock` ≥ 50 KB.
3. Liczba zapytań do hexdb.io (log `[aircraft_lookup] … HTTP request`) bez zmian względem wersji
   sprzed poprawki dla tego samego ruchu: wypadnięte wpisy wracają z NVS, nie z sieci.

## 6. Poza zakresem

- Limit / wygaszanie wpisów w NVS (`lookup_store` już czyści przestrzeń nazw przy zapełnieniu).
- Odświeżanie starych wpisów (typ samolotu się nie zmienia, CLAUDE.md).
