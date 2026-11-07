# Native unit tests

Pure logic only — no ESP32, no display, no network. Unity, run by PlatformIO on the PC.
Why the code is split so this works: "Testing" in [CLAUDE.md](../CLAUDE.md).
How the layout came about: [spec 07](../spec/done/07-test-reorganization.md).

## Running

```bash
pio test -e native                    # all six suites (~40 s on Windows, was ~3 min with 41)
pio test -e native -f test_opensky    # one suite
pio test -e native-asan               # the same under AddressSanitizer + UBSan (Linux / CI)
```

One module inside a suite: build with `TEST_MODULE` set to the module name, e.g.

```bash
PLATFORMIO_BUILD_FLAGS="-DTEST_MODULE=opensky_client" pio test -e native -f test_opensky
```

(Not `TEST_ONLY`: `unity.h` already defines a `TEST_ONLY()` macro.) Changing the flags rebuilds
the suite; the next plain run rebuilds it again.

⚠️ Windows: a suite sometimes ends as `ERRORED` with `[WinError 4551]` and no failed assertion —
application control blocking the freshly built `program.exe`. Rerun that suite (`-f test_…`).
Rarer now than with 41 executables, but it still happens.

## Layout

The six suites follow the contexts of [spec 12](../spec/planned/12-code-reorganization-ddd.md),
so they stayed put when `src/` moved into `src/<context>/<layer>/` (stage 1). Each suite is one
executable. Test files include `src/` headers by **full path** (`#include
"enrichment/core/lookup_cache.h"`, `-I src`) and never an `adapters/`, `main/` (the roots) or
SDK header — `tools/check_layers.py` reports it, `tools/test_check_layers.py`'s `LayoutTest`
fails on a short include. `native` links everything in `src/` except `main/`, `*/adapters/`
and `platform/*_esp32.cpp`, with no exception (since spec 12 stage 4 split the pure halves out:
a device function declared in a core header and defined in an adapter can't be called from a
test — it wouldn't link; docs/architecture.md "Source layout"). Tests follow their code: when
spec 12 stage 3 moved a type to another context, its tests moved to that context's suite with
their names unchanged (`git log -S test_<name>` finds them); stage 4 renamed
`config_portal_test.cpp` → `settings_form_test.cpp` and `time_sync_test.cpp` → `clock_test.cpp`
with their code; stage 5 moved the 19 screenshot hand-over tests (`ScreenshotBridge`,
`ScreenshotReturn`) from `test_admin/screenshot_test.cpp` to
`test_display/screenshot_bridge_test.cpp` with their code, names and bodies unchanged.

| Suite | What | Modules (`<module>_test.cpp`) |
|---|---|---|
| `test_airspace` | the sky over home: aircraft state, tracked flights, order, NOW pick, phase, overhead pass, sky snapshot | aircraft_state, tracked_flight, flight_ordering, now_pick, flight_phase, cpa_predictor, poll_pipeline |
| `test_enrichment` | aircraft / route / airport data (hexdb.io, caches, NVS) | aircraft_lookup, route_lookup, lookup_cache, lookup_store, lookup_budget, aircraft_names, place_names |
| `test_opensky` | the OpenSky data source | opensky_client, opensky_quota, opensky_auth, scan_area |
| `test_display` | screens, theme, navigation, touch, brightness, display texts; the UI flows (`display/services`) | table_view, featured_panel, flight_screen, radar_view, radar_geometry, aircraft_summary, status_bar, service_screen, screen_nav, redraw_policy, touch_input, lcars_theme, utf8, fit_to_width, font_set, fonts, backlight, i18n, ui_flow, update_prompt_flow, ota_screen_flow, alert_screen_flow, service_screen_flows, password_reset_flow, screenshot_bridge, ui_perf |
| `test_admin` | settings, web panel, backups, password, updates | config_page, settings_form, config_backup, config_store, web_auth, web_i18n, ota_update, release_check, version, screenshot¹, device_alerts |
| `test_platform` | concurrency, perf, WiFi, time; the shared kernel (`src/shared/`) | core_link, perf_stats, wifi_manager, clock, sun_times, geo |

¹ `screenshot` (the BMP header / band / `?screen=` helpers `web_server` uses) lives in
`src/display/services/` but its 9 tests stay in `test_admin` (the settings page serves the
screenshot); the hand-over between the cores is `screenshot_bridge`, tested in `test_display`.

Cases per suite (spec 12 stage 6): admin 257 (3 skipped), airspace 71, display 408, enrichment
80, opensky 74, platform 84 — 974, the same as the `RUN_TEST(` count (`LayoutTest` keeps it
≥ 874).

Settings are guarded against a silent change of format: `config_backup_test.cpp` compares the
whole backup JSON (with and without secrets) to a golden string and restores a pre-stage-6
backup field by field, `config_store_test.cpp` pins every default, and
`tools/test_check_layers.py` pins the NVS keys and their types. Settings are nested objects
(`c.home.lat`, `c.backlight.night_mode`, `c.web.password` — docs/architecture.md "Source
layout (spec 12)"); members are assigned, never `{…}`-initialized (gnu++11 on `cyd`).

The UI flows are tested as plain step functions: build the flow, feed it `millis()` values
(odd ones where a `0` sentinel matters, and across the 32-bit wrap) and touch states, assert the
answer struct (`PromptStep`, `OtaScreenStep`, `ServiceOutcome` …). `service_screen_flows_test.cpp`
injects a fake command queue as the `CommandSender`; `screenshot_bridge_test.cpp` runs
`screenshotUiPass()` against a `std::mutex` bridge.

`support/` (on the include path via `-I test/support`, header-only):

- `test_support.h` — the assertions below and `resetAllForTesting()`.
- `fixtures.h` — `makeAircraft()`, `makeRow()`, `makeConfig()` (defaults + `home` in Warsaw,
  `home.configured`),
  `stubPorts()` (stub hexdb.io for `poll_pipeline`; `stubHexdb()` holds what is "cached" and
  records every fetch).
- `json_assert.h` — `parseJson()`, `assertJsonBool / Int / String / Missing(doc, "a.b.c", …)`.

## Conventions

- A `<module>_test.cpp` has its tests and helpers in an anonymous namespace and exactly one
  public function, `void run_<module>_tests()`, which starts with `UnitySetTestFile(__FILE__)`
  (so failures name this file, not `main.cpp`) and calls `RUN_TEST(...)`.
- The first line of the file says what it tests and where it is described — a spec, CLAUDE.md
  or a `docs/` file, cited by heading: `docs/screens.md "Screen 1"`, `CLAUDE.md "Testing"`.
  `python tools/check_comments.py` checks every such reference hits a real heading (its
  `RepoTest` in `tools/test_check_comments.py` fails CI otherwise).
- `setUp()` / `tearDown()` exist only in each suite's `main.cpp`. `setUp()` calls
  `resetAllForTesting()`: clears the hexdb.io caches and the fixture stubs, sets the display
  language back to Polish. A module with new global state adds its reset there.
- New module: add `<module>_test.cpp` to the suite of its context, declare and call its
  `run_<module>_tests()` in that suite's `main.cpp` (`if (RUN_MODULE(<module>)) …`).
- Built with `-Wall -Wextra -Werror` (tests and `src/`). Unused stub parameters:
  `const std::string & /*icao24*/`.
- Native `SpinLock` is a no-op (the tests are single-threaded), so code shared between the
  cores is templated on its lock (`LatestBox`, `Guarded`, `ScreenshotBridge`) and a threaded
  test plugs in a `std::mutex` adapter. One such test exists:
  `test_display/screenshot_bridge_test.cpp:test_bridge_concurrent_random_timeouts_never_touch_freed_buffers` — two
  long-lived `std::thread`s, a fake clock, a seeded `std::mt19937`, never real time; its
  `delete[]` variant is there for `native-asan` to catch a use-after-free. No `-pthread` flag
  is needed (MinGW posix threads, glibc ≥ 2.34).

### Assertions

Use the one that prints the values; `TEST_ASSERT_TRUE` is for real conditions (ranges, `&&`),
not for comparing one value.

| Instead of | Use | A failure prints |
|---|---|---|
| `TEST_ASSERT_TRUE(x.kind == Kind::Never)` | `ASSERT_ENUM_EQ(Kind::Never, x.kind)` (`ASSERT_ENUM_NE`) | `Expected 2 Was 1. x.kind` |
| `TEST_ASSERT_TRUE(s == "abc")` | `ASSERT_STR_EQ("abc", s)` (`std::string` or `const char *`) | `Expected 'LOT282' Was 'LOT281'. s` |
| `TEST_ASSERT_TRUE(h.find("foo") != npos)` | `ASSERT_CONTAINS(h, "foo")` | `"Goodbye" not found in html = "<html><body>…"` (first 200 chars) |
| `TEST_ASSERT_FALSE(h.find("foo") != npos)` | `ASSERT_NOT_CONTAINS(h, "foo")` | `"hunter22" unexpectedly found in html: "...xxx secret=hunter22 yyy..."` (±60 chars) |
| a search in a `std::vector<std::string>` | `ASSERT_CONTAINS(v, "a2")` | `"a2" not in v = ["a1", "a3"]` |
| `contains(json, "\"x\": false")` | `assertJsonBool(doc, "general.update_check", false)` | `general.update_check: expected false, was true` |
| | `assertJsonInt / assertJsonString / assertJsonMissing` | `display.night_mode: expected "schedule", was missing` |
| | `parseJson(text)` on something that isn't JSON | `not JSON (InvalidInput): {not json` |

The messages in this table are real output: each helper was broken on purpose once
(2026-10-25) to check that the failure says what went wrong.

## Screenshots of the web pages

`config_page_test.cpp` renders the settings pages to files when `SCREENSHOT_OUT` is set (its
two render tests are `IGNORE`d otherwise); `python tools/screenshots.py` runs
`pio test -e native -f test_admin` for that and turns them into PNGs.
