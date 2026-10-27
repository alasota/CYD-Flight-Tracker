# History — decisions and why

What was tried, what went wrong, and why things are the way they are. The current state is in
[screens.md](screens.md), [data-sources.md](data-sources.md), [architecture.md](architecture.md)
and CLAUDE.md; this file keeps the reasons so nobody re-introduces an old mistake. Older
findings of the 2026 code review: [review-notes-2026.md](review-notes-2026.md).

## Display and layout

- **Header bar.** The first design had three zones: date/time left, screen name centre, WiFi
  bars right. Then a filled orange/peach block with the screen name (duplicating the active
  nav button), then a filled peach pill with the aircraft count, coloured by feed health. That
  pill looked exactly like the active nav button, the count sat glued to the date with a big gap
  before the time, and nobody could tell what its colour meant. Now (variant A): nothing filled,
  health is an explicit dot, peach fill is reserved for the active tab. WiFi bars were dropped
  with the first redesign; an `rssiToBars()` helper was specified but never had a place in the
  layout and was never implemented — spec 11 removed every mention of it.
- **Header height and content bounds.** The first pass of the three-screen spec left no real
  height for the bottom nav bar (content ran to y 238), so the bar rendered as a sliver. Content
  was cut to y 225 with a 25 px header, then (slim 15 px header, big nav tabs) to y 18..215.
  Old comments with `y:28..225`, "25px header" or a "19px" row step date from those versions.
- **Full-screen redraws.** The first Screen 2 called `fillScreen()` once a second to tick the
  countdown; the whole screen — frame, header, nav — blinked every second. Hence the redraw
  policy and the countdown sprite.
- **Frames and dividers.** Screen 1's featured panel, Screen 2's identity panel and Screen 3's
  summary had a magenta swept-corner "elbow" frame; Screen 3 had an orange vertical divider at
  x 192..200. Both removed on request. The helpers stayed as "unused primitives" for a while
  (`drawElbowFrame`, `drawElbow`, `elbowArcPoint`, `drawVerticalDivider`, `drawPanel`,
  `drawHeaderBlock`, the two-screen era's `drawViewToggleButton`) and were deleted in spec 11 —
  git has them if they are ever wanted again.
- **Colours.** The original palette was LCARS orange / magenta / cyan. Colour scheme "variant A"
  (warm chrome, cool content) replaced it on 2026-10-17. `LCARS_MAGENTA`, `LCARS_CYAN`,
  `LCARS_AMBER`, `LCARS_ROSE`, `LCARS_PALE_BLUE`, `LCARS_BLUE_VIOLET` were kept "for legacy" but
  nothing drew them; deleted in spec 11. `LCARS_ORANGE` survives as `THEME_WARNING` (spec 08).
- **Stardate.** An early header showed a decorative `STARDATE: XXXX.XX`; replaced by the real
  date, `computeStardate()` and its tests removed — the rule "unused code goes with its tests"
  comes from there.
- **Fonts.** LovyanGFX's `fonts::efontJA_*` carry ~10,800 glyphs each (~1.05 MB of flash
  together, mostly kanji); `tools/subset_efont.py` keeps only the needed glyphs (~4.4 KB), which
  is what lets the firmware fit two OTA slots. The header date moved from the 10 px efont to
  DejaVu12 because its dots looked like "+".

## Screen 1 — Flights

- **Table sub-header.** A thin cyan `LOTY` / `FLIGHTS` strip sat above the column header;
  removed on request, its space went to roomier rows. Rule kept from it: no text taller than the
  strip or row it sits in when drawn with a background colour.
- **Column header alignment.** The first implementation drew the labels as one pre-joined
  string from x 4, which drifted out of line with the data (`SKAD` at x ~100 over a column
  starting at x 160). Each label is now drawn at its column's x from the shared `kColumns`.
- **Airline column → distance.** The table had a LINIA (airline) column; the callsign already
  names the airline, so it was replaced by ODL (distance) — it tells you where to look.
- **Two placeholders.** A wall of `--` while data was merely loading read like a failure; now
  `...` = loading, `--` = looked up, nothing known.
- **On the ground is inferred.** Apron traffic near EPWA often arrives with `on_ground=false`
  and null altitude / speed. Trusting the flag put a parked LOT jet in the featured slot as
  "0m 0km/h" with an overflight icon — hence `inferOnGround()` and `has_altitude` /
  `has_velocity`.
- **Ground rows dimmed and sorted last.** "5km" listed after "23km" read like a sorting bug.
- **Stat chips on the ground.** `0m` / `1m/s` looked like a bug; now one `NA ZIEMI` chip. The
  first direction arrow was a solid ▼ without a stem — indistinguishable from the landing icon.

## Flight phase icons

- The first `classifyPhase()` skipped the `NONE` step and returned `OVERFLIGHT` for every
  distant aircraft, so the FAZA column showed ▶ almost everywhere. Fixed and pinned by tests.
- Icons went from ASCII `^ v > -` (too small; `phaseIcon()`, deleted in spec 11) to filled
  ▲ ▼ ▶ triangles (nobody read them as takeoff / landing) to the 16×12 pixel-art planes
  ("variant B" of the 2026-10-18 mock-ups).

## Screen 2 — NOW

- Renamed from FLIGHT to TERAZ / NOW on request; the code still says `kScreenFlight`.
- The original spec showed the *nearest* aircraft (same pick as Screen 1) with a two-line
  identity panel and an em-dash when empty. The first version then said "no flights in range"
  while a plane was landing 1 km away: the nearest one wasn't going to pass overhead. Now
  `pickNowAircraft()` picks by predicted closest approach, says `NIE PRZELECI NAD TOBĄ` when
  nothing passes, and the panel has a third (stats) line.

