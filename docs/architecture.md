# Architecture — how the firmware is put together

The current architecture in detail: the source layout, the two cores, the settings page,
updates, brightness, the "!" alerts, CI and the agent harness. The rules a session must follow
are in CLAUDE.md; this file is the reference behind them. (Spec 12 reorganises the code into
contexts in seven stages; stage 1 — folders, full-path includes, layer report — is done. Stage 7
rewrites this file around the contexts.)

## Source layout (spec 12)

Spec: `spec/planned/12-code-reorganization-ddd.md` (§4 contexts, §5 dependency table, §8
stages). Stage 1 moved the files with `git mv`, **unsplit and unrenamed**; stages 2–7 move types,
split pure / hardware halves, add the `app/` layer, rename to the glossary and make the layer
check block CI.

- **Folders:** `src/<context>/<layer>/` — contexts `airspace` (flight phase, CPA, the poll
  pipeline), `enrichment` (hexdb.io lookups, caches, names), `opensky` (client, login, credits),
  `display` (screens, theme, fonts, i18n, touch, backlight, redraw), `admin` (settings, page,
  backup, password, alerts; `admin/domain/firmware/` = OTA, release check, version); layers
  `domain/` (pure), `app/` (use cases, pure), `adapters/` (SDK / hardware only). Plus `shared/`
  (`sun_times`), `platform/` (`core_link`, `time_sync`, `wifi_manager`, `perf_stats`,
  `net_config`) and the root `src/app/` (`main.cpp`, `net_task` — composition roots, one per
  core). Subfolders: `display/domain/screens/`, `display/domain/theme/` (+ `fonts/`).
- **Placement rule in stage 1:** a file sits where its pure half will live, named as before; its
  `#ifdef ARDUINO` half stays inside until stage 3 moves it to `adapters/`. Only files that are
  entirely hardware are in `adapters/` / root `app/` already (`enrichment/adapters/hexdb_http`,
  `admin/adapters/github_ca.h`, `app/net_task`, `app/main.cpp`). `include/LGFX_CYD.hpp` and
  `include/touch_calibration.h` stay in `include/` until stage 3.
- **Includes** are full paths from `src/` (`#include "display/domain/i18n.h"`), in `src/` and
  `test/` alike; `-I src` is in `env:cyd` and `env:native` (`cyd-ota`, `native-asan` inherit).
  `test/support/` headers and test-local headers (`github_fixtures.h`) stay short.
- **`native` filter:** `+<*> -<app/> -<*/adapters/> -<platform/*_esp32.cpp>` — `-<app/>` matches
  only the root `src/app/`, not `admin/app/`.
