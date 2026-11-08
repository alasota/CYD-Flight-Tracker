# Architecture — how the firmware is put together

The current architecture in detail: the source layout, the two cores, the settings page,
updates, brightness, the "!" alerts, CI and the agent harness. The rules a session must follow
are in CLAUDE.md; this file is the reference behind them.

## Source layout (spec 12)

Spec: `spec/planned/12-code-reorganization-ddd.md` (§2 layer names, §4 contexts, §5 dependency
table, §8 stages and their results).

### Contexts and layers

| Context | What it answers | Layers |
|---|---|---|
| `airspace` | what flies over home and when it will be overhead (the core domain) | `core`, `services` |
| `enrichment` | which airline, type and route a flight has (hexdb.io, caches) | `core`, `adapters` |
| `opensky` | OpenSky as a data source; nothing of its format leaves this folder | `core`, `adapters` |
| `display` | screens, theme, fonts, touch, backlight, display texts | `core`, `services`, `adapters` |
| `admin` | settings, web panel, backups, password, alerts, updates | `core`, `adapters`, `assets` |

Layers: `core/` (pure logic, native, tested), `services/` (state machines and pipelines over
`core` and ports, native, tested), `adapters/` (SDK calls, not native). Outside the contexts:
`shared/` (the shared kernel: a change needs a reason in its commit message), `platform/` (what
connects the contexts on the ESP32) and `main/` (composition roots).

### Dependency rules

Who (row) may include what (column). `tools/check_layers.py` holds this table as data
(`ALLOWED`); `--strict` runs in CI before the native tests and fails the build.

| Who | `shared` | `airspace/core` | `enrichment/core` | `opensky/core` | `display/core` | `admin/core` | `*/services` | `platform` | `*/adapters` | SDK |
|---|---|---|---|---|---|---|---|---|---|---|
| `shared` | yes | | | | | | | | | |
| `airspace/core` | yes | yes | | | | | | | | |
| `enrichment/core` | yes | | yes | | | | | | | |
| `opensky/core` | yes | yes | | yes | | | | | | |
| `display/core` | yes | yes | yes | | yes | yes¹ | | | | |
| `admin/core` | yes | yes² | | yes² | yes² | yes | | | | |
| `airspace/services` | yes | yes | yes | | | | own | | | |
| `display/services` | yes | yes | yes | | yes | yes¹ | own | yes | | |
| `admin/services` | yes | yes | | yes | yes² | yes | own | | | |
| `platform` | yes | yes | yes | yes | yes | yes | | yes | | only `#ifdef ARDUINO` |
| `*/adapters` | yes | own context | own | own | own | own | own | yes | own | yes |
| `main/` | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |

