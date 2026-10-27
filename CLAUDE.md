# CLAUDE.md

## Project

**CYD Sky Tracker** — firmware for the ESP32-2432S028R ("Cheap Yellow Display") that shows live
aircraft over a fixed home location: callsign, airline, type, route, takeoff / landing, and a
countdown to when one is overhead. Data: OpenSky Network (`/states/all`), names from hexdb.io.
Onboarding and settings follow AnthonySturdy/micro-radar (WiFiManager captive portal, a web page
at `cyd-sky.local`, credentials on the device); the look is deliberately different — an
LCARS-inspired console. Three screens: Flights (table), NOW (overhead countdown), Radar.

## Where things are

| File | What |
|---|---|
| this file | rules: hardware, modules, testing, redraw, i18n, two cores, pitfalls, commands |
| [docs/screens.md](docs/screens.md) | what each screen, the header and the tab bar show, coordinates, colours, phase icons, CPA math, clock |
| [docs/data-sources.md](docs/data-sources.md) | OpenSky (OAuth2, credits, login fallback), hexdb.io (endpoints, caches, budgets) |
| [docs/architecture.md](docs/architecture.md) | two cores in detail, settings page, OTA + password + web security, GitHub updates, brightness / night, device alerts, CI, project page, harness |
| [docs/history.md](docs/history.md) | what was tried and why it changed — read before "simplifying" something back |
| [docs/review-notes-2026.md](docs/review-notes-2026.md) | the 2026 code review (comments cite it as `docs/review-notes-2026.md X.Y`) |
| `spec/` | specs (`planned/`, `done/`) + BDD `.feature` files; `spec/README.md` lists them |
| `test/README.md` | test suites, helpers, conventions |

Open the docs file for the area you touch. Comments cite sections as `docs/screens.md "Screen 1"`
or `CLAUDE.md "Testing"`; `tools/check_comments.py` checks they hit a real heading — rename a
heading only together with its references.

## Hardware

- Board **ESP32-2432S028R** (ESP32-WROOM-32), 320×240 TFT (ILI9341) in landscape
  (`setRotation(1)`), resistive touch (XPT2046) on its own SPI bus. ⚠️ CYD variants differ: this
  is the ILI9341 + resistive "R" board. If touch is dead or colours are wrong it may be an
  ST7789 "C" (capacitive) unit — confirm before debugging pins.
- Pinout (randomnerdtutorials.com CYD guide): TFT MISO 12, MOSI 13, SCLK 14, CS 15, DC 2,
  RST -1 (software), backlight 21 · touch CS 33, IRQ 36, MOSI 32, MISO 39, CLK 25 · light sensor
  (LDR) 34.
- Display driver **LovyanGFX**, configured in C++ in `include/LGFX_CYD.hpp` (`LGFX_Device`
  subclass: bus, panel, `Light_PWM`, `Touch_XPT2046`) — never by editing library internals, no
  display `build_flags`. Display on `HSPI_HOST`, touch on `VSPI_HOST`, `bus_shared = false`.
  `include/LGFX_CYD.hpp` and `platformio.ini` are the source of truth for pins and versions.
- Touch: LovyanGFX's built-in XPT2046 driver, no separate library. `tft.getTouch()` gives screen
  coordinates (follows `setRotation()`); calibration in `include/touch_calibration.h`, mapping
  explained in docs/screens.md "Screen navigation".
- `platformio.ini`: board `esp32dev` (no CYD entry), partitions `min_spiffs.csv` (two 1.875 MB
  OTA slots — the firmware must stay under one), envs `cyd`, `cyd-ota`, `native`,
  `native-asan`.

## Code conventions

Arduino framework on ESP32 (not raw ESP-IDF). One `.h`/`.cpp` pair per module in `src/`, one
responsibility each. Pure logic is separate from LovyanGFX / HTTP / touch calls (adapters behind
`#ifdef ARDUINO`) so it runs under `native`.

- **Data, core 0:** `wifi_manager` (WiFiManager connect + captive portal) · `opensky_client`
  (token + `/states/all`, returns plain structs, **no drawing**) · `opensky_auth` (login state,
  30-min retry, Bearer / refresh / anonymous decisions) · `opensky_quota` (credits left) ·
  `aircraft_lookup` (icao24 → airline / type) · `route_lookup` (callsign → route, airport →
  IATA / country) · `lookup_cache` (header-only LRU) · `lookup_store` (NVS copy of the caches) ·
  `lookup_budget` (which new lookups per poll) · `hexdb_http` (shared HTTPS GET, keep-alive) ·
  `poll_pipeline` (one answer → `DataSnapshot`: sort, lookups, phases, CPA, NOW pick) ·
  `net_task` (the core-0 task, owns all of the above) · `net_config` (HTTP timeouts).