- **`tools/check_layers.py`** classifies each file (`classify()`) and checks every `#include`
  against the §5 table (`ALLOWED`): domain sees `shared/` and other domains as the table allows,
  `*/app` and `*/adapters` their own context, tests anything but `adapters/`, root `app/` and SDK
  headers; SDK headers (`Arduino.h`, `WiFi*.h`, `LovyanGFX.hpp`, `esp_*`, `freertos/…` …) only in
  `adapters/`, root `app/` and inside `#ifdef ARDUINO` in `platform/`. Stage-1 decisions: the §5
  footnotes count as allowed; the table's missing rows were filled in (`enrichment/app` may see
  `airspace` domain / app — it implements its port; `opensky/app` only its own context).
  Output `path:line: from -> to ("include")`, a count per pair and a total; exit 0, `--strict`
  exits 1 (stage 7 turns that on in CI). Stage 1 ends with **55 findings** — SDK includes inside
  `#ifdef` in domain files, `admin/domain -> admin/app` (`device_alerts`), domain → adapters
  (`hexdb_http.h`, `github_ca.h`, `LGFX_CYD.hpp`), domain / app → platform (`time_sync.h`,
  `core_link.h`, `net_config.h`); each later stage should lower it. A short include (resolving
  only from the file's own folder) and a file outside any context are findings too;
  `tools/test_check_layers.py`'s `LayoutTest` keeps the tree free of them and the `RUN_TEST(`
  count ≥ 874.
- **`tools/rewrite_includes.py`** — the move table (`MOVES`) and the include rewriter: `--move`
  (`git mv` per entry, skips done ones), default rewrites short includes in place, `--check` exits
  1 if anything would change. Kept until stage 7: stages 2 / 3 move files again with a new `MOVES`.

## Concurrency — two cores (core_link, net_task)

Spec: `spec/done/04-dual-core.md`. Rules (what may run where) are in CLAUDE.md "Concurrency —
two cores"; this is how it works.

- **Core 0 — `net_task`** (FreeRTOS task, 16 KB stack, priority 1, next to WiFi and lwIP):
  `wifi_manager`, `time_sync` (NTP), `config_portal` (WebServer + mDNS), `opensky_client`,
  the hexdb.io lookups with their caches, `poll_pipeline`, `release_check`, both OTA paths,
  marking the image valid. It owns the in-memory `Config` and reloads it after a settings save
  (`configPortalSaveCount()`). Each pass ends with `vTaskDelay(kNetIdleSleepMs = 5)`.
- **Core 1 — `loop()` in `main.cpp`** (Arduino loopTask, default 8 KB stack): `tft`, touch,
  `screen_nav`, redraw, `backlight`, the update question, service screens. It **sleeps**
  between passes (`ulTaskNotifyTake`, `uiSleepMs()`: touch poll every 20 ms — one GPIO read
  without a finger, LovyanGFX checks the XPT2046 IRQ pin first — the countdown second, the clock
  minute, the prompt timer); `net_task` wakes it when it publishes.
- **Three channels, nothing else shared** (`core_link`):
  1. `NetState` net → UI: an immutable snapshot (`shared_ptr<const NetState>`) in a
     `LatestBox` — latest wins, the lock only swaps a pointer, the old value is freed *after*
     unlocking (tested). Rows live in a `DataSnapshot` shared between consecutive NetStates, so
     a health / update / OTA change never copies them. `net_task` publishes only when
     `netStateDiffers()`; the UI turns a new one into redraw events with `diffNetState()`.
  2. `NetCommand` UI → net: a FreeRTOS queue of 8 POD commands (`None` by default,
     `InstallTag` = YES on the update question, `SkipTag` = NO); never blocks. A command the
     full queue refuses waits in a one-slot `PendingCommand` (`submitCommand()`, a newer one
     replaces it) and is retried at the top of every `loop()` pass (`retryPendingCommand()`),
     logged once: `[update] command queue full, retrying`.
  3. Atomics / `Guarded<T>` (a POD copied under a spinlock): `uiBeatMs`, `netBeatMs`,
     `pollingAllowed` (the night pause, decided by the UI), UI perf numbers, and the backlight
     status `/light.json` reads.
- **Poll → two snapshots** (`poll_pipeline`, pure, `buildDataSnapshot()`): phase A right after
  `/states/all` with no lookup budget (cache only, the rest `...`), phase B with the budgets;
  B is published only if it fetched something (`DataSnapshot::fetches`). Results are memoised
  per poll so B doesn't repeat A's lookups.
- **Settings page between phase-B lookups:** `LookupPorts::betweenFetches` is called after
  every *uncached* lookup (a real hexdb.io request; never in phase A). `net_task` serves the web
  page there (`serveBetweenFetches(busy, configPortalLoop, updateStarted)` — no nesting; false
  when `configPortalRestartPending()` or an OTA is in progress). False = stop: no more uncached
  lookups in this snapshot (cached keys still resolve, the rest stays `...`); a stopped B is
  still published if it fetched anything. A save made there reaches `cfg` only after the poll
  (`seenSaveCount`). The web handler runs nested inside `pollOpenSky()` — watch
  `[perf] stack net=` against the 16 KB stack.
- **hexdb.io keep-alive** (`hexdb_http`): one TLS session per phase B (`hexdbSessionEnd()`
  after it); a request on a connection the server closed is retried once, fresh. HTTPClient's
  destructor `stop()`s the socket, so the HTTPClient lives as long as the session.
- **Screenshots** — `/screenshot.bmp` is the one web route that needs the panel. The handler
  asks the UI for bands of 16 rows into two buffers (`ScreenshotSource::begin / requestBand /
  waitBand / end`, forwarded by `shot::` in `main.cpp` to a `ScreenshotBridge<SpinLock>`, pure
  and header-only in `screenshot.h`) — the UI reads band k+1 while band k goes out. Every
  `begin()` / `requestBand()` takes a new **generation** under the lock; a timeout or `end()`
  sets it to 0. The UI (`takeJob()` / `writeRow()` / `finishJob()`) copies a row or marks a band
  done only while its job's generation is still current, so a late band can neither write into
  a freed buffer nor leave `BandDone` behind. The UI doesn't draw while a capture runs, and
  draws again once the bridge is idle or nothing happened for > 5000 ms (`kCaptureIdleMs`,
  wrap-safe). The native test runs handler and UI on two `std::thread`s with random timeouts
  (ASan in CI).
- **`?screen=N`** switches the display only for the capture (spec 10): `ScreenshotReturn`
  (`screenshot.h`, UI side) remembers the screen shown before — only for a real switch (0..2 and
  not the current one; the demo screens 3 = update question and 4 = alert screen leave
  nothing) and only the first of a capture series — and `afterPass(capturing)` gives it back
  once capturing ends. `main.cpp` then `nav.set()`s it with `ScreenChangeCause::Screenshot`:
  repainted, never written to NVS. An alert screen closed by a capture stays closed.
- **Liveness:** no `netBeatMs` for 60 s → the header shows a network error (stale data isn't
  presented as fresh); no `uiBeatMs` for 2 s → a new image isn't marked valid and is restarted
  (see "Firmware updates (OTA)").
- **Heap:** ~143 KB free, largest block 57 KB (with ~8 aircraft). The NOW countdown sprite
  (~30 KB) is created in `setup()` (`flightScreenReserveSprite()`) before WiFi and `net_task`
  allocate, so the largest block stays above the 50 KB verified TLS (`release_check`) needs.
- **Perf** (`perf_stats`, pure): the UI measures how much later than planned each pass starts
  (drawing time + oversleep); `net_task` logs once a minute
  `[perf] ui gap max=85ms p99<20ms slow=0 | net busy 20.3% max=807ms | stack ui=… net=… | heap …
  | cache ac=40 rt=38 ap=12` and serves the same as `GET /perf.json` (`"cache": {ac, rt, ap}` =
  entries in the hexdb.io RAM caches).

## Settings page (config_portal, config_page, config_backup)

- `config_portal` runs the `WebServer` + mDNS (`cyd-sky.local`) on core 0. The HTML is rendered
  by the pure **`config_page`** (`renderConfigPage(model, sink)`, native-tested) and sent by
  `sendPage()` in ~1.4 KB chunks (see CLAUDE.md "Concurrency — two cores": send in large
  pieces).
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
- **Page language** (`Config::web_language`, NVS `wlang`, default PL), independent of the
  display's: PL/EN switch in the header — two small `<form method="post" action="/lang">`
  (hidden `l` + `tab`), and a select on the General tab. Strings: the `web_i18n` X-macro table
  (`WEB_I18N_STRINGS`, `webTr()`); they go into HTML/JS raw, so `web_i18n_test.cpp` forbids
  the characters `"`, `<`, `>`, backslash and `&` in them.
- **Backup / restore** (`config_backup`, pure): `GET /backup.json[?secrets=1]` downloads all
  settings as JSON (grouped like the tabs, `"format": "cyd-sky-tracker-config"`,
  `format_version` 1; OpenSky credentials and the panel password (`"security":
  {"web_password"}`) only with `secrets=1`). `POST /restore` (multipart `backup_file`) applies
  such a file on top of the current settings — missing keys keep their value, wrong-typed keys
  are skipped and listed, other JSON is rejected; then `saveConfig()` (sanitizes) and a reload.
- Other routes: `GET /screenshot.bmp[?screen=N]` (24-bit BMP of the panel, see "Concurrency —
  two cores"), `GET /perf.json`, `GET /light.json`.

## Firmware updates (OTA) & panel password

Spec: `spec/done/01-ota-updates.md`. Released in v1.1.0.

- **Upload:** `POST /update[?size=N]` (multipart field `firmware`) — the Firmware tab (XHR with
  a progress bar; a plain form post works too) or `pio run -e cyd-ota -t upload`
  (`curl --digest`, password from `$CYD_WEB_PASSWORD`). The file is the CI's
  `cyd-sky-tracker-<version>.bin` (app only). `OtaSession` checks the header before anything
  reaches `Update`, and calls `Update.end()` (which switches the boot slot) only when the file
  carried the **firmware marker** `CYDSKY-FW1|board=<FW_BOARD>|v=<FW_VERSION>` (`version.cpp`,
  `firmwareMarker()`; `FW_BOARD` defaults to `cyd-r` — builds for other CYD variants will set it
  per env and reject a mismatch). `firmware.yml` greps the built `.bin` for it. Builds older
  than v1.1.0 have no marker: going back to them needs USB / the web installer.
- **Image checks** (`ota_update`, pure, behind an `OtaWriter` seam — `Update` on the board, a
  fake in tests): `checkImageHeader()` wants the 0xE9 magic and this chip's id, and refuses a
  leading `0xFF` (the merged `-full.bin`, meant for 0x0); `checkImageSize()`;
  `FirmwareMarkerScanner` finds the marker even across chunk boundaries.
- **Display:** `config_portal` and `release_check` draw nothing; their `OtaObserver` callbacks
  (start / every 16 KB / end, on core 0) go to `net_task`, which publishes them as
  `NetState::ota` (`OtaProgress`, phase + seq); the UI draws `service_screen` from it. The
  16 KB callback also does `vTaskDelay(1)` (core 0's watchdog). Success: answer page, ~1.5 s,
  `ESP.restart()` (`configPortalRestartPending()` keeps `net_task` off the network meanwhile;
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
- **Password:** `Config::web_password` (NVS `wpass`), optional (default none);
  `validateWebPassword()` accepts 8–64 printable ASCII characters, `""` = no password.
  When set, every
  route calls `authorize()` first: HTTP Digest (`WebServer::authenticate`, realm
  `CYD Sky Tracker`, user `admin` — Basic accepted too); `AuthThrottle` (5 wrong in a row →
  60 s lock) → 429 + `Retry-After`. Upload callbacks check credentials *before* buffering or
  writing anything. Forgotten password: finger on the screen at power-up, 5 s, YES
  (`offerWebPasswordReset()`, timing in `web_auth::resetHoldState()`).

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
  poll, ≥ 50 KB free block). Setting `update_check_enabled` (NVS `updchk`, default on) only
  *checks*; installing always needs a YES. A failed connect is retried once.
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
- **Display question:** `shouldShowUpdatePrompt()` — only `Available` (a release newer than a
  plain release build), once per boot, not for a development build, not over the NOW countdown,
  not under a finger, not while the screen is dark or the alert screen is open (`PromptGate`).
  YES installs (`NetCommand::InstallTag`); NO stores `update_skipped_tag` (NVS `updskip`, not
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
  (±3 min).
- **Rules:** a service screen (OTA progress / failed, update question, GitHub install) gets
  100 % at once (`backlightFullNow()` from the OTA observers). No night without a synced clock,
  and the night never brightens the screen (min of day and night level). At night with
  `night_pct` 0 the backlight is off: the first tap only lights it (`max(min, 10 %)`) for
  `night_wake_s` and is **not** passed to `handleTap()`. A tap at night always restarts the wake
  time. `night_pause_polling` (default off) skips `openSkyClientPoll()` in `net_task` (via the
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
- **Flow:** `net_task` derives them every pass (`deviceAlertInputs(cfg)` =
  `openSkyAuthStatus()`, `openSkyQuota()`, the night pause) into `NetState::alerts` /
  `alertDetails`; logs each change once (`[alert] + opensky_budget_over_limit`).
  `netStateDiffers()` ignores `quotaObservedMs`; `diffNetState()` sets
  `RedrawEvents::alertsChanged` only when the badge (`displaySeverity()`) changes →
  `kRedrawHeader`.
- **Header badge:** see screens.md "Screen chrome" (`alertBadgeColor()`, `statusBarZones(...,
  badge)` → `alert_x`; the count re-centres between badge and date).
- **Tap → message screen** (`service_screen`): `alertBadgeHit()` (`screen_nav`) = x 0 .. badge
  + 8 px, y 0..24, checked first in `handleTap()`. Full screen: "UWAGA", up to 3 entries
  (errors first; more → "+ N więcej — zobacz panel WWW"), "Szczegóły: http://cyd-sky.local",
  **ZAMKNIJ 15 s** button (36 px tall). Pure state machine `alertScreenStep()`: closes on a
  fresh tap on the button (after the opening finger lifted, `acceptPromptTap()`), after exactly
  15 s, or at once when no display alert is left; the 1 Hz countdown repaints only the button.
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
  `FW_VERSION` macro to `src/admin/domain/firmware/version.cpp` only (build middleware on the
  pattern `*version.cpp`, which must match one file; a new version recompiles only it).
  `firmwareVersion()` returns it, `"dev"` when not injected (native tests). Shown only in
  the settings page footer, not on the display. The script also writes
  `.pio/build/cyd/cyd-sky-tracker-<version>.bin` (copy of firmware.bin) and `version.txt`.
- **Partitions:** `min_spiffs.csv` — two 1.875 MB OTA slots, NVS at the same offset as before.
  The firmware (~1.47 MB) must stay under one slot; the efont subset (CLAUDE.md "Language") is
  what makes it fit.
- **Pinned versions:** platform and libraries are pinned in `platformio.ini` so CI and local
  builds match — bump them deliberately (and rerun `tools/subset_efont.py` after a LovyanGFX
  bump).
- **GitHub Actions — four workflows** (`.github/workflows/`):
  - `firmware.yml` — push to main / manual, and reusable (`workflow_call`, inputs `version` =
    forced `FW_VERSION`, `ref` = commit): the layer report (`tools/check_layers.py`, report
    only, before the tests — "Source layout"), `pio test -e native`, `pio test -e native-asan`,
    the Python tool tests (`tools/test_check_bdd.py`, `test_release_plan.py`,
    `test_build_site.py`, `test_check_comments.py`, `test_check_layers.py`,
    `test_rewrite_includes.py`, `test_subset_efont.py`), the BDD check (`tools/check_bdd.py`),
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
  version in `spec/README.md`'s "Stan" (`etap 1/7: vX.Y.Z`).
- **The `vNEXT` placeholder:** branches don't know their version, so docs write a bare
  placeholder (ROADMAP's "Done in" rows, spec status, `spec/README.md`, `docs/*.md`);
  `/release` fills it in (`release_plan.py --fill vX.Y.Z`), the project page shows it as "the
  next release" meanwhile (`build_site.py`'s `split_cell()`). In backticks it's text about the
  placeholder and is left alone — always write it that way when *explaining* it.
- PR creation, merging and releases use `gh` (installed, logged in as alasota); without it
  `/implement` / `/fix` leave a compare link + `pr.md`, `/release` stops.
