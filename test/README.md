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
so they stay put when `src/` gets reorganized. Each suite is one executable.

| Suite | What | Modules (`<module>_test.cpp`) |
|---|---|---|
| `test_airspace` | the sky over home: phase, overhead pass, sky snapshot | flight_phase, cpa_predictor, poll_pipeline |
| `test_enrichment` | aircraft / route / airport data (hexdb.io, caches, NVS) | aircraft_lookup, route_lookup, lookup_cache, lookup_store, lookup_budget, aircraft_names, place_names |
| `test_opensky` | the OpenSky data source | opensky_client, opensky_quota |
| `test_display` | screens, theme, navigation, touch, brightness, display texts | table_view, featured_panel, flight_screen, radar_view, radar_geometry, aircraft_summary, status_bar, service_screen, screen_nav, redraw_policy, touch_input, lcars_theme, fonts, backlight, i18n |
| `test_admin` | settings, web panel, backups, password, updates | config_page, config_portal, config_backup, config_store, web_auth, web_i18n, ota_update, release_check, version, screenshot |
| `test_platform` | concurrency, perf, WiFi, time | core_link, perf_stats, wifi_manager, time_sync, sun_times |

`support/` (on the include path via `-I test/support`, header-only):

- `test_support.h` — the assertions below and `resetAllForTesting()`.
- `fixtures.h` — `makeAircraft()`, `makeRow()`, `makeConfig()` (defaults + home in Warsaw),
  `stubPorts()` (stub hexdb.io for `poll_pipeline`; `stubHexdb()` holds what is "cached" and
  records every fetch).
- `json_assert.h` — `parseJson()`, `assertJsonBool / Int / String / Missing(doc, "a.b.c", …)`.

## Conventions

- A `<module>_test.cpp` has its tests and helpers in an anonymous namespace and exactly one
  public function, `void run_<module>_tests()`, which starts with `UnitySetTestFile(__FILE__)`
  (so failures name this file, not `main.cpp`) and calls `RUN_TEST(...)`.
- The first line of the file says what it tests and where CLAUDE.md / a spec describes it.
- `setUp()` / `tearDown()` exist only in each suite's `main.cpp`. `setUp()` calls
  `resetAllForTesting()`: clears the hexdb.io caches and the fixture stubs, sets the display
  language back to Polish. A module with new global state adds its reset there.
- New module: add `<module>_test.cpp` to the suite of its context, declare and call its
  `run_<module>_tests()` in that suite's `main.cpp` (`if (RUN_MODULE(<module>)) …`).
- Built with `-Wall -Wextra -Werror` (tests and `src/`). Unused stub parameters:
  `const std::string & /*icao24*/`.

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