- **Settings and maintenance:** `config_store` (NVS settings) · `config_portal` (WebServer +
  mDNS, routes) · `config_page` (page HTML, pure) · `config_backup` (JSON backup / restore) ·
  `web_auth` (password, throttle, origin check) · `web_i18n` (page strings) · `ota_update`
  (image checks, marker, `OtaSession`) · `release_check` (GitHub releases) · `version` ·
  `device_alerts` (the "!") · `screenshot` (BMP + band hand-over) · `perf_stats`.
- **Shared / platform:** `core_link` (what crosses the cores) · `time_sync` (NTP, pure
  formatting) · `sun_times` (sunrise / sunset) · `backlight` (brightness, night mode) ·
  `i18n` (display strings) · `touch_input` (raw → screen mapping, `hitTest`).
- **Domain logic, pure:** `flight_phase` (takeoff / landing / overflight) · `cpa_predictor`
  (closest-approach time) · `radar_geometry` (polar → screen) · `aircraft_names` (airline by
  callsign prefix, friendly type names) · `place_names` (IATA → city, `asciiFold()`).
- **Display, core 1:** `lcars_theme` (palette roles, fonts, pills, tabs, phase icons, arrows) ·
  `status_bar` (header) · `screen_nav` (screen index, hit tests, auto-cycle) · `redraw_policy`
  (events → redraw mask) · `aircraft_summary` (identity block shared by all screens) ·
  `featured_panel` · `table_view` · `flight_screen` · `radar_view` · `service_screen` (OTA,
  password reset, alert screen) · `main.cpp` (`setup()`, the UI `loop()`).

Rules:
- Never hardcode WiFi / OpenSky secrets. `millis()`-based timing, no `delay()` in the UI.
- `opensky_client` stays decoupled from the views — one data source, three screens.
- Views don't define colours or shapes: anything on more than one screen goes to `lcars_theme`,
  the aircraft identity block to `aircraft_summary`. Use the `THEME_*` roles, not palette names.
- Unused code is deleted together with its tests (git has it), not kept "for later".
- Text never overflows: cut with `fitToWidth()` (UTF-8 safe), never draw text taller than the
  row / strip it sits in. No scrolling — paginate or truncate.
- Display text never hardcoded — `tr(Str::X)` (see "Language").
- Comments describe what the code does now; history goes to docs/history.md.

## Testing — logic gets covered first, not "later"

- **Test first.** Before non-trivial logic (geometry, sorting, timing, cache behaviour, parsing,
  phase / CPA, wraparound, boundaries such as the auto-cycle hold at exactly 6 / -5 s), write
  its test in `test/`, or at least alongside. "Later" doesn't happen on a solo project.
- Module boundaries exist so logic runs in `pio test -e native` — no ESP32, display or network.
  Only thin adapters that call hardware / SDK APIs (TFT draw, HTTP request, touch read) may go
  untested; everything they delegate to gets a test.
- New module or feature → propose its test file with it, unasked.
- A milestone is done when `pio test -e native` is green **and** the new logic has coverage —
  compiling is not correct.
- **Layout** (spec 07, details in `test/README.md`): six suites by context — `test_airspace`,
  `test_enrichment`, `test_opensky`, `test_display`, `test_admin`, `test_platform`. A module's
  tests are `test/test_<suite>/<module>_test.cpp`: an anonymous namespace plus one
  `run_<module>_tests()`; the suite's `main.cpp` is the only place with `setUp()`
  (→ `resetAllForTesting()`; new global state gets its reset there). One module:
  `PLATFORMIO_BUILD_FLAGS="-DTEST_MODULE=<module>"`.
- **Assertions that print values** (`test/support/test_support.h`): `ASSERT_ENUM_EQ`,
  `ASSERT_STR_EQ`, `ASSERT_CONTAINS` / `ASSERT_NOT_CONTAINS`, JSON via `json_assert.h` — never
  `TEST_ASSERT_TRUE(a == b)` for one value. Fixtures: `fixtures.h` (`makeAircraft()`,
  `makeRow()`, `makeConfig()`, `stubPorts()`).
- `native` builds with `-Wall -Wextra -Werror` (tests and `src/`); CI also runs `native-asan`
  (AddressSanitizer + UBSan, Linux only).
- Python tooling (`tools/*.py`) has `unittest` tests next to it (`tools/test_*.py`).

## Redraw policy — no full-screen clears except on screen change

A full `fillScreen()` flashes visibly on the ILI9341 over SPI (why: docs/history.md).
- `fillScreen()` only on a screen change (touch, auto-cycle, table page) and the first draw.
  Never on a timer tick.
- New poll data → clear and redraw only the content area (`LCARS_CONTENT_TOP..BOTTOM`, one
  `fillRect`). Header and tab bar only when their own content changed.