## Navigation and touch

- **Nav bar.** First drawn as 6 px pills (invisible on the board), then a 15 px bar with tiny
  labels (nobody could see or hit it), then three separate 25 px pill buttons (read as buttons,
  not tabs), now folder tabs.
- **Edge strips.** Left / right edge strips also switched screens (prev / next); removed on
  request so taps above the tab bar belong to the active screen. The very first touch code
  ("Milestone 9") used them.
- **Touch library.** XPT2046_Touchscreen forced a hack on the Arduino global `SPI` object and
  needed its rotation kept in sync by hand; replaced by LovyanGFX's `Touch_XPT2046`. Before the
  calibration was measured every tap landed in the header; the first LovyanGFX port forgot the
  horizontal inversion and mirrored the buttons (LOTY opened RADAR) — a native test pins the
  direction now.
- **Screen persistence.** Two screens used to be saved as `last_view`; it became `last_screen`
  (0/1/2). Before spec 10 any web page could flip screens through `/screenshot.bmp?screen=N`
  and wear the flash with NVS writes; now a screenshot switch is never saved.

## Data sources

- **HTTP/1.0 for /states/all.** The body is parsed straight from `getStream()`, the raw socket.
  With HTTP/1.1 the server may answer `Transfer-Encoding: chunked`; ArduinoJson then read the
  leading hex chunk size as a bare number, "succeeded", and every poll came back with 0
  aircraft.
- **Basic Auth.** OpenSky dropped username / password in March 2026; older tutorials and
  micro-radar still describe it.
- **Credits.** 15 s polls = 5760 credits/day, above an account's 4000, and the display simply
  said "no flights". Hence the credit tracking, the honest empty states and the budget alert.
- **Wrong credentials** (before spec 06): a typo in `client_secret` was silent — the token
  fetch failed, the poll went anonymous with a 200, the panel still said "account", the login
  server got asked every 15 s, and hours later the screen said "credits exhausted" (400 instead
  of 4000). `openSkyDailyCreditLimit(bool)` was replaced by `openSkyDailyLimit(auth)`.
- **Route endpoint.** The design doc suggested hexdb.io's IATA route endpoint (3-letter codes
  read better); the code uses the ICAO one because OpenSky callsigns are ICAO-style. IATA codes
  still show, from the airport records.
- **RAM caches.** Before spec 05 the hexdb.io RAM cache was an unbounded `std::map`: a few
  hundred new aircraft a day, so after days of uptime the heap's largest block fell below the
  50 KB `release_check` needs and update checks silently stopped. Now an LRU over NVS.
- **Lookup budget.** An ad-hoc `kMaxNewLookupsPerPoll = 5` lived untested in `main.cpp`; now
  `lookup_budget` / `poll_pipeline`, pure and tested.

## Concurrency and resources

- **One loop** (before spec 04): every HTTPS request froze the screen — measured ~0.8 s per
  OpenSky poll, up to 5.7 s with the settings page or the GitHub check. `loopTask` ran with a
  16 KB stack (`SET_LOOP_TASK_STACK_SIZE`) only for TLS; gone since the network moved to
  `net_task`. Comments that say "main.cpp reloads / keeps / budgets" date from then; spec 11
  fixed them.
- **Watchdog and the settings page.** v1.4.0 streamed the page as hundreds of tiny
  `sendContent()` calls (3 TCP writes each): ~8 s of a fully busy core 0 per page, and the task
  watchdog restarted the board whenever the page was opened. `sendPage()` now sends ~1.4 KB
  chunks (0.3 s per page).
- **Countdown sprite.** Created on the first visit to NOW, it split the largest heap block to
  41 KB, below the 50 KB verified TLS needs — update checks stopped until reboot. Now reserved
  in `setup()`.
- **Lost commands.** Before spec 09 a NO on the update question that met a full command queue
  was simply lost, and the question came back after a reboot. `NetCommandType::RefreshNow` was
  never sent; spec 11 removed it and made `None` the default.
- **Screenshot bridge** (spec 09 review): the old `cancelled` flag lost both races — a late
  `BandDone` overwrote `Idle` (5 s without drawing), and a new `begin()` cleared the flag while
  the UI was still writing into the previous request's freed buffer. Replaced by a generation
  counter.
- **Check now** (before spec 09) ran the TLS check inside the HTTP handler and held the page
  for seconds; now it is queued.
- **`GET /lang`** changed the page language (before spec 10) — an `<img src>` on any page could
  flip it. Now POST only.

## Brightness

- Before spec 03 the board ran at half brightness all the time (LovyanGFX starts at 127).

## Tooling and docs

- **Project page installer.** v1.7.0's page first went out with the v1.6.4 installer: the push
  deploy took the latest Release before the v1.7.0 one existed. Release commits no longer
  deploy on push; `release.yml` deploys with its own firmware.
- **Display driver.** The first plan used TFT_eSPI with `build_flags`; LovyanGFX (configured in
  C++, `include/LGFX_CYD.hpp`) replaced it. CLAUDE.md once carried a "platformio.ini starting
  point"; `platformio.ini` and `LGFX_CYD.hpp` are the truth now.
- **CLAUDE.md split** (spec 11): it had grown to ~1550 lines mixing rules, screen specs and
  history, and contradicted itself (25 vs 15 px header, an "orange block" in the status bar, the
  old Screen 2 spec next to its "current behaviour", `rssiToBars()` described as existing). It
  became a < 250-line rule book plus the files in `docs/`. `tools/check_comments.py` keeps the
  comments' doc references pointing at real headings.