¹ read-only types: `AlertSet`, display settings. ² value types for composing settings and alerts
(`NightMode`, `PhaseThresholds`, `OpenSkyQuota` …). The check treats both footnotes as allowed
(an include can't show them). Tests may include anything except `adapters/`, `main/` and SDK
headers.

- `display` never sees `opensky`; it sees `FeedHealth`. `opensky` and `enrichment` don't know each
  other; `airspace/services/poll_pipeline` joins them through ports.
- No context includes another context's `adapters/`. A function that needs several contexts goes
  to `main/`: `deviceAlertInputs()` and `panelLiveInputs()` in `main/alert_sources.cpp`, declared
  in `admin/core` (the core header is the port).

### Glossary

The same names in the code, these docs and the spec (§3).

| Term | In the code | Meaning |
|---|---|---|
| Home | `Home` (`lat`, `lon`, `radius_deg`, `configured`) | the place the device watches the sky over |
| Scan area | `ScanArea` | the rectangle around home sent to OpenSky |
| Aircraft state | `AircraftState` | one observation: position, altitude, speed, heading, on ground |
| Flight | `TrackedFlight` | an aircraft in range with distance, bearing, phase, airline, type, route |
| Sky snapshot | `AirspaceSnapshot` | the flights of one poll plus the overhead prediction |
| Phase | `Phase` | takeoff / landing / overflight / none |
| Overflight | `CpaPrediction` | when and how close to home an aircraft will pass (CPA) |
| Aircraft data | `AircraftInfo` | airline, type, registration (hexdb.io) |
| Route | `RouteInfo` | origin and destination (hexdb.io) |
| Airport | `AirportInfo` | IATA / ICAO codes, country (hexdb.io) |
| Credits | `OpenSkyQuota`, `openSkyCreditCost()` | OpenSky's limit; the cost of a query is a function, not a type |
| Feed health | `FeedHealth` | whether data is flowing |
| Alert | `AlertId`, `AlertSet` | something needs the owner's attention: one alert, the active ones |
| Settings | `Settings` | everything stored in NVS, an aggregate of per-context objects |

### Modules

- **`airspace/core`:** `aircraft_state` (`AircraftState`, `inferOnGround()`) · `home` ·
  `tracked_flight` (`TrackedFlight`) · `airspace_snapshot` (`AirspaceSnapshot`) · `flight_ordering`
  (order, featured split) · `now_pick` (NOW aircraft) · `feed_health` (`FeedHealth`) ·
  `flight_phase` · `cpa_predictor`. **`airspace/services`:** `poll_pipeline` (one answer →
  `AirspaceSnapshot`: sort, lookups, phases, CPA, NOW pick).
- **`enrichment/core`:** `aircraft_lookup` (icao24 → airline / type) · `route_lookup` (callsign →
  route, airport → IATA / country) · `aircraft_names` (airline by callsign, type names) ·
  `place_names` (IATA → city, `asciiFold()`) · `lookup_cache` (LRU) · `lookup_store` (NVS copy) ·
  `lookup_budget` (lookups per poll). **`enrichment/adapters`:** `hexdb_http` (HTTPS GET) ·
  `hexdb_sources` (hexdb.io lookups) · `lookup_store_nvs`.
- **`opensky/core`:** `opensky_client` (JSON of `/states/all` → `AircraftState`, **no drawing**) ·
  `opensky_settings` · `opensky_auth` (login, 30-min retry, Bearer / refresh / anonymous) ·
  `opensky_quota` (credits left) · `scan_area` (`ScanArea`, credit cost). **`opensky/adapters`:**
  `opensky_http` (token, `/states/all`).
- **`display/core`:** `lcars_theme` (palette roles, font metrics, icon / arrow shapes) · `text/`
  (`utf8`, `fit_to_width`, `font_set` = the character set) · `status_bar` (header) · `screen_nav`
  (screen, hit tests, auto-cycle) · `redraw_policy` (events → redraw mask) · `aircraft_summary`
  (identity block of all screens) · `featured_panel` · `table_view` · `flight_screen` ·
  `radar_view` · `radar_geometry` (polar → screen) · `service_screen` (OTA, password reset, alert
  screen, `kUpdatePromptTimeoutMs`) · `display_settings` (+ auto-cycle) · `backlight` (brightness,
  night mode) · `i18n` (display strings) · `touch_input` (raw → screen).
- **`display/services`:** `ui_flow` (taps, table page, screen changes, ticks, auto-cycle, health,
  `UiView`) · `service_screen_flows` (`ota_screen_flow`, `update_prompt_flow`,
  `alert_screen_flow` as one step) · `password_reset_flow` · `screenshot_bridge` · `ui_perf`.
- **`display/adapters`:** `backlight_ldr` (LDR, PWM) · `touch_xpt2046` · `service_flows`
  (service-screen drivers) · `lgfx/<module>_draw` (screens, theme, fonts) · `lgfx/ui_draw` ·
  `lgfx/screenshot_panel`.
- **`admin/core`:** `settings` (`Settings`: aggregate of context objects) · `settings_form`
  (`/save` form, checks, credentials file) · `config_page` (page head, tab bar, footer, status
  texts; `PanelLiveInputs`) · `config_page_tabs` (one function per tab) · `config_page_writer`
  (the HTML `Writer`) · `config_backup` (JSON backup / restore) · `web_auth` (password, throttle,
  origin) · `web_i18n` (page strings) · `device_alerts` (the "!") · `screenshot` (BMP, `?screen=`)
  · `firmware/` (`ota_update`: image checks, `OtaSession`, version-change texts; `release_check`: GitHub releases;
  `version`). **`admin/assets`:** CSS / JS / icon, embedded into `admin/core/web_assets.h`.
- **`admin/adapters`:** `web_server` (WebServer + mDNS, the server, its checks, page and settings
  routes) · `web_server_firmware` (`POST /update`, the GitHub routes) · `web_server_internal.h`
  (what those two share, `namespace web_server_detail`) · `settings_nvs` · `ota_flash` (`Update`)
  · `github_http` (check / install) · `github_ca.h`.
- **`shared/`:** `geo` (distance, bearing, compass point) · `language.h` (`Language`) ·
  `flight_info` (`AircraftInfo`, `RouteInfo`, `AirportInfo`) · `clock` (date / time text) ·
  `sun_times` (sunrise / sunset).
- **`platform/`:** `core_link` (what crosses the cores) · `time_sync` (NTP) · `wifi_manager`
  (connect + captive portal) · `perf_stats` · `net_config` (HTTP timeouts).
- **`main/`, core 1:** `main.cpp` (wiring only: `setup()`, the UI `loop()`). **`main/`, core 0:**
  `net_task` (the core-0 task) · `net_perf` (its share of `/perf.json` and `[perf]`) ·
  `net_enrichment` (`productionPorts()`, per-poll memos) · `net_rollback` (marks a new image valid
  or restarts it) · `alert_sources` (`deviceAlertInputs()`, `panelLiveInputs()`).

### How the layout works, and how it got here

The stage bullets use today's names.

- **Folders:** `src/<context>/<layer>/` — contexts `airspace` (the sky model: aircraft state,
  tracked flights, their order, the NOW pick, feed health, flight phase, CPA; the poll pipeline),
  `enrichment` (hexdb.io lookups, caches, names), `opensky` (client, login, credits),
  `display` (screens, theme, fonts, i18n, touch, backlight, redraw), `admin` (settings, page,
  backup, password, alerts; `admin/core/firmware/` = OTA, release check, version). Layers:
  `core/` (pure logic of the context: types, rules, maths, screen layouts, texts — native, tested),
  `services/` (state machines, pipelines, use cases that combine `core` parts and ports — native,
  tested), `adapters/` (SDK calls: ESP32, LovyanGFX, HTTP, NVS, `WebServer` — not native). Plus
  `shared/` (`sun_times`, `geo`, `clock`, `language`, `flight_info`), `platform/` (`core_link`, `time_sync`, `wifi_manager`,
  `perf_stats`, `net_config`) and `src/main/` (`main.cpp`, `net_task` — composition roots, one
  per core — and the parts of `net_task`). Subfolders: `display/core/screens/`, `display/core/theme/` (+ `fonts/`),
  `display/core/text/` (`utf8`, `fit_to_width`, `font_set`), `display/adapters/lgfx/` (the
  `LGFX_CYD.hpp` device class and one `<module>_draw.{h,cpp}` per screen / the theme).
- **Adapters** (stage 4, names from spec 12 §4): `display/adapters/` — `lgfx/status_bar_draw`,
  `service_screen_draw`, `radar_view_draw`, `flight_screen_draw` (+ the countdown sprite),
  `table_view_draw`, `featured_panel_draw`, `aircraft_summary_draw`, `lcars_theme_draw` (the
  efont objects, `LCARS_FONT_*`, `gfxTextMeasure()`, icons, arrows, pills, tabs),
  `touch_xpt2046` (`touchInputRead()`, `touchLogTap()`), `backlight_ldr` (LDR, PWM, night
  window); since stage 5 also `service_flows` (the service-screen drivers, `ServiceScreens`),
  `lgfx/ui_draw` and `lgfx/screenshot_panel` (see the stage 5 bullet);
  `enrichment/adapters/` — `hexdb_sources` (aircraft / route / airport fetch + `lookup*()`),
  `lookup_store_nvs`, `hexdb_http`; `opensky/adapters/opensky_http` (token, `/states/all`
  HTTP/1.0 stream, 401 / 429, schedule); `admin/adapters/` — `web_server` + `web_server_firmware` (routes + mDNS, was
  `config_portal`), `settings_nvs` (`loadSettings()` / `saveSettings()` …), `ota_flash` (`Update`,
  SHA-256, `otaFlashSession()`), `github_http` (release check / install), `github_ca.h`. `#ifdef ARDUINO` is left only in `platform/`
  (`core_link`, `time_sync`, `wifi_manager`) and `enrichment/adapters/hexdb_http`.
- **Stage 2** (`git mv` of 80 files, include lines only): `<ctx>/domain/` → `<ctx>/core/`,
  `<ctx>/app/` → `<ctx>/services/`, `src/app/` → `src/main/`; `admin/app/` split —
  `config_page`, `web_i18n` (pure) → `admin/core/`, `config_portal` (the `WebServer` routes, now `web_server`) →
  `admin/adapters/`. Until stage 5 only `airspace/services/` (`poll_pipeline`) and
  `display/services/` (`screenshot`) existed.
- **Stage 3** (symbols, not files — written by hand, no move table): each misplaced type left its
  old header in the same commit it appeared in the new one, and its tests moved with it into the
  suite of the new context (49 tests, names unchanged). `airspace/core/`: `aircraft_state`
  (`AircraftState`, `kTaxiSpeedMps`, `inferOnGround()` — from `opensky_client`, which now
  includes it and translates OpenSky's array into it), `feed_health.h` (`FeedHealth`, from
  `status_bar.h`), `tracked_flight` (`TrackedFlight`, `buildEnrichedRecords()`,
  `annotateDistances()`, `classifyPhases()` — from `table_view`), `flight_ordering`
  (`sortRowsForDisplay()`, `sortRowsByDistance()`, `splitFeaturedAndRest()` — from
  `table_view`), `now_pick` (`pickNowAircraft()`, `kOverheadRadiusKm` — from `flight_screen`).
  `shared/geo`: `computeDistanceBearing()` (from `table_view`), `compassPoint()` (from
  `lcars_theme`). `opensky/core/scan_area`: `bboxAreaSqDeg()`, `openSkyCreditCost()` (from
  `config_portal`). `display/core/text/`: `utf8` (`utf8Length()`, `utf8IsCharBoundary()` — was an
  anonymous helper), `fit_to_width` (`TextMeasure`, `fitToWidth()`), `font_set`
  (`isFontSafeText()`, `isFontCodepoint()`, `fontHasGlyph()` — from `lcars_theme`). Companions moved with
  their partners (`DistanceBearing`, `FeaturedSplit`, `NowPick`) so `poll_pipeline` includes no
  screen header. `core_link.h` now includes `tracked_flight.h` and `feed_health.h`, no
  `display/core/screens/` header (`LayoutTest.test_core_link_includes_no_screen_headers`).
  `TrackedFlight`'s copy / move constructors, assignments and destructor are declared in
  `tracked_flight.h` and `= default` in `tracked_flight.cpp` — out of line on purpose, for
  firmware size (docs/history.md "Tooling and docs").
- **Stage 4** (pure / hardware split, owner 2026-11-01): the whole `#ifdef ARDUINO` half of a
  `core/` file (definitions, its file-local helpers and globals, SDK includes) went to the adapter
  `.cpp`; the pure part stayed as it was. Declarations with plain types (`Settings`, `std::string`,
  core structs) **stay unguarded in the core header** — it is the port, the adapter implements it
  (`settings.h`, `lookup_store.h`, `opensky_client.h`, `release_check.h`, `ota_update.h`,
  `backlight.h`, `device_alerts.h`); a native caller would fail to link, which is the guard.
  Signatures with `LGFX &` / `LGFX_Sprite` / `lgfx::IFont` went to the adapter's own header.
  Constants both halves use moved to the core header as plain `constexpr` with a module prefix
  (`kTableRowStep`, `kFlightCountdownTop`, `kFeaturedPanelTop`, `kStatusBarDotGap` …; the `cyd`
  env compiles as gnu++11, so no `inline` variables); the table's column array is
  `extern const TableColumn kTableColumns[]`, defined once in `table_view.cpp`. An adapter reaches
  other contexts' types through its own core header: `aircraft_summary.h`, `featured_panel.h` and
  `flight_screen.h` include `tracked_flight.h` instead of declaring `struct TrackedFlight;`. Draw
  files were split callers first (status bar / service screens → radar → NOW → table + featured +
  identity block → theme), so no step added a `display/core -> display/adapters` include. Also:
  `extractAircraftFromStates()` is public in `opensky_client.h` (`opensky_http` calls it; one new
  test), `OtaObserver` moved from the web server's header to `ota_update.h`, `config_portal`'s
  pure functions went to `admin/core/settings_form` (the parsed credentials file, since stage 6
  `CredentialsFile`, `parseOpenSkyCredentialsJson()`, `isValidFloatString()`, `isValidUnsignedIntString()`), then
  `config_portal` was renamed `web_server` (its `configPortal*()` functions became
  `webServer*()` in stage 7), `time_sync`'s formatters (`formatDate()`, `formatTime()`,
  `minuteOf()`) went to `shared/clock`, `main/net_task` lost its whole-file guard, and
  `include/LGFX_CYD.hpp` / `include/touch_calibration.h` moved to `display/adapters/lgfx/` /
  `display/core/` (`include/` holds only PlatformIO's README). `AircraftInfo`'s constructor is
  declared in `aircraft_lookup.h` and `= default` in the `.cpp`, like `TrackedFlight`'s copies
  (docs/history.md "Tooling and docs").
- **Stage 5** (the `services/` layer, spec 12 §6; owner 2026-11-05: the plan's defaults, then the
  `ServiceScreens` driver + `UiFlow` aggregate when `main.cpp` stopped at 327 lines). The state
  machines of `main.cpp` (909 lines) and the form logic of `handleSave()` became pure modules —
  a step in, one answer struct out, no `#ifdef ARDUINO`, native-tested:
  - `display/services/ui_flow` — `routeTap()` (`TapKind` None / OpenAlerts / JumpTo / PagePrev /
    PageNext), `tablePageCount()` + `TablePager`, `ScreenChangeTracker` (persist, page reset,
    auto-cycle timer), `MinuteWatch`, `CountdownTick`, `shouldAutoCycleNow()`,
    `shownHealthStep()`, `TouchEdge`, `UiView` (`makeUiView()`, `tapContextOf()`, `netRows()`,
    `netCpaAt()`, `autoCycleInputAt()`) and `UiFlow`, which holds them for the loop
    (`takeNetEvents()`, `updateHealth()`, `screenChanged()`, `apply(ServiceOutcome)`,
    `pageTap()`, `tick()`, `takeRedrawMask()`, `addWakeTimes()`).
  - `ota_screen_flow` (`OtaScreenFlow`, `holdsBacklight()`), `update_prompt_flow`
    (`UpdatePromptFlow`, `updatePromptGate()`), `alert_screen_flow` (`AlertScreenFlow` around
    `service_screen`'s `alertScreenStep()`), together in `service_screen_flows`
    (`ServiceScreenFlows`: the three flows + the `PendingCommand`, the sender injected; each pass →
    one `ServiceOutcome {ownsDisplay, clearPending, firstDraw, restartAutoCycle, swallowTouch,
    logQueueFull}`).
  - `password_reset_flow` (`shouldOfferPasswordReset()`, `PasswordResetFlow`: Hold → WaitRelease
    → Confirm → Finished, `sleepMs` 30 / 20 / 1500), `ui_perf` (`UiPerfMeter`), `screenshot_bridge`
    (`ScreenshotBridge<Lock>` and `ScreenshotReturn` split out of `screenshot.h`, plus
    `screenshotPrepareTarget()` and `screenshotUiPass()`; `screenshot.h` keeps the BMP / band /
    `?screen=` helpers `web_server` uses, in `admin/core/` since stage 8).
  - `admin/core/settings_form` — `FormArgs`, `applySettingsForm()`, `passwordProblem()`,
    `rejectedFieldsText()` (see "Settings page"); `handleSave()` is 22 lines of HTTP.

  Device halves: `display/adapters/service_flows` (`runOtaScreen()`, `openUpdatePrompt()` /
  `runUpdatePrompt()`, `showAlertScreen()` / `runAlertScreen()`, `runPasswordReset()` and
  `ServiceScreens : ServiceScreenFlows` — `otaPass()`, `dialogsPass()`, `openAlerts()`,
  `askAboutUpdateIfDue()`), `lgfx/ui_draw` (`applyRedraw(tft, mask, UiView)`,
  `drawSetupPrompt()`), `lgfx/screenshot_panel` (the bridge and return instances, the four
  `ScreenshotSource` functions, `screenshotPanelPass()`, `drawScreenshotDemo()`). Also new:
  `status_bar`'s `statusBarZonesNow()`, `ota_flash`'s `otaFlashRunningPendingVerify()`,
  `touch_xpt2046`'s `touchLogTap()`. The adapters see other contexts only through display / platform
  headers (the installed version comes in as a parameter, the demo states through
  `core_link.h`), so no new layer finding. `main.cpp` is 249 lines of wiring; its only plain
  static is `netSeen`. `LayoutTest` keeps it so: `test_main_cpp_is_under_250_lines`,
  `test_main_cpp_keeps_no_flow_state`, `test_stage_5_flows_left_main_cpp`,
  `test_web_server_save_route_has_no_form_logic`, `test_stage_5_services_have_their_tests`,
  `test_stage_5_moved_tests_follow_their_code`.
- **Stage 6** (settings per context, spec 12 §7, and the page assets as files, §4.5; owner
  2026-11-07: the plan's defaults). `Settings` (`admin/core/settings.h`) has no flat field left;
  it is the aggregate of nine value objects, each declared in its context, header-only, with the
  old defaults as member initializers (⚠️ gnu++11 on `cyd`: not aggregates there — assign
  members, never `X{…}`):

  | `Settings::` | Type | Header |
  |---|---|---|
  | `home` | `Home {lat, lon, radius_deg, configured}` | `airspace/core/home.h` |
  | `phase` | `PhaseThresholds {near_airport_km, climb_rate_threshold_mps}` | `airspace/core/flight_phase.h` |
  | `opensky` | `OpenSkyCredentials {client_id, client_secret}` | `opensky/core/opensky_settings.h` |
  | `poll` | `PollSettings {interval_s}` | `opensky/core/opensky_settings.h` |
  | `display` | `DisplaySettings {language, last_screen}` | `display/core/display_settings.h` |
  | `auto_cycle` | `AutoCycleSettings {enabled, interval_s}` | `display/core/display_settings.h` |
  | `backlight` | `BacklightSettings {brightness_*, night_*, night_pause_polling}` | `display/core/backlight.h` |
  | `update` | `UpdateSettings {check_enabled, show_prerelease, skipped_tag}` | `admin/core/settings.h` |
  | `web` | `WebSettings {language, password}` | `admin/core/settings.h` |

  The radius is in `Home` (not §7's `PollSettings`): the radar rings, the `NetState` diff and the
  alerts read it too, and the display never sees `opensky` (docs/history.md "Tooling and docs").
  `sanitizeSettings()`, the clamps, `defaultSettings()`, `loadSettings()` / `saveSettings()` and the
  backup stay in `admin`; the NVS keys and the backup JSON are flat and unchanged. Code outside
  `admin/` takes only its objects: `fetchAircraftStates(creds, home)`,
  `openSkyClientPoll(now, creds, home, poll)`, `openSkyAuthStatus(creds)`,
  `openSkyMsUntilNextPoll(now, poll)`, `backlightHandleTouch()` / `backlightStatus(const
  BacklightSettings &)`, `backlightLoop(tft, backlight, home, fullBright)`, `PipelineParams {Home
  home; PhaseThresholds phase; …}`, `releaseCheckBegin(const UpdateSettings &)`,
  `nightMinutesToday()` / `pausedMinutesPerDay(const BacklightSettings &, …)`. `backlight.h` and
  `opensky_client.h` include `home.h` (core → `airspace/core` is allowed), so their adapters get
  `Home` without a new finding; `settings.h` is included only by `admin/`, the roots and
  `core_link.h`. `settings_form.h`'s parsed credentials file is `CredentialsFile {ok, creds}`.
  `LayoutTest` keeps it so:
  `test_config_is_built_from_context_objects`, `test_settings_nvs_keys_and_types_unchanged` (28
  keys, their `Preferences` types), `test_only_admin_roots_and_core_link_include_settings`,
  `test_config_type_named_only_in_admin_roots_and_core_link`,
  `test_context_functions_take_their_settings_objects`,
  `test_display_reads_only_its_settings_from_net_state`. Page assets: `src/admin/assets/` +
  `tools/embed_assets.py` → `admin/core/web_assets.h` (see "Settings page").
- **Stage 7** (glossary names, spec 12 §3; owner 2026-11-07: the plan's defaults). Renames only,
  one glossary entry per commit, each done by a one-off lexer tool (docs/history.md "Tooling and docs"):

  | Was | Now | Where |
  |---|---|---|
  | `BoundingBox`, `computeBoundingBox()` | `ScanArea`, `computeScanArea()` | `opensky/core/scan_area.h` |
  | `OpenSkyHealth` | `FeedHealth` | `airspace/core/feed_health.h` |
  | `DataSnapshot`, `buildDataSnapshot()` | `AirspaceSnapshot`, `buildAirspaceSnapshot()` | `airspace/core/airspace_snapshot.h`, `poll_pipeline` |
  | `LookupPorts` (§4.1) | `EnrichmentPort` | `airspace/services/poll_pipeline.h` |
  | `AircraftRow` | `TrackedFlight` | `airspace/core/tracked_flight.h` |
  | `Aircraft` | `AircraftState` | `airspace/core/aircraft_state.h` |
  | `configPortal*()` (8) | `webServer*()` (`Begin`, `Loop`, `IsActive`, `SaveCount`, `SetScreenshotSource`, `SetOtaObserver`, `SetPerfSource`, `RestartPending`) | `admin/adapters/web_server.h` |
  | `Config` | `Settings` | `admin/core/settings.h` |
  | `defaultConfig()`, `sanitizeConfig()`, `loadConfig()`, `saveConfig()`, `reloadConfig()`, `configToBackupJson()`, `restoreConfigFromBackupJson()`, `makeConfig()` | `…Settings()`, `settingsToBackupJson()`, `restoreSettingsFromBackupJson()`, `makeSettings()` | `settings`, `config_backup`, `net_task`, `fixtures.h` |
  | `NetState::cfg`, `cfgSeq` | `NetState::settings`, `settingsSeq` | `platform/core_link.h` |
  | file `admin/core/config_store.{h,cpp}`, `config_store_test.cpp` | `admin/core/settings.{h,cpp}`, `settings_test.cpp` (`run_settings_tests()`) | file rename |

  Kept on purpose: locals and parameters named `cfg` (`const Settings &cfg`), `ConfigPageModel`
  and the `config_page` / `config_backup` modules (the page's established name), `CpaPrediction`
  (§3's "Overflight" concept), `RouteInfo` / `AirportInfo` (symmetric with `AircraftInfo`),
  `openSkyCreditCost()` and `AlertId` / `AlertSet` (no single type behind §3's "CreditCost" /
  "Alert"), names derived from "aircraft" / "row" (`makeAircraft()`, `fetchAircraftStates()`,
  `pickNowAircraft()`, `sortRowsForDisplay()`, `makeRow()`, `TrackedFlight::aircraft`), the "bbox"
  of OpenSky's query (`bboxAreaSqDeg()`), and the §4 file names that differ (`cpa_predictor`,
  `poll_pipeline`, `screen_nav`, `touch_input`, `backlight`, `opensky_client`; §4 follows the code). WiFiManager's `setConfigPortalBlocking()` / `getConfigPortalActive()` /
  `setConfigPortalTimeout()` and the SDK's `configTzTime()` are library names. Strings never
  changed: NVS keys, backup JSON keys, routes, log text, hexdb.io's `"Aircraft not found."` and
  the `docs/data-sources.md "Aircraft identification"` references. Proof: built with a fixed
  `FW_VERSION`, every rename commit's sections were identical to the step before and
  `firmware.bin` differed only in the image hashes (≤ 65 B); the file rename only moved the
  link order (same sections, same 1 348 224 B, RAM unchanged). `check_comments.py`'s
  `RENAMED_SYMBOLS` reports a compound old name that comes back in code or a comment (outside
  `"quoted text"`: "X was renamed Y in spec 12 stage 7"). `LayoutTest`: `test_settings_module_is_admin_core_settings`,
  `test_net_state_carries_settings`, `test_web_server_functions_are_named_after_the_module`.
- **Stage 8** (enforcement; owner 2026-11-08). The 12 remaining findings were removed in the code;
  the dependency table gained no tick and no exception. Moved: `Language` → `shared/language.h`,
  `AircraftInfo` / `RouteInfo` / `AirportInfo` → `shared/flight_info`, `AirspaceSnapshot` →
  `airspace/core/airspace_snapshot.h`, `ScanArea` / `computeScanArea()` → `opensky/core/scan_area`,
  `kUpdatePromptTimeoutMs` → `service_screen.h`, `screenshot` → `admin/core/`,
  `alert_sources.cpp` → `main/`. `web_server` reads quota, login state and light through
  `panelLiveInputs()`. Split, behaviour unchanged (the five page renders are `cmp` identical):
  `net_task.cpp` 419 → 283 lines (`net_perf`, `net_enrichment`, `net_rollback`), `config_page.cpp`
  897 → 355 (`config_page_tabs`, `config_page_writer.h`), `web_server.cpp` 659 → 525
  (`web_server_firmware.cpp`). After an upload the page text has `→` (`otaVersionChangePageText()`), the `[ota]` serial
  line `->` (`otaVersionChangeLogText()`), both in `ota_update`. `LayoutTest` pins it: `test_source_files_stay_under_the_section_10_limits`,
  `test_web_server_routes_keep_their_guards` (every POST: `requireSameOrigin()`, then
  `authorize()`; every upload callback: `isMultipartRequest()`; every route `secured<>`),
  `test_web_server_includes_only_its_own_context`, `test_net_task_parts_live_in_the_composition_root`,
  `test_config_page_is_split_by_tab`.
- **Includes** are full paths from `src/` (`#include "display/core/i18n.h"`), in `src/` and
  `test/` alike; `-I src` is in `env:cyd` and `env:native` (`cyd-ota`, `native-asan` inherit).
  `test/support/` headers and test-local headers (`github_fixtures.h`) stay short.
- **`native` filter:** `+<*> -<main/> -<*/adapters/> -<platform/*_esp32.cpp>` — no exception
  since stage 4 (`config_portal.cpp` was re-added until its three pure functions moved to
  `settings_form`, tested by `settings_form_test.cpp`). `-<*/adapters/>` covers
  `display/adapters/lgfx/` too; `-<platform/*_esp32.cpp>` matches nothing yet and stays for
  later platform splits.
- **`tools/check_layers.py`** classifies each file (`classify()`, `LAYERS = ("core", "services",
  "adapters", "assets")`, root `main`) and checks every `#include` against the §5 table
  (`ALLOWED`): `core` sees `shared/` and other cores as the table allows, `*/services` and
  `*/adapters` their own context (`{ctx}/core`, `{ctx}/services`), tests anything but
  `adapters/`, `main/` and SDK headers; SDK headers (`Arduino.h`, `WiFi*.h`, `LovyanGFX.hpp`,
  `esp_*`, `freertos/…` …) only in `adapters/`, `main/` and inside `#ifdef ARDUINO` in
  `platform/`. A file in an old `domain/` / `app/` folder is "outside any context and layer".
  Stage-1 decisions: the §5 footnotes count as allowed; the table's missing rows were filled in
  (`enrichment/services` may see `airspace` core / services — it implements its port;
  `opensky/services` only its own context). Output `path:line: from -> to ("include")`, a count
  per pair and a total; exit 0, `--strict` exits 1 on any finding (the CI step).
- **Findings: 0.** They went 55, 51, 47, 14, 14, 12, 12, 0 over stages 1–8 (per stage: spec 12
  §8 "Wynik etapu N"). `LayoutTest.test_layer_findings_do_not_exceed_the_stage_limit` has
  `LAYER_FINDINGS_LIMIT = 0`. A short include (resolving only from the file's own folder) and a
  file outside any context are findings too; `LayoutTest` keeps the tree free of them, of old
  `domain/` / `app/` paths in code, build files and tools, and keeps the `RUN_TEST(` count ≥ 874.
- **The one-off migration tools** (`rewrite_includes.py`, `rename_symbols.py`) were deleted in
  stage 8 with their tests and CI lines; git has them.

## Concurrency — two cores (core_link, net_task)

Spec: `spec/done/04-dual-core.md`. Rules (what may run where) are in CLAUDE.md "Concurrency —
two cores"; this is how it works.

- **Core 0 — `net_task`** (FreeRTOS task, 16 KB stack, priority 1, next to WiFi and lwIP):
  `wifi_manager`, `time_sync` (NTP), `web_server` (WebServer + mDNS), `opensky_client`,
  the hexdb.io lookups with their caches, `poll_pipeline`, `release_check`, both OTA paths,
  marking the image valid. It owns the in-memory `Settings` and reloads it after a settings save
  (`webServerSaveCount()`); it hands each module only its settings objects (`PipelineParams`
  gets `home` + `phase`, OpenSky `opensky` + `home` + `poll`, the release check `update`). Each pass ends with `vTaskDelay(kNetIdleSleepMs = 5)`.
- **Core 1 — `loop()` in `main.cpp`** (Arduino loopTask, default 8 KB stack): `tft`, touch,
  `screen_nav`, redraw, `backlight`, the update question, service screens. `main.cpp` only wires:
  the decisions are in `display/services` (`UiFlow`, `ServiceScreenFlows`, `UiPerfMeter`), the
  drawing in `display/adapters` (`ui_draw`, `service_flows`, `screenshot_panel`). It **sleeps**
  between passes (`ulTaskNotifyTake`, `uiSleepMs()`: touch poll every 20 ms — one GPIO read
  without a finger, LovyanGFX checks the XPT2046 IRQ pin first — the countdown second, the clock
  minute, the prompt / alert timers from `UiFlow::addWakeTimes()` and
  `ServiceScreens::addWakeTimes()`); `net_task` wakes it when it publishes.
- **One UI pass:** `UiPerfMeter::tick()`, the UI heartbeat, the pending command's retry, the
  newest `NetState` (`UiFlow::takeNetEvents()`, then `updateHealth()`), the backlight. Then
  `serviceScreensPass()`, first come first: the OTA screen (`ServiceScreens::otaPass()`), a
  screenshot (`screenshotPanelPass()`), the update question and the alert message
  (`dialogsPass()`); each answers a `ServiceOutcome` that `UiFlow::apply()` turns into dropped
  events / a full redraw / a fresh auto-cycle interval / a swallowed touch. If none owns the
  display, `screensPass()`: a new press (`TouchEdge`) either only wakes a dark screen or goes
  through `routeTap()`; the update question is asked once when its gate opens; `UiFlow::tick()`
  (countdown second, clock minute, auto-cycle); `applyRedraw()` with `takeRedrawMask()`.
- **Three channels, nothing else shared** (`core_link`):
  1. `NetState` net → UI: an immutable snapshot (`shared_ptr<const NetState>`) in a
     `LatestBox` — latest wins, the lock only swaps a pointer, the old value is freed *after*
     unlocking (tested). Rows live in an `AirspaceSnapshot` shared between consecutive NetStates, so
     a health / update / OTA change never copies them. `net_task` publishes only when
     `netStateDiffers()`; the UI turns a new one into redraw events with `diffNetState()`.
     The settings ride along as one `shared_ptr<const Settings> settings` (a new one per save, no
     per-object copies); the UI reads only `home`, `display`, `auto_cycle`, `backlight` and
     `update.skipped_tag` from it (`LayoutTest.test_display_reads_only_its_settings_from_net_state`).
  2. `NetCommand` UI → net: a FreeRTOS queue of 8 POD commands (`None` by default,
     `InstallTag` = YES on the update question, `SkipTag` = NO); never blocks. A command the
     full queue refuses waits in a one-slot `PendingCommand` (`submitCommand()`, a newer one
     replaces it, held by `ServiceScreenFlows`) and is retried at the top of every `loop()` pass
     (`retryCommand()` → `retryPendingCommand()`), logged once: `[update] command queue full,
     retrying`.
  3. Atomics / `Guarded<T>` (a POD copied under a spinlock): `uiBeatMs`, `netBeatMs`,
     `pollingAllowed` (the night pause, decided by the UI), UI perf numbers, and the backlight
     status `/light.json` reads.
- **Poll → two snapshots** (`poll_pipeline`, pure, `buildAirspaceSnapshot()`): phase A right after
  `/states/all` with no lookup budget (cache only, the rest `...`), phase B with the budgets;
  B is published only if it fetched something (`AirspaceSnapshot::fetches`). Results are memoised
  per poll so B doesn't repeat A's lookups.
- **Settings page between phase-B lookups:** `EnrichmentPort::betweenFetches` is called after
  every *uncached* lookup (a real hexdb.io request; never in phase A). `net_task` serves the web
  page there (`serveBetweenFetches(busy, webServerLoop, updateStarted)` — no nesting; false
  when `webServerRestartPending()` or an OTA is in progress). False = stop: no more uncached
  lookups in this snapshot (cached keys still resolve, the rest stays `...`); a stopped B is
  still published if it fetched anything. A save made there reaches the in-memory `Settings` only after the poll
  (`seenSaveCount`). The web handler runs nested inside `pollOpenSky()` — watch
  `[perf] stack net=` against the 16 KB stack.
- **hexdb.io keep-alive** (`hexdb_http`): one TLS session per phase B (`hexdbSessionEnd()`
  after it); a request on a connection the server closed is retried once, fresh. HTTPClient's
  destructor `stop()`s the socket, so the HTTPClient lives as long as the session.
- **Screenshots** — `/screenshot.bmp` is the one web route that needs the panel. The handler
  asks the UI for bands of 16 rows into two buffers (`ScreenshotSource::begin / requestBand /
  waitBand / end` = `screenshotPanelBegin()` … in the `screenshot_panel` adapter, forwarding to
  a `ScreenshotBridge<SpinLock>`, pure and header-only in `screenshot_bridge.h`) — the UI reads
  band k+1 while band k goes out (`screenshotUiPass()`, called by `screenshotPanelPass()`). Every
  `begin()` / `requestBand()` takes a new **generation** under the lock; a timeout or `end()`
  sets it to 0. The UI (`takeJob()` / `writeRow()` / `finishJob()`) copies a row or marks a band
  done only while its job's generation is still current, so a late band can neither write into
  a freed buffer nor leave `BandDone` behind. The UI doesn't draw while a capture runs, and
  draws again once the bridge is idle or nothing happened for > 5000 ms (`kCaptureIdleMs`,
  wrap-safe). The native test runs handler and UI on two `std::thread`s with random timeouts
  (ASan in CI).
- **`?screen=N`** switches the display only for the capture (spec 10): `ScreenshotReturn`
  (`screenshot_bridge.h`, UI side) remembers the screen shown before — only for a real switch (0..2 and
  not the current one; the demo screens 3 = update question and 4 = alert screen leave
  nothing) and only the first of a capture series — and `afterPass(capturing)` gives it back
  once capturing ends; `screenshotPanelPass()` returns it as `backTo` and `main.cpp`
  `nav.set()`s it with `ScreenChangeCause::Screenshot`: repainted, never written to NVS. What a
  capture prepares is `screenshotPrepareTarget()`: a screen 0..2, the update question demo
  (`kScreenCount`) or the alert screen demo (`kScreenCount + 1`). An alert screen closed by a
  capture stays closed (`ServiceScreenFlows::closeAlertScreen()`).
- **Liveness:** no `netBeatMs` for 60 s → the header shows a network error (stale data isn't
  presented as fresh); no `uiBeatMs` for 2 s → a new image isn't marked valid and is restarted
  (see "Firmware updates (OTA)").
- **Heap:** ~143 KB free; largest block measured from 40 948 to 69 620 B, and it can settle near
  41 KB within minutes of boot, cut by small long-lived allocations (docs/history.md
  "Concurrency and resources"). The NOW countdown sprite (39 360 B: 320 × 123 at 8 bit) is
  created in `setup()` (`flightScreenReserveSprite()`) before WiFi and `net_task` allocate. On
  the board a verified GitHub session completed from a block of ~43 KB, its peak not measured; a
  full download from ~41 KB is unproven. Thresholds: "New versions from GitHub (release_check)".
- **Perf** (`perf_stats`, pure): the UI (`ui_perf`'s `UiPerfMeter`) measures how much later
  than planned each pass starts (drawing time + oversleep) and hands each 60 s window to
  `core_link::uiPerf` with its stack high-water mark; `net_task` logs once a minute
  `[perf] ui gap max=85ms p99<20ms slow=0 | net busy 20.3% max=807ms | stack ui=… net=… | heap …
  | cache ac=40 rt=38 ap=12` and serves the same as `GET /perf.json` (`"cache": {ac, rt, ap}` =
  entries in the hexdb.io RAM caches).

## Settings page (web_server, config_page, config_backup)

- `web_server` (`admin/adapters/`, was `config_portal`) runs the `WebServer` + mDNS
  (`cyd-sky.local`) on core 0; what a save does with the form, the form's checks
  (`isValidFloatString()`, `isValidUnsignedIntString()`) and the credentials-file parser
  (`parseOpenSkyCredentialsJson()`) are the pure `settings_form` (`admin/core/`), settings storage
  is `settings_nvs`. The HTML is rendered by the pure **`config_page`** (`renderConfigPage(model,
  sink)`, native-tested) and sent by `sendPage()` in ~1.4 KB chunks (see CLAUDE.md "Concurrency —
  two cores": send in large pieces).
- **Save** (`POST /save`, `handleSave()`): origin check, `authorize()`, the request's args into a
  `FormArgs` (the first value of a name counts, like `WebServer::arg()`; query-string args are in
  it too), `applySettingsForm(cfg, args)` — changes the `Settings` **in place** (no second copy on
  core 0's stack, the handler runs nested in a poll) and returns `SettingsFormOutcome {rejected,
  password, message}` — then `saveSettings()` and the page again. One table `kNumberFields[]`
  (`{name, label, apply}`, form order; plain functions, not lambdas, so it stays
  constant-initialized in .rodata under gnu++11) covers the 14 fields that can be rejected (7
  numbers, 5 clamped brightness / night fields, 2 clock times): a value that doesn't parse keeps
  the old one and its label goes into `rejected` in form order, listed by
  `rejectedFieldsText(rejected, web_language)` after `WebStr::SavedWithInvalid` ("Zapisano, ale te
  pola były błędne …"). The clamped five are clamped even when not sent; a missing checkbox =
  unchecked, a missing text field = unchanged, an empty `client_secret` keeps the saved one; a
  refused password is listed under `passwordProblem()`'s label. Sets `home.configured`.
- **A save NVS refuses** is reported, not answered as saved. `saveSettings()` and the single-key
  saves return `bool` (`NvsWriteCheck` counts the refused values; a failed open counts too) and
  log `[settings] saving <what> failed: N value(s) not stored (NVS full?)`. `/save`, `/lang`, the
  credentials upload and `/restore` then answer `WebStr::SaveFailed` (PL + EN) as an error, ahead
  of every other message; the values may be partly old, because keys are written one by one. No
  answer to change, so log only: `saveLastScreen`, `/update/prerelease`, "skip this version". The
  room for a save is the lookup store's reserve (docs/data-sources.md "Caching and the per-poll
  budget").
- **Five tabs:** **Location** (lat/lon, radius + credits per request), **OpenSky client**
  (account pill, client id/secret, credentials-file upload, poll interval + credits per day vs.
  the limit), **General** (display + web-panel language, display brightness / night mode,
  auto-cycle, flight-phase thresholds, panel password), **Backup**, **Firmware**. One Save for
  the first three (hidden on Backup and Firmware); the active tab round-trips via `?tab=` / a
  hidden field. The saved `client_secret` is **never written into the page** (empty field +
  "saved" placeholder; empty on save = keep it; a `clear_credentials` checkbox removes both).
- **Look:** dark (`#17181c`), muted greys, thin borders, the display's peach `#ff9966` as the
  only accent (active tab, buttons, focus) with dark text on it. Alerts `--alert:#ff3333`,
  `--warn:#ff8000`.
- **Assets as files** (spec 12 §4.5): the page's CSS, script and icon are
  `src/admin/assets/` — `style.css`, `app.js` (tabs, live credits, the "Now" line, upload
  progress), `restart.js` (reload once the board is back after an update), `check_pending.js`
  (while "Check now" waits) and `favicon.svg` (the same file as `site/favicon.svg`, a test keeps
  them equal). `tools/embed_assets.py` turns them into `src/admin/core/web_assets.h`
  (`kWebStyleCss`, `kWebAppJs`, `kWebRestartJs`, `kWebCheckPendingJs`, `kWebFaviconUri`; one C
  literal per source line), which only `config_page.cpp` includes. The page stays **one inlined
  HTML response** — `<link rel="icon" href="data:…">`, `<style>` + CSS, `<script>` + the
  `T={…}` texts from `webTr()` + the JS — byte for byte what the hand-written literals were, no
  routes per asset (docs/history.md "Tooling and docs"). The header is checked in and regenerated
  by a `pre:` script in `env:cyd` and `env:native` (written only when its content differs).
  **To change the look or the script:** edit the asset file and build (`pio run` or
  `pio test -e native`), or run `python tools/embed_assets.py`; commit the asset and the header
  together (`--check` exits 1 on a stale header, `RepoTest` fails in CI). Files are read as
  UTF-8, a BOM is dropped. Every line is trimmed and the lines are joined with **nothing** in
  between. What the generator refuses (an error naming `file:line`, nothing written):
  - a comment after code — comments only on their own line (`/* … */` in CSS, also over several
    lines; `//` in JS);
  - a line ending in a letter / digit / `_` / `$` / quote followed by one starting with one
    (`return` + `x`, `margin:0` + `auto` would glue);
  - in CSS, any line but the last that doesn't end in `;`, `{`, `}` or `,` — so a selector or a
    value is never split (`.card` + `.alert` would become `.card.alert`), and the last
    declaration of a block split over lines ends with `;` too.

  Other JS joins (`}` + `x.onerror=…` without a `;`, `a=b` + `++c`) aren't refused by the
  generator; `test_embed_assets.py` runs `node --check` on each joined script (skipped when
  `node` isn't on PATH; CI has it). So: end every JS statement with `;`, no regex or template
  literal that needs a newline. Text strings belong in `web_i18n`, not in the JS. The SVG
  becomes `data:image/svg+xml,` + the markup with `"` → `'`, `#` → `%23`.
- **Page language** (`Settings::web.language`, NVS `wlang`, default PL), independent of the
  display's: PL/EN switch in the header — two small `<form method="post" action="/lang">`
  (hidden `l` + `tab`), and a select on the General tab. Strings: the `web_i18n` X-macro table
  (`WEB_I18N_STRINGS`, `webTr()`); they go into HTML/JS raw, so `web_i18n_test.cpp` forbids
  the characters `"`, `<`, `>`, backslash and `&` in them.
- **Backup / restore** (`config_backup`, pure): `GET /backup.json[?secrets=1]` downloads all
  settings as JSON (grouped like the tabs, `"format": "cyd-sky-tracker-config"`,
  `format_version` 1; OpenSky credentials and the panel password (`"security":
  {"web_password"}`) only with `secrets=1`). `POST /restore` (multipart `backup_file`) applies
  such a file on top of the current settings — missing keys keep their value, wrong-typed keys
  are skipped and listed, other JSON is rejected; then `saveSettings()` (sanitizes) and a reload.
- Other routes: `GET /screenshot.bmp[?screen=N]` (24-bit BMP of the panel, see "Concurrency —
  two cores"), `GET /perf.json`, `GET /light.json`.

## Firmware updates (OTA) & panel password

Spec: `spec/done/01-ota-updates.md`. Released in v1.1.0.

- **Upload:** `POST /update[?size=N]` (multipart field `firmware`) — the Firmware tab (XHR with
  a progress bar; a plain form post works too) or `pio run -e cyd-ota -t upload`
  (`curl --digest`, password from `$CYD_WEB_PASSWORD`; ⚠️ with an empty password it fails with
  HTTP 400 and uploads nothing — use `curl -F firmware=@<file> http://cyd-sky.local/update`,
  docs/history.md "Tooling and docs"). The file is the CI's
  `cyd-sky-tracker-<version>.bin` (app only). `OtaSession` checks the header before anything
  reaches `Update`, and calls `Update.end()` (which switches the boot slot) only when the file
  carried the **firmware marker** `CYDSKY-FW1|board=<FW_BOARD>|v=<FW_VERSION>` (`version.cpp`,
  `firmwareMarker()`; `FW_BOARD` defaults to `cyd-r` — builds for other CYD variants will set it
  per env and reject a mismatch). `firmware.yml` greps the built `.bin` for it. Builds older
  than v1.1.0 have no marker: going back to them needs USB / the web installer.
- **Image checks** (`ota_update`, pure, behind an `OtaWriter` seam — `Update` in `ota_flash` on the board, a
  fake in tests): `checkImageHeader()` wants the 0xE9 magic and this chip's id, and refuses a
  leading `0xFF` (the merged `-full.bin`, meant for 0x0); `checkImageSize()`;
  `FirmwareMarkerScanner` finds the marker even across chunk boundaries.
- **Display:** `web_server` and `github_http` draw nothing; their `OtaObserver` (`ota_update.h`) callbacks
  (start / every 16 KB / end, on core 0) go to `net_task`, which publishes them as
  `NetState::ota` (`OtaProgress`, phase + seq); on the UI, `OtaScreenFlow` (`ota_screen_flow`)
  decides from it what to draw — frame, bar or result — and `runOtaScreen()` (`service_flows`)
  draws `service_screen`. The
  16 KB callback also does `vTaskDelay(1)` (core 0's watchdog). Success: answer page, ~1.5 s,
  `ESP.restart()` (`webServerRestartPending()` keeps `net_task` off the network meanwhile;
  the UI keeps the OTA screen via `NetState::restartPending`). Failure: "update failed" for 5 s,
  then a full redraw.
- **Rollback:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in this arduino-esp32 (2.0.17).
  `main.cpp` overrides `verifyRollbackLater()` → true; `net_task` calls
  `esp_ota_mark_app_valid_cancel_rollback()` once `shouldMarkAppValid()` (pending verify,
  ≥ 30 s up, settings page reachable or the WiFi setup hotspot up, **and the UI loop alive** —
  `core_link::uiBeatMs` < 2 s old). A new image whose UI loop stops while still on probation is
  restarted (`shouldRestartForRollback()`), so the bootloader goes back to the previous slot.
  OpenSky trouble never rolls back. Verified with an image that `abort()`s after 8 s and one
  whose UI loop hangs after 8 s: both came back on the previous slot.
- **Password:** `Settings::web.password` (NVS `wpass`), optional (default none);
  `validateWebPassword()` accepts 8–64 printable ASCII characters, `""` = no password.
  When set, every
  route calls `authorize()` first: HTTP Digest (`WebServer::authenticate`, realm
  `CYD Sky Tracker`, user `admin` — Basic accepted too); `AuthThrottle` (5 wrong in a row →
  60 s lock) → 429 + `Retry-After`. Upload callbacks check credentials *before* buffering or
  writing anything. Forgotten password: finger on the screen at power-up, 5 s, YES
  (`PasswordResetFlow` in `password_reset_flow`, run by `runPasswordReset()` in `setup()` before
  `net_task` starts; timing in `web_auth::resetHoldState()`).

### Web panel security — cross-site requests and framing

Spec: `spec/done/10-web-panel-security.md`. A password doesn't stop CSRF — a browser sends its
stored Digest login with a foreign page's form too.

- Every POST handler (`/save`, `/restore`, `/upload_credentials`, `/update`, `/update/check`,
  `/update/install`, `/update/prerelease`, `/lang`) calls `requireSameOrigin()` **before**
  `authorize()`: `checkRequestOrigin(Origin, Referer, Host)` (`web_auth`, pure) — Origin
  decides, else Referer's host; `http://` only, exact case-insensitive host[:port] match, `:80`
  = no port; `null`, `https://`, userinfo (`cyd-sky.local@evil`), look-alike suffixes, an
  Origin with a path or an empty Host → `CrossOrigin` → 403 `Cross-site request refused` + log
  `[auth] cross-origin POST /save from http://evil.example refused`. **Neither header**
  (`NoHeaders`: curl, `pio run -e cyd-ota -t upload`) is allowed.
- Upload callbacks decide at `UPLOAD_FILE_START` with `decideUploadStart(origin,
  credentialsOk)` (origin first): a cross-site upload buffers nothing and never starts
  `OtaSession`.
- `GET /lang` answers 405 + `Allow: POST` — a GET must not change anything.
- **No framing:** every response (route wrapper `secured<Handler>`; 401 / 429 / 403 / BMP /
  JSON included) carries `kSecurityHeaders`: `X-Frame-Options: DENY` and
  `Content-Security-Policy: frame-ancestors 'none'`. No `Access-Control-*` header anywhere
  (tested).
- ⚠️ **arduino-esp32 2.x `WebServer`:** a route registered with an upload callback gets that
  callback for **any** POST body; a non-multipart one (e.g. the empty probe `curl --digest`
  sends first) arrives as "raw", and `server.upload()` then dereferences null and crashes the
  board. Every upload callback starts with `if (!isMultipartRequest()) return;` (needs
  `Content-Type` in `collectHeaders()`), and handlers take the buffered file with `takeUpload()`
  so a later request can't reuse a stale one. `collectHeaders()` *replaces* the list — it is
  `Content-Type, Origin, Referer, Host`; dropping `Content-Type` brings the crash back.

## New versions from GitHub (release_check)

Spec: `spec/done/02-github-release-check.md`.

- **Check:** `GET api.github.com/repos/<FW_UPDATE_REPO>/releases/latest` 2 min after boot, then
  daily (`If-None-Match` ETag → a 304 doesn't count against GitHub's 60/h). Only between OpenSky
  polls and with heap to spare (`canStartNetworkTask()`: WiFi, clock synced, ≥ 5 s to the next
  poll, largest free block ≥ `kUpdateMinFreeBlock`, 50 KB). With the block settled below that,
  scheduled checks and *Check now* don't run. Setting `update.check_enabled` (NVS `updchk`,
  default on) only *checks*; installing always needs a YES. A failed connect is retried once.
- **TLS is verified here** (unlike OpenSky / hexdb.io): `src/admin/adapters/github_ca.h` holds
  the *root* CAs (Sectigo E46/R46 + USERTrust ECC/RSA for github.com / api.github.com, ISRG
  X1/X2 for release-assets.githubusercontent.com). `firmware.yml` warns when a host stops verifying
  against them — then add the new root and release. Every URL and every redirect hop must pass
  `isAllowedUpdateUrl()` (https, GitHub hosts, our repo's paths) before connecting; redirects
  are followed by hand. The repo is a compile-time constant, never a setting.
- **Install:** `browser_download_url` of `releaseAssetName(tag, board)`
  (`cyd-sky-tracker-<tag>.bin` for `cyd-r`) → 302 → release-assets.githubusercontent.com,
  streamed into the shared `otaFlashSession()` with `OtaExpectations{sha256 (asset "digest",
  else SHA256SUMS.txt), version (= tag, checked against the marker)}`; `Update.end()` only when
  all match. No hash = no install. Runs in `net_task`'s loop (not inside the HTTP handler),
  ~20 s for 1.36 MB, OTA screen titled "POBIERANIE Z GITHUBA".
- **An install asked for by hand has its own gate** (`stepPendingInstall()`, each pass of
  `releaseCheckLoop()`; page and YES on the display share the queue): WiFi, clock synced, no OTA
  running, largest block ≥ `kUpdateInstallMinFreeBlock` (32 KB). `enabled` and the time to the
  next poll don't count. Held back: one log line `[update] install <tag> held back: wifi=…
  clock=… ota busy=…, largest block N (needs 32768)`. After `kUpdateInstallWaitMs` (60 s) it is
  dropped: `… gave up, it could not start: …`, `OtaResult::InstallNotStarted`
  (`WebStr::OtaInstallNotStarted`) in `lastInstall`. The Firmware tab shows it on its next load;
  the page's own reload comes ~5 s after the click, so that takes a manual reload. After a YES on
  the display a give-up shows nothing there, and the question doesn't return that boot.
- **Display question:** `shouldShowUpdatePrompt()` — only `Available` (a release newer than a
  plain release build), once per boot, not for a development build, not over the NOW countdown,
  not under a finger, not while the screen is dark or the alert screen is open (`PromptGate`).
  YES installs (`NetCommand::InstallTag`); NO stores `update.skipped_tag` (NVS `updskip`, not
  in backups); 30 s = later (next boot). Screenshot: `/screenshot.bmp?screen=3` (demo data,
  `updateDemoState()`).
- **Settings page** (Firmware tab, first card): installed / latest / status / checked time,
  *Install*, *Check now* (`POST /update/check`), *Show development versions*
  (`POST /update/prerelease`, NVS `updpre`, default off) which adds the newest GitHub
  pre-release above the latest release with *Install* (`POST /update/install tag=…` accepts
  only the shown tags). Development versions are never offered on the display.
- **Check now and the switch only queue the check** (spec 09; `ManualCheck`:
  `askManualCheck()` → Queued / AlreadyQueued / TooSoon, `takeManualCheck(gateOpen)`,
  `manualCheckPending()`): the TLS check never runs inside the HTTP handler.
  `releaseCheckLoop()` runs it after `pollOpenSky()`, before the scheduled check; a request not
  run within `kUpdateManualCheckExpiryMs` (60 s, gate closed: no heap / clock) expires. Once a
  minute, counted from a check that actually *ran*; a second click within it answers with the
  page + "Sprawdzono przed chwilą — …" (`WebStr::UpdCheckTooSoon`). While pending
  (`ConfigPageModel::updateCheckPending`) the pill reads "Checking GitHub…" and the Firmware tab
  reloads every 3 s.
- **Development versions = tags with a suffix** (`v1.3.0-rc.1`, `-beta.N`, `-dev.N`):
  `release.yml` marks them pre-release and skips the project page; `build_site.py` gives them
  no changelog entry. Release one by hand: `git tag v1.3.0-rc.1 && git push origin v1.3.0-rc.1`.
  Every PR gets one automatically (`pr.yml`). The board parses only `vX.Y.Z-dev.N` with **one**
  number (uint16), so PR builds use the workflow run number as N and put the PR number in the
  release *title*.
- Screenshots `update_prompt.png`, `web_firmware*.png`: `python tools/screenshots.py --device
  <board>` (web pages from `config_page_test.cpp` in `test_admin` with `SCREENSHOT_OUT`,
  headless Chrome; the display one from the board — LovyanGFX can't build for the PC without
  SDL).

## Brightness & night mode (backlight, sun_times)

Spec: `spec/done/03-brightness-night-mode.md`.

- **Backlight:** `tft.setBrightness()` (LovyanGFX `Light_PWM`, GPIO21). LovyanGFX starts at
  127; `backlightBegin()` sets 255 right after `tft.init()`.
- **Light sensor:** LDR on GPIO34 (ADC1), read at **11 dB**. Measured on the board: a lit room
  reads ~0 (single spikes up to ~150), darkness ~1100–1300 — higher = darker. 0 dB saturates at
  4095 in the dark. In the dark the backlight shines into the sensor (under a cloth: PWM 255 →
  ~260, 127 → ~450, 10 → ~1100, 0 → ~1260). Not compensated: it pushes the right way (darker →
  dimmer → reads darker), so the screen settles at its minimum. Curve constants in
  `backlight.h` (`kLightRawBright` 40, `kLightRawDark` 1000): fixed per board, not settings.
- **Pure logic** (`backlight`): `LightFilter` (median of 5 + EMA, 2 s warm-up at full
  brightness), `autoBrightnessPct()` (linear between the two constants, 5 % hysteresis),
  `pctToPwm()` (min duty 8 + quadratic), `stepPwm()` ramps (1 s by day, 3 s into / out of the
  night, a wake-up jumps), `nightWindowFor()` / `isNight()` (Off / by the clock, through
  midnight / sunset → sunrise, polar day = never, polar night = always), `decideBacklight()`,
  `onTouchWake()`, `shouldPollOpenSky()`, `lightStatusJson()`. `sun_times` = the sunrise
  equation (UTC minutes; `shiftSunTimes()` to local), tested against api.sunrise-sunset.org
  (±3 min). `refreshSunToday(SunToday &, synced, utc, local, home)` keeps today's times and
  recomputes only when the local date, the **UTC offset** or home changes — so a change of
  zone (set after boot, DST day) is picked up at once; the adapter (`backlight_ldr`) logs
  `[light] sun today: …` when it returns true.
- ⚠️ A software reset keeps the system clock, so `isTimeSynced()` is true before NTP;
  `setup()` sets the zone first (`timeSyncSetZone()`, docs/screens.md "Time — the header
  clock"), otherwise local time is UTC until `configTzTime()` runs.
- **Rules:** a service screen (OTA progress / failed, update question, GitHub install) gets
  100 % at once (`backlightFullNow()` from the OTA observers). No night without a synced clock,
  and the night never brightens the screen (min of day and night level). At night with
  `night_pct` 0 the backlight is off: the first tap only lights it (`max(min, 10 %)`) for
  `night_wake_s` and is **not** passed to `routeTap()` (`ui_flow`). A tap at night always
  restarts the wake time. `night_pause_polling` (default off) skips `openSkyClientPoll()` in `net_task` (via the
  `core_link::pollingAllowed` flag the UI sets) during the night window except within the wake
  time. Auto-cycle stops and the update question isn't shown while the screen is dark
  (`PromptGate::screenOff`).
- **Settings** (General tab, "Display" card; backup group `display`): `brightness_auto`
  (default on), min / max (10 / 100 %) or fixed (80 %), `night_mode` (default **sunset to
  sunrise**), night from / to (23:00 / 06:30), `night_pct` (default 10 %, 0 = off),
  `night_wake_s` (60 s, 10–600), `night_pause_polling` (off). The card's "Now" line comes from
  `GET /light.json` every 2 s. The OpenSky tab's credits/day estimate subtracts the night window
  when pausing is on.
- **Log:** `[light] raw=… avg=… -> N% (pwm P)`, `[light] night (sun): 10%`,
  `[light] sun today: sunrise 07:17, sunset 17:25`,
  `[light] touch at night: awake for 60 s (screen lit)`, `[light] polling paused (night)` /
  `resumed (touch)`.

## Device alerts — the "!" (device_alerts)

Spec: `spec/done/08-device-alerts.md`. One way for the device to say "this needs your
attention", separate from the health dot (the dot = the data feed right now; the "!" =
something to fix or to know, even while data flows — e.g. anonymous after a failed login:
green dot, red "!").

- **Derived, never remembered** (`collectAlerts(AlertInputs)`, pure): an alert disappears by
  itself when its cause does; nothing to acknowledge, closing the message doesn't clear it.
  Alerts: `OpenSkyLoginFailed` (error, `OpenSkyAuthState::Mode::LoginFailed`),
  `OpenSkyQuotaExhausted` (error, last answer a 429), `OpenSkyBudgetOverLimit` (warning,
  `openSkyDailyCredits()` > `openSkyDailyLimit(auth)` — shown right after saving such settings,
  before the credits run out).
- **A new alert** = a row in `kAlertDefs[]` (severity, `onDisplay`, settings tab, `Str` title,
  `WebStr` title + action, log code) + a rule in `collectAlerts()` + texts in `i18n.h` /
  `web_i18n.h` (PL + EN; numbers via `fillTemplate()` "{}" placeholders) + the body / why case
  in `alertDisplayBody()` / `alertWebWhy()`. Drawing code doesn't change.
- **Flow:** `net_task` derives them every pass (`deviceAlertInputs(cfg)` in `alert_sources` =
  `openSkyAuthStatus()`, `openSkyQuota()`, the night pause) into `NetState::alerts` /
  `alertDetails`; logs each change once (`[alert] + opensky_budget_over_limit`).
  `netStateDiffers()` ignores `quotaObservedMs`; `diffNetState()` sets
  `RedrawEvents::alertsChanged` only when the badge (`displaySeverity()`) changes →
  `kRedrawHeader`.
- **Header badge:** see screens.md "Screen chrome" (`alertBadgeColor()`, `statusBarZones(...,
  badge)` → `alert_x`; the count re-centres between badge and date).
- **Tap → message screen** (`service_screen`): `alertBadgeHit()` (`screen_nav`) = x 0 .. badge
  + 8 px, y 0..24, checked first in `routeTap()` (`TapKind::OpenAlerts`, only with home
  set). Full screen: "UWAGA", up to 3 entries
  (errors first; more → "+ N więcej — zobacz panel WWW"), "Szczegóły: http://cyd-sky.local",
  **ZAMKNIJ 15 s** button (36 px tall). Pure state machine `alertScreenStep()`: closes on a
  fresh tap on the button (after the opening finger lifted, `acceptPromptTap()`), after exactly
  15 s, or at once when no display alert is left; the 1 Hz countdown repaints only the button.
  `AlertScreenFlow` (`alert_screen_flow`) wraps it for the loop (list redraw on change, the
  finger swallowed while open and on the closing pass, full redraw on close).
  While open: no auto-cycle (restarts on close), no update question
  (`PromptGate::alertScreenOpen`); OTA closes it. At night with the screen off the first tap
  only wakes it. `/screenshot.bmp?screen=4` = demo with all three alerts.
- **Web panel:** `.card.alert` above the tab bar (on every tab) when `alerts.any()`: what the
  "!" means, then per alert the title, `alertWebWhy()` (HTTP code, renewal time, minimum
  interval `minPollIntervalForLimit()`), the action and "→ Przejdź do: <tab>". `sendPage()`
  computes them with the same `deviceAlertInputs()`. The "!" circle is `.bang`; the red box
  for "no panel password" is `.nopw`.
- The display's "!" message and the alert card stay generic; login details (HTTP code, since
  when, next try) are only on the OpenSky tab (data-sources.md "OpenSky login failure").

## Versioning & CI

- **Version = git tag.** `tools/pio_version.py` (PlatformIO `extra_scripts = pre:`) runs
  `git describe --tags --always --dirty` (or takes `$FW_VERSION`) and passes it as the
  `FW_VERSION` macro to `src/admin/core/firmware/version.cpp` only (build middleware on the
  pattern `*version.cpp`, which must match one file; a new version recompiles only it).
  `firmwareVersion()` returns it, `"dev"` when not injected (native tests). Shown only in
  the settings page footer, not on the display. The script also writes
  `.pio/build/cyd/cyd-sky-tracker-<version>.bin` (copy of firmware.bin) and `version.txt`.
- **Second pre-script:** `tools/embed_assets.py` (`pre:` in `env:cyd` after `pio_version.py`,
  and in `env:native`; `cyd-ota` / `native-asan` inherit) regenerates `src/admin/core/web_assets.h`
  from `src/admin/assets/` when it differs (see "Settings page"); a broken asset stops the build
  with `file:line`.
- **Partitions:** `min_spiffs.csv` — two 1.875 MB OTA slots, NVS at the same offset as before.
  The firmware (~1.35 MB) must stay under one slot; the efont subset (CLAUDE.md "Language") and
  the build without exceptions help it fit.
- **Build flags:** `env:cyd` (and `cyd-ota`, which extends it) has `build_unflags = -fexceptions`
  and `-fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables` — no `.eh_frame`, no
  exception tables (−133 408 B, why: docs/history.md "Tooling and docs"). Nothing may use `try` /
  `catch` / `throw`; an error that would throw (`std::bad_alloc`) aborts, as an uncaught
  exception did before. `native` / `native-asan` keep the default flags.
- **Pinned versions:** platform and libraries are pinned in `platformio.ini` so CI and local
  builds match — bump them deliberately (and rerun `tools/subset_efont.py` after a LovyanGFX
  bump).
- **GitHub Actions — four workflows** (`.github/workflows/`):
  - `firmware.yml` — push to main / manual, and reusable (`workflow_call`, inputs `version` =
    forced `FW_VERSION`, `ref` = commit): the blocking layer check (`tools/check_layers.py --strict`,
    before the tests — "Source layout"), `pio test -e native`, `pio test -e native-asan`,
    the Python tool tests (`tools/test_check_bdd.py`, `test_release_plan.py`,
    `test_build_site.py`, `test_check_comments.py`, `test_check_layers.py`,
    `test_subset_efont.py`,
    `test_embed_assets.py` — its `RepoTest`
    fails on a stale `web_assets.h`), the BDD check (`tools/check_bdd.py`),
    the comment check (`tools/check_comments.py`, warnings only),
    builds, uploads artifact `cyd-sky-tracker-<version>` with `cyd-sky-tracker-<version>.bin`
    (app, OTA or 0x10000), `…-full.bin` (merged image, 0x0), `bootloader.bin`,
    `partitions.bin`, `boot_app0.bin`, `version.txt`, `SHA256SUMS.txt`.
  - `site.yml` — changes under `site/`, `docs/screenshots`, `docs/icons` on main / manual, and
    reusable: builds the project page and deploys it to Pages. On its own it takes the
    installer's firmware from the **latest GitHub Release** (no release → page without the
    installer); called with `firmware-artifact` it uses that artifact. A push of a **release
    commit** to main (head commit subject starts with `Release v`) doesn't deploy (`build`
    job's `if:`); `release.yml` does, with that release's firmware. The condition must not use
    `github.event_name` — in `release.yml`'s `workflow_call` that is the tag push, so it would
    skip the release deploy too (`SiteWorkflowDeployTest` in `tools/test_build_site.py`).
  - `release.yml` — tags `v*`: calls `firmware.yml`, creates the GitHub Release with its
    files, then calls `site.yml` with the same artifact.
  - `pr.yml` — every push to a PR against main: version = next patch after the latest release
    + `-dev.<run number>` (`v1.4.2` → `v1.4.3-dev.57`), `firmware.yml` on the PR's head commit
    with it, then (same-repo PRs only) a pre-release `v1.4.3-dev.57 · PR #12` (the PR's
    previous one is deleted first) and one PR comment kept up to date. Closing the PR deletes
    its dev releases and tags. Tags made with `GITHUB_TOKEN` don't trigger `release.yml` — on
    purpose.
- **Release by hand** (normally `/release`): on main after the merge,
  `git switch main && git pull --ff-only && git tag -a v1.7.0 -m v1.7.0 && git push origin v1.7.0`.

### Project page (site/, build_site.py)

GitHub Pages, https://alasota.github.io/CYD-Flight-Tracker/. Source in `site/` (static
HTML/CSS/JS, PL/EN — every text is a `.en` / `.pl` element pair, CSS shows one; light,
humorous tone).

- `tools/build_site.py` assembles `_site/` = `site/` + `docs/screenshots` + `docs/icons` +
  `firmware/` (bootloader, partitions, boot_app0, app + `manifest.json` for the ESP Web Tools
  install button). The manifest lists the four parts separately, **not** the merged -full.bin,
  so an update without "Erase device" keeps NVS (settings).
- **"What's next"** is generated from **`ROADMAP.md`** (replaces `<!-- ROADMAP -->`):
  `## Planned` / `## In progress` / `## Done` headings, each with a rated table, rows in
  priority order — `# | Item | Value | Effort | Risk | Rating | Verdict`. Item and Verdict cells
  carry their Polish text in an inline `<!-- pl: … -->` comment; Value / Effort / Risk use fixed
  words (high/medium/low, small/medium/large) that `build_site.py` translates (`WORDS_PL`);
  Rating is ⭐ stars. Columns are looked up by name. In **Done** the Verdict column becomes
  **Version / Wersja** (`done_version()`: "Done in v1.6.0: …" → a `v1.6.0` link to
  `#changelog`, a `vNEXT` placeholder → "the next release" without a link, no version → the
  full verdict).
- **Version badge** (`<!-- VERSION -->`) and **"What changed"** (`<!-- CHANGELOG -->`) come
  from git, **grouped by minor version** (`group_by_minor()`: v1.6.0 – v1.6.4 = one
  `<details class="card">`, newest first; only stable `vX.Y.Z` tags, a dev tag's commits land
  in the next release's). Summary: version, tag range, newest release's date, number of
  changes, release / diff links. The 2 newest groups are open (`CHANGELOG_OPEN`), older ones
  fold to one line with their first roadmap item. Inside: first the ROADMAP **Done** rows of
  that minor (`roadmap_done_by_version()`, linking to `#roadmap`), then the commits folded under
  "Commits (N)", max 15 + a GitHub compare link. Behind-the-scenes commits (tests, docs, specs,
  harness, release tooling — the `CHORE` regex, `is_chore()`) are greyed (`li.chore`), not
  hidden; "Fix: …" isn't a chore. So every released feature needs a ROADMAP Done row, and
  commit subjects must read well in a changelog (shown as they are). Hence `site.yml` checks
  out with `fetch-depth: 0`. The pure parts are tested by `tools/test_build_site.py`.
- **Hand-written sections** of `site/index.html` besides the intro: `#screens`, `#features`
  ("What else it does": 2×2 tiles — the "!", updates from GitHub, brightness / night
  (`id="night-mode"`), backup — each a two-sentence summary with a screenshot or not, the long
  story in a native `<details>` "More"), `#panel` (the settings page: a tab viewer,
  `role="tablist"`, one screenshot at a time in a frame of max 640 px that scrolls inside, a
  caption beside it; `site.js` handles click / arrow keys / Home / End), "First boot" (3 steps
  + password), "For the curious". Without JS the tab row is hidden and all panels show one
  under another (`html.js` gates it).
- ⚠️ `site.js` and the inline head script set the language with `classList`
  (`remove('en','pl')` / `add(lang)`), never `className =` — that would wipe the `js` class and
  every panel would reappear. Grids use `minmax(0, 1fr)`: a plain `1fr` column grew to its
  min-content (nowrap changelog summaries, roadmap tables) and the 375 px page scrolled
  sideways.
- `site/img/device.jpg` is a placeholder for a real photo: 1600×1000 px JPG, ≤ ~300 KB.

## Spec → PR harness (details)

The summary is in CLAUDE.md "Spec → PR harness"; shared agent rules in
`.claude/harness/conventions.md`.

- **`/implement 05` / `/implement #12`** (`.claude/skills/implement/SKILL.md`, the
  orchestrator) runs `spec-analyst` (plan + BDD `.feature`, then **stops for the owner's
  approval**) → `test-author` (failing tests first) → `implementer` → `docs-writer` (docs,
  screenshots, spec to `done/`) → `reviewer` (up to two fix rounds) → PR from
  `.github/pull_request_template.md`. Hand-over files in `.claude/work/<slug>/` (git-ignored).
- **BDD:** `spec/**/NN-name.feature` next to its spec; every Scenario is tagged
  `@test=<suite>/<file>.cpp:<function>` (a Unity test that must exist and be `RUN_TEST`ed),
  `@test=tools/<file>.py:<Class>.<method>` (a Python `unittest` method `test*` in that
  top-level class) or `@manual` (checked on the board / in a browser, listed in the PR).
  `tools/check_bdd.py` enforces it in CI; `--list` / `--manual <feature>` print the PR tables.
- **Branches** `spec/NN-name` / `issue/N-name` / `fix/…` / `release/vX.Y.Z`; the agents never
  commit to main or merge — merging stays with the owner. `main` takes changes only through PRs
  (ruleset: PR + `firmware / build` green + linear history, rebase merges — the agents' commit
  subjects become the changelog).
- **`/fix <description | #issue>`** (`.claude/skills/fix/`): branch `fix/…`;
  `bug-investigator` finds the root cause and commits a **regression test that fails**
  (`// Regression: …`) → **stops for the owner's OK** on the diagnosis → `implementer`
  (smallest fix, the test turns green unchanged) → `docs-writer` fix mode (pitfalls only) →
  `reviewer` → PR "Fix: …" with label `bug`.
- **`/release [patch|minor|major]`** (`.claude/skills/release/`): `tools/release_plan.py`
  (tested by `tools/test_release_plan.py`) reads the highest stable tag and the PRs merged since
  → **minor** if any feature (`spec/…`, `issue/…`), **patch** for fixes only (`fix/…`, label
  `bug`) and other changes; major only on request. Owner confirms → a `release/vX.Y.Z` PR if
  docs need it (`docs-writer` release mode: specs still in `planned/` to `done/`, the `vNEXT`
  placeholder → the version, ROADMAP rows, project page / README for released features),
  rebase-merged when green → annotated tag on main's HEAD → watches `release.yml`.
- **Specs built in stages** (spec 12): one spec, one PR per stage on the same `spec/NN-…`
  branch name. The status line `Status: **etap N z M …` with N < M keeps the spec in
  `planned/` even though its branch was merged (`release_plan.py`'s `STAGE_STATUS`); a
  `**zrobione` status still moves it. Each stage gets a "Wynik etapu N" in the spec and its
  version in `spec/README.md`'s "Stan" (`etap 1/8: v1.11.0, etap 2/8: vX.Y.Z`).
- **The `vNEXT` placeholder:** branches don't know their version, so docs write a bare
  placeholder (ROADMAP's "Done in" rows, spec status, `spec/README.md`, `docs/*.md`);
  `/release` fills it in (`release_plan.py --fill vX.Y.Z`), the project page shows it as "the
  next release" meanwhile (`build_site.py`'s `split_cell()`). In backticks it's text about the
  placeholder and is left alone — always write it that way when *explaining* it.
- PR creation, merging and releases use `gh` (installed, logged in as alasota); without it
  `/implement` / `/fix` leave a compare link + `pr.md`, `/release` stops.