- Header → `kRedrawHeader` on count, health, alert badge or minute change (any screen).
- NOW countdown tick (1 Hz) → only the countdown zone + status strip, through the 8-bit sprite
  pushed in one go; identity panel, header and tabs untouched.
- The decision is the pure `computeRedrawMask(events, currentScreen)` (`redraw_policy`); events
  come from `diffNetState()` and the UI's timers; `main.cpp` only acts on the mask.

## Language — Polish / English

- Every displayed string is in the `I18N_STRINGS` X-macro list in `src/i18n.h` (PL + EN side by
  side); the `Str` enum and both tables are generated from it, so a string can't miss a
  translation. Tables are `static const char *const` → flash (.rodata); no `PROGMEM` / `F()`.
- Never hardcode display text: `tr(Str::X)` (current language, `i18nSetLanguage()`); pure
  functions that return text take an explicit `Language` so tests check both.
- Setting `language` (NVS `lang`, default PL). After a save `net_task` reloads the config, the UI
  calls `i18nSetLanguage()` and redraws fully (`RedrawEvents::languageChanged`). The web page has
  its own language and table (`web_i18n`, docs/architecture.md "Settings page").
- **Fonts with Polish letters:** efont 16 / 14 / 10 px subsets in `src/fonts/lcars_efont.c`,
  generated by `tools/subset_efont.py` (keeps the efont BSD notice). The character set (printable
  ASCII + ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ + `° ± × · – — … →`) is defined once in `lcars_theme`
  (`isFontCodepoint()` / `isFontSafeText()`); the script's `CODEPOINTS` must match it, and
  `fonts_test.cpp` checks every allowed character has a glyph in all three fonts. A character
  outside the set isn't drawn, so every on-screen string must be `isFontSafeText()` (tested for
  i18n, the city table, type / airline tables). To add one: extend both lists, rerun the
  script, run the tests. hexdb.io names are `asciiFold()`ed. Font4 / Font8 never show Polish.
- Layout maths uses `utf8Length()` and the fixed advances (`LCARS_FONT_BODY_CHAR_PX` 8,
  `LCARS_FONT_TABLE_CHAR_PX` 7, `LCARS_FONT_SMALL_CHAR_PX` 5); `fitToWidth()` cuts on UTF-8
  boundaries. Tests enforce: screen names ≤ 7 chars, column labels fit their columns.
- Not translated: units (`m`, `km`, `km/h`, `MIN`), `--` / `...` placeholders, hexdb.io names.

## Concurrency — two cores

Details (channels, screenshot bridge, poll phases, perf): docs/architecture.md "Concurrency —
two cores". Spec: `spec/done/04-dual-core.md`.
- **Core 0, `net_task`** (16 KB stack): WiFi, NTP, settings page, OpenSky, hexdb.io, GitHub,
  OTA, the in-memory `Config`. **Core 1, `loop()` in `main.cpp`** (8 KB): `tft`, touch,
  navigation, redraw, backlight, update question, service screens; sleeps between passes.
- Code on core 0 never calls LovyanGFX; code on core 1 never does network I/O. They share only
  `core_link`: the `NetState` snapshot (net → UI, pointer swap), the `NetCommand` queue
  (UI → net) and a few atomics / `Guarded<T>`.
- Never allocate, free, log or do I/O while holding a `SpinLock` (a FreeRTOS critical section).
- ⚠️ Core 0's task watchdog panics (5 s, `CONFIG_ESP_TASK_WDT_PANIC=1`): nothing in `net_task`
  may hold the CPU for seconds. Blocking socket waits yield; tight loops and floods of tiny TCP
  writes don't — send in large pieces (the settings page goes out in ~1.4 KB chunks).
- ⚠️ Flash writes (OTA, NVS: `lookup_store`, `saveLastScreen`) stall both cores for ms to tens of
  ms — visible in `[perf]`.
- ⚠️ Heap: ~143 KB free, largest block ~57 KB; verified TLS (`release_check`) needs ≥ 50 KB in
  one block. Allocate long-lived buffers at boot (the NOW sprite: `flightScreenReserveSprite()`
  in `setup()`), not on first use. Watch `[perf] stack net=` — the web handler runs nested
  inside a poll.
- Liveness: no net heartbeat for 60 s → the header shows a network error; no UI heartbeat for
  2 s → a new OTA image isn't marked valid and is restarted (rollback).

## Pitfalls (⚠️ found the hard way — details in docs/)

- ⚠️ OpenSky is OAuth2 `client_credentials` only — no Basic Auth (docs/data-sources.md
  "Authentication"). Wrong credentials fall back to anonymous, they never stop the data.
- ⚠️ `/states/all` must use HTTP/1.0 — the body is parsed from the raw stream and chunked
  encoding broke it (docs/history.md).
- ⚠️ Credits: 15 s polls exceed even an account's 4000/day; never poll faster than the interval;
  back off on 429 (docs/data-sources.md "Rate limits").
- ⚠️ hexdb.io is a free hobby service: every answer cached (RAM LRU → NVS), never re-fetched,
  never block a row on it; RAM caches must stay bounded.
- ⚠️ arduino-esp32 2.x `WebServer`: an upload route gets non-multipart POSTs too and crashes in
  `server.upload()` — every upload callback starts with `if (!isMultipartRequest()) return;`,
  and `collectHeaders()` must keep `Content-Type` (docs/architecture.md "Firmware updates").
- ⚠️ Every POST route: `requireSameOrigin()` before `authorize()`; every response carries the
  anti-framing headers; nothing changes state on a GET.
- ⚠️ Touch raw axes are swapped and the horizontal one inverted — check against
  `touch_input::mapRawTouch()`'s tests before touching `touch_calibration.h`.
- ⚠️ Windows: a native suite sometimes ends `ERRORED` (WinError 4551, application control
  blocking the fresh `.exe`) with no failed assertion — rerun that suite.
- ⚠️ Project page: `site.js` sets the language with `classList`, never `className =`; release
  commits don't deploy on push (docs/architecture.md "Versioning & CI").

## Versioning, CI and the harness

- Version = git tag (`tools/pio_version.py` → `FW_VERSION`, only in `src/version.cpp`), shown in
  the settings page footer. Workflows: `firmware.yml` (tests, checks, build, artifacts),
  `site.yml` (project page), `release.yml` (tag `v*` → GitHub Release + page), `pr.yml` (a
  `-dev.N` pre-release per PR). Details: docs/architecture.md "Versioning & CI".
- Project page source `site/` (PL/EN pairs), built by `tools/build_site.py`; "What's next" comes
  from `ROADMAP.md`, the changelog from git — so every released feature needs a ROADMAP Done row
  and commit subjects must read well in a changelog.

### Spec → PR harness (`/implement`, `/fix`, `/release`)

- `/implement NN` / `/implement #12`: spec-analyst (plan + `.feature`, **stops for approval**) →
  test-author (red tests) → implementer → docs-writer → reviewer → PR. `/fix`: bug-investigator
  (failing regression test, **stops for OK**) → implementer → docs-writer → reviewer → PR "Fix:"
  (label `bug`). `/release`: `tools/release_plan.py` picks minor / patch, docs PR, tag.
- Rules for the agents: `.claude/harness/conventions.md`; hand-over files in
  `.claude/work/<slug>/` (git-ignored). Branches `spec/…`, `issue/…`, `fix/…`, `release/…`;
  agents never commit to main or merge. `main` takes PRs only (rebase merges).
- Every BDD Scenario is tagged `@test=<suite>/<file>.cpp:<function>`,
  `@test=tools/<file>.py:<Class>.<method>` or `@manual`; `tools/check_bdd.py` enforces it.
- Docs on a branch write the version as a bare `vNEXT` placeholder; `/release` fills it in.
  Details: docs/architecture.md "Spec → PR harness".

## Commands

- Build: `pio run` · upload: `pio run -t upload` · over WiFi: `pio run -e cyd-ota -t upload`
  (`$CYD_WEB_PASSWORD`) · monitor: `pio device monitor -b 115200`
- Tests: `pio test -e native` (one suite: `-f test_display`; sanitizers on Linux:
  `pio test -e native-asan`)
- Checks: `python tools/check_bdd.py`, `python tools/check_comments.py [--strict]` (comment doc
  references, removed symbols, `main.cpp` in core-0 modules, CLAUDE.md size)
- Tool tests (CI): `python tools/test_check_bdd.py`, `python tools/test_release_plan.py`,
  `python tools/test_build_site.py`, `python tools/test_check_comments.py`
- Project page: `python tools/build_site.py --out _site` (`--firmware DIR` adds the installer)
- Screenshots of the settings page / update question:
  `python tools/screenshots.py [--device cyd-sky.local]`
- Spec / issue → PR: `/implement 05`, `/implement #12`; bug → PR: `/fix <description>`,
  `/fix #14`; release: `/release` (`python tools/release_plan.py` shows the plan)

## Out of scope

- Historical tracks (`/flights/*`, `/tracks/*`) — only live `/states/all`.
- MQTT / Home Assistant — a standalone display, not a sensor node.
- Trails, multi-aircraft or turn-aware prediction — `cpa_predictor` is straight-line on purpose.
- Own-receiver mode (`/states/own`) — network-wide data, not a personal ADS-B feeder.
- Animated screen transitions — instant redraw is fine.
