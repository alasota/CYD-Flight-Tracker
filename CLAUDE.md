# CLAUDE.md

## Project

**CYD Sky Tracker** — firmware for the ESP32-2432S028R ("Cheap Yellow Display" / CYD) that shows
live aircraft flying over a fixed home location, using the OpenSky Network REST API.

Architecture is inspired by **AnthonySturdy/micro-radar** (github.com/AnthonySturdy/micro-radar):
WiFiManager captive-portal onboarding, a local web config page reachable via mDNS, OpenSky
credentials stored on-device, a configurable scan radius. **The visual style is deliberately
different** — an LCARS-inspired (Star Trek console) look rather than micro-radar's plain
radar-scope. See "Visualization concept" and "Design language" below.

## Hardware

- Board: **ESP32-2432S028R** ("CYD"), ESP32-WROOM-32, 320x240 landscape TFT (ILI9341 driver),
  resistive touchscreen (XPT2046) on a separate SPI bus from the display.
- ⚠️ CYD variants differ. This pinout is for the classic **ILI9341 + resistive-touch "R" board**.
  If the touch doesn't respond or colors look wrong, the unit may be an ST7789 "C" (capacitive
  touch) variant — confirm before debugging pins, don't assume.
- Confirmed pinout (source: randomnerdtutorials.com CYD pinout guide):

  | Function      | GPIO |
  |---------------|------|
  | TFT_MISO      | 12   |
  | TFT_MOSI      | 13   |
  | TFT_SCLK      | 14   |
  | TFT_CS        | 15   |
  | TFT_DC        | 2    |
  | TFT_RST       | -1 (software reset only) |
  | TFT_BL (backlight) | 21 |
  | Touch CS      | 33   |
  | Touch IRQ     | 36   |
  | Touch MOSI    | 32   |
  | Touch MISO    | 39   |
  | Touch CLK     | 25   |

- Display driver: **LovyanGFX** (lovyan03). Configure it via a custom `LGFX_Device` subclass
  (`include/LGFX_CYD.hpp`) rather than editing library internals — keeps the repo reproducible
  and avoids "works on my machine" pin mismatches. Unlike TFT_eSPI, LovyanGFX is configured in
  C++ (bus/panel/light structs), not via preprocessor `build_flags`.
- Touch: **LovyanGFX's built-in XPT2046 driver** (`lgfx::Touch_XPT2046`), configured in
  `include/LGFX_CYD.hpp` — **no separate touch library** (XPT2046_Touchscreen was dropped: it
  forced a hack on the Arduino global `SPI` object and needed its rotation kept in sync by
  hand). The touch controller has its own SPI wiring, so it gets its own SPI host: display on
  `HSPI_HOST`, touch on `VSPI_HOST`, `bus_shared = false`. Read it with `tft.getTouch()` (screen
  coordinates, already following `tft.setRotation()`) / `tft.getTouchRaw()`. Calibration
  constants live in `include/touch_calibration.h`. Used for the bottom-nav buttons and table
  paging.
- `platformio.ini` board id: `esp32dev` — there is no distinct PlatformIO board entry for the
  CYD; all display config comes from the `LGFX_CYD.hpp` device class, not from the `board=`
  field.

## Data source: OpenSky Network REST API

Base URL: `https://opensky-network.org/api`. Endpoint used: `GET /states/all` with a bounding
box (`lamin`, `lomin`, `lamax`, `lomax`, decimal degrees) centered on the configured home
location.

### ⚠️ Authentication changed in March 2026 — do not use Basic Auth

OpenSky **exclusively** uses the OAuth2 `client_credentials` flow now. Username/password Basic
Auth (as referenced in older tutorials, and possibly in micro-radar's own README) **no longer
works**. Implement it like this:

1. User provides `client_id` + `client_secret` via the config portal (created at
   opensky-network.org → Account page). Store in NVS/Preferences, never in source.
2. To get a token:
   ```
   POST https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token
   Content-Type: application/x-www-form-urlencoded
   Body: grant_type=client_credentials&client_id=...&client_secret=...
   ```
   Response JSON has `access_token` and `expires_in` (~1800s).
3. Send `Authorization: Bearer <token>` on every `/states/all` call.
4. Cache the token and refresh **proactively** (e.g. using `millis()`, refresh ~60s before
   `expires_in` elapses) rather than waiting for a `401`. Treat a `401` as "refresh once and
   retry", not as a fatal error.
5. Anonymous (no credentials) access to `/states/all` still works with lower limits — the
   firmware must work anonymously too, OAuth is an optional upgrade path, same as in micro-radar.

### Rate limits / credits (check current values if this ever misbehaves — they can change)

- Anonymous: 400 credits/day. Authenticated standard user: 4000/day.
- Cost per `/states/all` call depends on bounding-box area: ≤25 sq° = 1 credit, 25–100 sq° =
  2, 100–400 sq° = 3, >400 sq° = 4.
- Keep the configurable radius defaulting to something that stays in the 1-credit tier (e.g.
  ±2.5° lat/lon ≈ 25 sq°), and surface the credit cost in the config UI so the user can see the
  trade-off when they increase the radius — same spirit as micro-radar's "2° radius soft cap".
- Default poll interval: 15s, must be configurable. Never hammer the API faster than the
  configured interval, even after a manual refresh (e.g. touch-triggered).
- A `429` response means credits are exhausted — back off using
  `X-Rate-Limit-Retry-After-Seconds` if present, otherwise a sane fixed backoff (e.g. 60s).
- **Credits left are tracked** (`opensky_quota`, pure + tested): every 200 carries
  `X-Rate-Limit-Remaining` (checked live), a 429 carries the seconds until the pool renews.
  `opensky_client` keeps the latest state (`openSkyQuota()`) and logs it
  (`ok: 12 aircraft, credits remaining=3971 (account)` / `rate limited (429): credits exhausted,
  pool renews in 25012s (~6 h 57 min), next try in 3600s` — the backoff itself stays capped at
  3600 s). The config page's OpenSky tab shows it (`quotaStatusText()`).
- **An empty screen says why** (`emptyStateText(health, shortForm)` in `status_bar`): only with
  a healthy feed does it read "BRAK LOTÓW W ZASIĘGU" / "BRAK LOTU"; with exhausted credits it's
  "LIMIT OPENSKY WYCZERPANY" / "LIMIT API", login failure "BŁĄD LOGOWANIA OPENSKY", no answer
  "BRAK DOSTĘPU DO OPENSKY" ("BŁĄD API" short). Stale aircraft from the last good poll are still
  shown as before; `main.cpp` repaints the content when the health changes and the list is empty.
  (Found the hard way: 15 s polls = 5760 credits/day > the account's 4000, and the display just
  said "no flights".) One way out: *Pause data fetching at night* (see "Brightness & night
  mode"); 23:00-06:30 at 15 s = 1800 requests less.

## Aircraft identification: airline + flight + model

This is the actual point of the project (settling "what plane is that" arguments), so it's not
optional polish — a raw OpenSky state vector only gives `icao24` and `callsign`, neither of
which is an airline name or aircraft type on its own. Enrichment is required.

- **Flight**: the `callsign` from `/states/all` (e.g. `LOT281`) is good enough as "which flight"
  — no extra lookup needed for this part.
- **Airline + aircraft type**: look up by `icao24` against **hexdb.io**, a free, keyless,
  community-run lookup:
  ```
  GET https://hexdb.io/api/v1/aircraft/{icao24}
  ```
  Returns JSON like:
  ```json
  {"ICAOTypeCode":"A319","Manufacturer":"Airbus","ModeS":"4010EE",
   "OperatorFlagCode":"EZY","RegisteredOwners":"easyJet Airline","Registration":"G-EZBZ",
   "Type":"A319 111"}
  ```
  Use `RegisteredOwners` as the airline name and `Type` (or `ICAOTypeCode` if you want the short
  form) as the aircraft model.
- **Not found**: returns a 404-style body (`{"status":"404","error":"Aircraft not found."}`) for
  aircraft outside its database (military, private, very new registrations). Handle this as
  "show callsign only, airline/type = unknown" — never crash or block the row on a miss.
- **This is a small hobby-run free service, not an SLA** — be a good citizen and don't hammer it:
  - **Cache every successful lookup by `icao24`** — an aircraft's airline/type essentially
    never changes, so a given `icao24` is looked up once, ever: every confirmed answer goes to
    NVS (`lookup_store`), and the **RAM cache is size-limited** (`lookup_cache`, LRU, the 96
    most recently used — spec 05). An entry that falls out of RAM comes back from NVS, never
    from hexdb.io. (Before spec 05 the RAM cache was an unbounded `std::map`: a few hundred new
    aircraft a day, so after days of uptime the heap's largest block fell below the 50 KB
    `release_check` needs and update checks silently stopped.) Re-fetching on every poll would
    be both rude and pointless.
  - Their stated informal limit is ~1000 requests / 5 minutes — trivially fine for one device
    with caching, so there's no excuse to skip the cache "because the limit is generous."
  - If a lookup is slow/unavailable, show the row with whatever's cached (or just the callsign)
    rather than blocking the whole table refresh on one flaky HTTP call.
- **Route + country lookup**: covered in its own section below ("Route & airport lookup") — it's
  no longer a nice-to-have, the table screen depends on it.

## Route & airport lookup: origin, destination, country codes

Needed for the "skąd/dokąd" columns and the featured panel's country codes (see
"Visualization concept" below). Both come from **hexdb.io** — same provider as aircraft
lookup, different endpoints. Verify these against the live API when implementing; documented
here from hexdb.io's own published endpoint list, not independently tested by hand:

- **Route** (origin/destination airport codes for a flight):
  ```
  GET https://hexdb.io/api/v1/route/iata/{callsign}
  ```
  Use the **IATA** variant by default — 3-letter codes (`WAW`, `FCO`) read better at a glance
  than 4-letter ICAO codes (`EPWA`, `LIRF`) for a "what plane is that" audience standing at a
  grill, and it costs the same one HTTP call either way. The ICAO variant
  (`.../route/icao/{callsign}`) is a documented fallback if IATA coverage turns out sparser in
  practice.
  Response: `{"flight":"EIN17A","route":"EIDW-EGLL","updatetime":1397991739}` — split `route` on
  `-` to get origin/destination codes. Not found: `{"status":"404","error":"Route not found."}`
  — common for general aviation / non-scheduled flights with no filed route. Show "—" for
  skąd/dokąd rather than blocking the row.
- **Airport → country code** (only needed for the featured/nearest aircraft's expanded view):
  ```
  GET https://hexdb.io/api/v1/airport/iata/{code}
  ```
  Response: `{"airport":"Heathrow Airport","country_code":"GB","iata":"LHR","icao":"EGLL",...}`
  — use `country_code`. Not found: `{"status":"404","error":"Airport not found."}` → show "—".
- **Caching, two different keys**:
  - Route lookups cache by **callsign** (a flight's route doesn't change mid-flight).
  - Airport/country lookups cache by **airport code** — this cache will end up tiny in practice
    (mostly the same handful of nearby airports repeating).
  - Both are NVS-backed (`lookup_store`) with a size-limited RAM cache in front (`lookup_cache`:
    96 routes, 64 airports), same as aircraft.
  - Same rule as `aircraft_lookup`: never re-fetch a cache hit, never block the table on one
    slow/failed request, degrade to "—" instead of hiding the row.

## Flight phase: takeoff / landing / overflight

Answers "kierunek" for aircraft near the airport — pure logic, no I/O, lives in its own
`flight_phase` module (see "Code conventions") so it's trivially unit-testable:

- Inputs: `distance_from_home_km`, `vertical_rate_mps` (from the OpenSky state vector),
  `on_ground` (from the same state vector), plus two configurable thresholds:
  `near_airport_km` (default ~15 km — tune after watching real approach/departure traffic, this
  default is a guess, not measured) and `climb_rate_threshold_mps` (default ~1.5 m/s).
- Classification (`Phase`: `NONE`, `TAKEOFF`, `LANDING`, `OVERFLIGHT`):
  - `on_ground == true` → `NONE` (don't show a phase for something still taxiing).
  - `distance_from_home_km > near_airport_km` → `NONE` — not near the airport, no icon.
  - else if `vertical_rate_mps > climb_rate_threshold_mps` → `TAKEOFF`.
  - else if `vertical_rate_mps < -climb_rate_threshold_mps` → `LANDING`.
  - else → `OVERFLIGHT` (near the airport but level — transiting through, not using it).
  (The first implementation skipped the `NONE` step and returned `OVERFLIGHT` for every distant
  aircraft, so the FAZA column showed ▶ almost everywhere — fixed and pinned by tests.)
- Icons — **16×12 px aircraft pixel art** (`lcars_theme`: `phaseIconKind(phase, onGround)`,
  `phaseIconRows()` = char grids `# d k g .`, `drawPhaseIcon()` draws them as fastHLine runs):
  side view climbing ~18° with a runway strip under the tail = takeoff (green), side view
  descending with the runway under the nose = landing (peach), side view level = overflight
  (sky), **top view = on the ground** (muted lavender, any phase), nothing at all = `NONE`.
  Used in the table's FAZA column and at the right end of the identity line (Screens 1-3).
  History: ASCII `^ v > -` (too small) → filled ▲ ▼ ▶ triangles (nobody read them as
  takeoff/landing) → these planes ("variant B" of the 2026-10-18 mock-ups). The README shows
  them from `docs/icons/phase_*.png` — regenerate those if the grids change.

## Time & WiFi status (for the header bar)

Needed because every screen now shows a real clock, not just uptime — this is a new
requirement, the firmware didn't sync wall-clock time before.

- **NTP sync**: after WiFi connects (`net_task`, right after `wifiManagerBegin()`), call
  `configTzTime(TZ_WARSAW, "pool.ntp.org", "time.nist.gov")` where
  `TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3"` — the standard POSIX TZ rule for Europe/Warsaw
  (correctly handles CEST DST switches). This is a stable, long-standing EU DST rule, but
  double-check it hasn't changed if clocks ever look off by an hour.
- Until the first sync completes, `time_sync` must report "not synced yet" — the header bar
  should show placeholders (`--.--.----` for the date, `--:--` for the time) rather than the
  garbage epoch-zero date ESP32 starts with.
- `time_sync` module: `isTimeSynced()`, plus **pure, testable** formatting functions that take
  a `time_t` as a parameter rather than calling `time(nullptr)` internally —
  `formatDate(time_t) -> "DD.MM.YYYY"` (zero-padded, slash-separated, e.g. `07.03.2026`) and
  `formatTime(time_t) -> "HH:MM"` (24h, zero-padded). Keeping the epoch as an
  injected parameter (instead of reading the wall clock inside the function) is what makes this
  testable without depending on when the test happens to run.
- **WiFi signal**: `rssiToBars(int rssi_dbm) -> 0..4`, pure function, from `WiFi.RSSI()`.
  Rough default thresholds (adjust after seeing real readings): ≥ -55 dBm → 4, -55..-65 → 3,
  -65..-75 → 2, -75..-85 → 1, below that → 0. Kept as a utility function even though the
  current header layout (see "Screen chrome" below) doesn't have a slot for it — say if you
  want it added back somewhere and I'll find room.
- **No stardate.** An earlier revision showed a decorative `STARDATE: XXXX.XX` in the header;
  that's dropped — the header shows the real calendar date via `formatDate` instead. Remove
  `computeStardate()` and its tests rather than leaving it around unused.

## Flight ETA — closest point of approach (for Screen 2, "Flight")

Answers "za ile sekund/minut będzie nad tobą" — how long until the nearest aircraft is closest
to home. Pure math, lives in its own `cpa_predictor` module, no I/O:

- **Approach**: linear extrapolation. Treat the aircraft as moving in a straight line at its
  current speed and track (constant velocity) and compute the time at which its distance to
  home is minimized — the standard closest-point-of-approach (CPA) calculation. This is an
  approximation: real aircraft turn, so accuracy degrades the further out you extrapolate. It's
  acceptable here specifically because the product only cares about the near-term result (under
  a couple of minutes), not a long-range prediction.
- **Math**:
  1. Convert both home and aircraft lat/lon to local flat x/y in km around the home latitude
     (equirectangular approximation — fine at this scale): `dx_km = (lon_ac - lon_home) *
     111.32 * cos(lat_home_rad)`, `dy_km = (lat_ac - lat_home) * 111.32`.
  2. Velocity vector from speed (convert m/s → km/s) and track (degrees, 0 = north, clockwise):
     `vx = speed_kms * sin(track_rad)`, `vy = speed_kms * cos(track_rad)`.
  3. `t_cpa_seconds = -(dx_km*vx + dy_km*vy) / (vx*vx + vy*vy)`.
- **Edge case**: if `vx² + vy²` is ~0 (aircraft essentially stationary — on ground, hovering),
  return "no prediction" (`found = false`) instead of dividing by zero.
- **Result is signed**: positive = CPA is in the future, negative = CPA already happened that
  many seconds ago. This is exactly what Screen 2's countdown needs — see "Screen 2" below for
  how it's displayed and when it switches to a minutes format.
- **Local ticking between polls**: aircraft data only refreshes every `poll_interval` (default
  15s), but the countdown should feel like it's ticking every second. Keep a local countdown
  driven by `millis()` that decrements between polls, and re-sync it to the freshly-computed
  `t_cpa_seconds` every time a new poll comes in — don't just freeze the displayed number for
  15 seconds and then jump.



## Visualization concept — three screens: Flights, Flight, Radar

Three screens now, not two, cycled in this fixed order: **Flights → Flight → Radar →
(back to Flights)**. All three share the same header chrome and `lcars_theme` styling so
switching feels like changing a mode on one console, not jumping between apps.

### Screen chrome — header bar (identical on all three screens)

**Exact pixel spec, y: 0 to 15px, full width (320px):**
- **Layout "variant A" — three zones, nothing filled** (`statusBarZones()`, native-tested so the
  zones never overlap for the longest texts in either language):
  `SKY TRACKER          ● 7 LOTÓW          18.10.2026 09:11`
  - **Left:** app name `kStatusBarAppName` ("SKY TRACKER"), small font, dim lavender
    (`THEME_CHROME_DIM`).
  - **Device-alert "!"** (only when an alert is active): 13 px red / orange circle 4 px right of
    the name — see "Device alerts". Tapping it opens the message screen.
  - **Centre:** a **health dot** (`statusBarHealthColor()`: green = data OK, yellow =
    rate-limited / backing off, red = auth or network error, i.e. the list may be stale) and the
    **number of aircraft in range** in white, with the correct plural (PL `1 LOT` / `3 LOTY` /
    `7 LOTÓW`, EN `1 FLIGHT` / `7 FLIGHTS`; `99+` above 99; `--` until the first successful poll).
  - **Right:** date `DD.MM.YYYY` (light peach) and time `HH:MM` (white) right next to it, both in
    `LCARS_FONT_CLOCK` (DejaVu12, 13px line; the date's dots looked like "+" in the 10px efont).
    Placed with *measured* widths (the font is proportional), and the count is centred in the gap
    between the app name and the date. No seconds — the header is
    redrawn once per minute (see "Redraw policy"), so `HH:MM:SS` would sit frozen.
  - A **1px lilac rule** along the header's bottom edge separates it from the content.
- **Why:** the previous header had a filled peach pill (count, coloured by feed health) that
  looked exactly like the active bottom-nav button, the count sat glued to the date with a big
  gap before the time, and nobody could tell what the pill's colour meant. Peach fill is now
  reserved for the active nav button; health is an explicit dot. (Earlier still, the block showed
  the screen name, duplicating the active nav button.)
- The header is repainted (on its own, `kRedrawHeader`) on every new poll (count), health
  change (colour) and minute rollover (clock).
- **WiFi bars are dropped from this header** in this revision — the earlier three-zone design
  (date/time left, name center, WiFi right) is superseded by this spec. Say if you want signal
  strength back somewhere; there's no slot for it here right now.

Define `LCARS_HEADER_HEIGHT = 15` and `LCARS_BOTTOM_NAV_HEIGHT = 25` as shared constants in
`lcars_theme`. Every screen's content is bounded by both: `y = LCARS_HEADER_HEIGHT` (28,
leaving a 3px gap, `LCARS_CONTENT_TOP` = 18) to `y = 240 - LCARS_BOTTOM_NAV_HEIGHT` (`LCARS_CONTENT_BOTTOM` = 215) — **not** all the way to 240.
Getting this wrong (leaving no real space for the bottom nav bar) is exactly what happened in
the first pass of this spec — every screen's content area has since been corrected from a
`y:238` bottom bound to `y:225`, and later (slim header / big nav buttons) to 18..215, see each screen's section above. Drawn by a dedicated
`status_bar` module, called once per frame from `main.cpp` before dispatching to whichever
screen is active — screens themselves don't need to know `status_bar` exists, they just draw
their content within these fixed bounds.

### Redraw policy — no full-screen clears except on screen change

A full `fillScreen()` + redraw of everything is visible as a flash on the ILI9341 over SPI. The
first implementation did exactly that once per second on Screen 2 (to tick the countdown), and
the whole screen — frame, header, nav bar — blinked every second. Rules:

- **`fillScreen()` only on a screen change** (manual, auto-cycle, or table page change) and on
  the first draw after boot/setup. Never on a timer tick.
- **New poll data** → redraw only the active screen's content area (y:18..215), clearing it with
  one `fillRect` of that region. Header and bottom nav are left alone unless their own content
  changed (screen index, health tag).
- **Header clock** → `status_bar` redraws only its date/time text area (not the orange block),
  when the displayed minute or the health tag changes, on every screen — not only on poll.
- **Screen 2 countdown tick (1 Hz)** → redraw only the countdown zone (y:82..195) and the status
  strip (y:190..215). The identity panel, header and nav bar are not touched. Draw the countdown
  zone into an `LGFX_Sprite` and `pushSprite()` it in one go so the digits don't flicker either.
  A full-width 16-bit sprite of that zone is ~70 KB; if free heap is tight once TLS is up, use an
  8-bit sprite (`setColorDepth(8)`) or a sprite sized to just the digits' bounding box — check
  `ESP.getFreeHeap()` rather than guessing.
- Keep the "what needs redrawing" decision as a small pure function (e.g. a dirty-region
  bitmask from `{screenChanged, pageChanged, newPoll, minuteChanged, healthChanged,
  countdownTick}`) so it's unit-testable under `native`; `main.cpp` just acts on the mask.

### Screen 1 — "Flights" (default view, prioritize this first)

**Exact pixel spec** (below the 25px header), no scrolling — everything drawn at fixed
coordinates to fit the 320x240 frame, consistent with "Design language" below:

**Featured flight section, y: 18 to 95px (~77px tall).**
- **No frame.** The magenta swept-corner "elbow" border that used to surround this panel was
  removed on request — text and stat chips sit directly on black, left-aligned. Same for
  Screen 2's identity panel and Screen 3's summary panel. (`drawElbowFrame()` stays in
  `lcars_theme` as an unused primitive; don't reintroduce the frame without being asked.)
- Highlighted text (top of the frame): flight (callsign), airline, aircraft type, phase icon —
  what's currently Line 1 of the featured panel, unchanged in content. **The line must never
  overflow the frame**: the phase icon is drawn at a fixed position, right-aligned just inside
  the frame's right edge, so it can never be pushed off-screen (it's the "taking off or
  landing?" answer — the most important glyph on the line). Callsign is never truncated; the
  airline is truncated (with `...`) to fill whatever width remains between callsign + type and
  the phase icon; type is truncated only if it alone still doesn't fit. Same rule wherever
  `aircraft_summary`'s identity line is used (Screen 2's identity panel too) — implement it
  once in `aircraft_summary` as a pure, tested layout function taking the available width.
- Detail line below it (bottom of the frame): the rest — origin → destination with country
  codes, plus the altitude/speed/distance stat chips — what's currently Line 2 + stat chips,
  unchanged in content, rendered smaller (8px / LovyanGFX's equivalent of Adafruit "Font 2" —
  again, verify the exact font reference name for LovyanGFX rather than assuming the TFT_eSPI
  name carries over).
- **Stat chips** (`featured_panel::featuredPillTexts()`): airborne → altitude (m) / speed (km/h)
  / distance (km). **On the ground** (`on_ground`, e.g. taxiing at the nearby airport) → one
  double-width `NA ZIEMI` / `ON GROUND` chip + distance — altitude and speed are meaningless
  there, and `0m` / `1m/s` looked like a bug. A missing value shows `--`, never a fake `0`.
  The distance chip shows the compass point too ("SE 6km", `compassPoint()`: N/NE/E/SE/S/SW/W/NW,
  international letters as on a compass rose) next to a "where to look" arrow (bearing from home,
  top = north). The arrow has a **stem** (`ArrowShape`: stem + small head) — the first, head-only
  version was a solid ▼ indistinguishable from the landing icon.
- Empty state ("no aircraft in range") still applies here, same as before — just now within
  these exact bounds.

**Flights table section, y: 98 to 215px.**
- **No sub-header strip.** The thin cyan `LOTY`/`FLIGHTS` label bar that used to sit above the
  column header was removed on request; its space went to roomier table rows. General rule that
  came out of it and still applies: no text may be taller than the strip/row it sits in when
  drawn with a background color.
- Column header row at **y: 98 to 113px**: `LCARS_ORANGE` (`0xFC00`)
  background, black text, labels separated by `|` (not per-column shaded cells), in
  `fonts::Font0` so the longest label fits its column. **Each label is drawn at its column's x
  position** (same constants as the data rows below), with the `|` separator drawn at each
  column boundary — *not* as one pre-joined string starting at x=4, which drifts out of
  alignment with the data (the first implementation put `SKAD` at ~x:100 over a column starting
  at x:160). Labels per language: PL `LOT`, `LINIA`, `SKAD`, `DOKAD`, `TYP`, `FAZA` / EN
  `FLIGHT`, `AIRLINE`, `FROM`, `TO`, `TYPE`, `PHASE` (ASCII — built-in bitmap fonts don't carry
  Polish diacritics). Every label in every language must fit its column (native-tested).
- **Column layout — single source of truth**, shared by the column header and the data rows
  (define once in `table_view`, expose via a pure function so tests can check that columns
  don't overlap and sum to ≤ 320px):

  | Column  | x (px) | width (px) | Content                     |
  |---------|--------|------------|-----------------------------|
  | LOT     | 2      | 70         | callsign (8 chars x 8px efont) |
  | ODL.    | 72     | 46         | distance from home, "8km" (replaced the airline column — the callsign already names the airline; distance says where to look) |
  | SKĄD    | 118    | 34         | origin airport IATA code    |
  | DOKĄD   | 152    | 34         | destination airport IATA code |
  | TYP     | 186    | 102        | friendly aircraft type, <= 12 chars ("737 MAX 8") |
  | FAZA    | 288    | 30         | phase triangle (EN label "PHASE" 25px + 4px label inset) |

- **Every cell is truncated to its column width** before drawing — text must never spill into
  the next column (with a background color set, it overwrites its neighbour; without one, the
  two strings overlap). Truncate with a trailing `.` / `...` when there's room, hard-cut
  otherwise. Do this with a pure, tested helper (e.g.
  `fitToWidth(text, maxPx, charWidthPx)` — reuse/generalize radar_view's `truncateAirline()`
  rather than writing a second one), and/or clip each cell with `setClipRect` as a pixel-level
  backstop. Callsign and phase icon always fit their columns and are never truncated.
- **5 data rows**, starting at **y: 116px**, **17px step** (rows at y = 116, 133, 150, 167,
  184) — 1px above the 16px Font2 text; the last row ends at y:201 (a `static_assert` in
  `table_view.cpp` enforces it). If more than 5 aircraft are in range, page via touch (Milestone 9's
  mechanism) rather than trying to fit more — pagination swaps which 5 are shown, it is not
  scrolling.
- Columns per row: **flight (callsign), airline, from (IATA code), to (IATA code), aircraft
  type, phase icon**. Airport codes only — no country codes, no room for them in a 17px row.
- **City names in the detail views** (featured panel, Screen 2, Screen 3 summary): the route is
  shown as places, not codes — "Antalya (TR) -> Warszawa (PL)" (`formatRowRouteCities()`; drops
  the country codes, then shortens both names, if it doesn't fit). Screen 3's narrow panel puts
  origin and "-> destination" on two lines. The table keeps 3-letter IATA codes (no room).
- **IATA codes everywhere** (table, featured panel, Screens 2 and 3). Every row with a route
  gets its two airports looked up (`AircraftRow::origin_airport` / `dest_airport`); new hexdb.io
  airport lookups are capped per poll (`kDefaultAirportLookupBudget`, planned with
  `planLookups()`, nearest rows first), cache hits are free. If hexdb.io has no record for an
  airport, its ICAO code is shown as a fallback (`rowAirportCode()` / `formatRowRoute()`).
- Airline/type come from `aircraft_lookup`; from/to come from `route_lookup`; phase from
  `flight_phase`. Never block the row on a missing value. **Two different placeholders:**
  `...` = still loading (the per-poll lookup budget hasn't reached it yet —
  `AircraftRow::info_pending` / `route_pending` / `*_airport_pending`), `--` = looked up,
  nothing known. A wall of `--` while data was merely loading read like a failure.
- Column-header labels sit `kTableHeaderLabelPadPx` (4px) right of the `|` divider line, so they
  don't touch it; cell text keeps the 2px `kTableCellPadPx`.
- **Order (improvement #9, `sortRowsForDisplay()`):** aircraft in the air first, nearest first;
  then aircraft on the ground (taxiing / parked at the nearby airport), nearest first; then rows
  without a position. The featured panel (and Screen 3's summary) therefore shows the nearest
  aircraft you can actually see in the sky, never a jet parked at the gate; the table lists the
  rest in the same order, excluding the featured one. The per-poll lookup budget follows this
  order too.
- **"On the ground" is inferred, not just read** (`opensky_client::inferOnGround()`, folded into
  `Aircraft::on_ground` at parse time): OpenSky's flag, OR speed below `kTaxiSpeedMps` (15 m/s),
  OR neither altitude nor speed reported. Apron traffic near EPWA is often sent with
  `on_ground=false` and null altitude/speed — trusting the flag put a parked LOT jet in the
  featured slot as "0m 0km/h" with an overflight icon. Null altitude/speed are tracked
  (`has_altitude` / `has_velocity`) and shown as `--`, never as a fake `0`.
- **Ground rows are dimmed** (`tableRowColor(i, onGround)` -> muted lavender) and get the
  top-view "on the ground" plane in the FAZA column instead of a phase — otherwise "5km" listed after "23km" read
  like a sorting bug.
- **Page indicator:** "1/2" (`tablePageLabel()`) right-aligned in the TYP column header whenever
  there is more than one page; nothing on a single page.
- **Row font: `LCARS_FONT_TABLE` = efont 14 px (subset, `src/fonts/`)** — 1px smaller than the 15px lilac
  header strip (`kTableHeaderHeightPx`), 7px per character; the rest of the UI keeps 16px.
- This is the priority screen — get it solid before spending time on the other two.

### Screen 2 — "Flight" (single nearest aircraft + overhead countdown)

**Displayed as `NOW` (EN) / `TERAZ` (PL)** in the header block and bottom nav — renamed from
"FLIGHT" on request. In code it's still `kScreenFlight` / `flight_screen`; only the on-screen
name changed.

**Current behaviour (supersedes "nearest aircraft" below):**
- **Which aircraft:** `pickNowAircraft()` predicts every aircraft's CPA (now including
  `CpaPrediction::miss_distance_km`, how far from home the closest point is) and shows the
  airborne one whose CPA is within `kOverheadRadiusKm` (5 km) and soonest (not more than 10 s in
  the past) — the question is "when will one be overhead", not "which is nearest". Without such
  a candidate it shows the nearest airborne aircraft with no countdown.
- **Honest empty states:** aircraft around but none passing overhead -> `NIE PRZELECI NAD TOBĄ`
  / `NOT PASSING OVERHEAD`; only with no aircraft at all -> `BRAK LOTÓW W ZASIĘGU`. (The first
  version said "no flights in range" while a plane was landing 1 km away.)
- **Identity panel has 3 lines** (y:18..88): identity, city route, and a stats line — a "where
  to look" arrow (`directionArrow()`: bearing from home, screen top = north) + `nowStatsText()`
  ("3km  236m  266km/h" / "6km  NA ZIEMI"). Countdown zone y:92..186, status strip y:190..215.
- Speeds everywhere are km/h (OpenSky's m/s x 3.6).
- **Caption "NAD TOBĄ ZA" / "OVERHEAD IN"** (`CountdownDisplay::caption`, small muted text) sits
  above the digits while the CPA is still ahead (t > 0); none once it has passed or without a
  prediction. The digits are centred in the space below it.

Deliberately minimal — one aircraft, one number that matters: when will it actually be
overhead. **Exact pixel spec**, below the 25px header, no scrolling:

**Identity panel, y: 18 to 68px (50px tall, x: 10 to 310).**
- No frame (same as Screen 1's featured panel) — text only, on black.
- Line 1 (~y:40): flight (callsign), airline, aircraft type, phase icon — via
  `aircraft_summary`, same content as Screen 1's featured panel Line 1.
- Line 2 (~y:62, smaller font, 8px / LovyanGFX's Font-2 equivalent): origin → destination as
  IATA + country code, e.g. `WAW (PL) → FCO (IT)` — same as Screen 1's Line 2.

**Countdown zone, y: 72 to 185px (113px tall), horizontally centered.**
- The dominant visual element on this screen — large digits, roughly 70-90px tall glyphs
  (scale a built-in font or use a larger LovyanGFX font; verify against what's actually
  available rather than assuming a specific font name).
- **Color-coded by urgency** (decorative LCARS "alert level" touch, adjustable):
  - `10 < t_cpa_seconds ≤ 60`: `LCARS_CYAN` — approaching, not urgent yet.
  - `0 ≤ t_cpa_seconds ≤ 10`: `LCARS_YELLOW` — imminent.
  - `-10 ≤ t_cpa_seconds < 0`: `LCARS_ORANGE` — just passed, counting away.
- **Seconds mode** (`-10..60`): big number + small `s` suffix, e.g. `42s` counting down through
  `0` to `-10`.
- **Minutes mode** (`t_cpa_seconds > 60`): smaller than the seconds giant-digit size (it's an
  estimate, shouldn't look as precise) — `~4 MIN`, color `LCARS_MAGENTA`.
- **Empty/no-prediction state** (no aircraft in range, or CPA not found): centered em-dash `—`,
  no color-coding.

**Status strip, y: 190 to 215px (25px tall — see Screen 1's note on the
bottom nav bar), centered text.**
- Label matching the countdown state: `ZBLIŻA SIĘ` (approaching, cyan zone) / `NAD TOBĄ`
  (imminent, yellow zone) / `MINĄŁ` (passed, orange zone) / `SZACOWANY CZAS` (minutes mode) /
  `BRAK LOTÓW W ZASIĘGU` (empty state).
- Optional nice-to-have, not required: since `flight_phase` is already computed elsewhere for
  this aircraft, its takeoff/landing/overflight chevron can sit alongside the label here too —
  skip it if it clutters this narrow strip, the countdown is the point of this screen.

**Behavior notes (unchanged from before, still apply):**
- Same nearest-aircraft selection as Screen 1's featured panel — reuse it, don't reselect
  independently.
- Local countdown ticks every second via `millis()` between polls, re-synced to the
  freshly-computed `t_cpa_seconds` on each new poll (see "local ticking" in the CPA section).
  Each tick redraws **only** the countdown zone + status strip, via a sprite — never the whole
  screen (see "Redraw policy" under "Screen chrome").
- When `t_cpa_seconds` drops below -10, don't hold onto a stale prediction — next poll
  recomputes "nearest aircraft" and its CPA as normal, so the screen naturally moves on to
  whatever is nearest now.

### Screen 3 — "Radar" (left: radar, right: nearest-aircraft summary)

238, corrected: see the bottom-nav-bar fix noted in Screen 1):

**No divider.** The orange vertical bar (x:192..200) between radar and summary was removed on
request — the summary text simply starts at x:208. (`drawVerticalDivider()` stays in
`lcars_theme` unused.)

**Radar, x: 0 to 190px, y: 18 to 215px.**
- Center at `(95, 117)`, radius `85px` (centred in the y:18..215 content area —
  shorter content area; radius unchanged, still fits with margin: top `127-85=42`, bottom
  `117+85=202`, both within `18..215`).
- 3 concentric rings at radius `28px` / `57px` / `85px` (thirds of max radius), each labeled
  in small text near the top of the ring with its corresponding distance in km (derived from
  `max_distance_km / 3`, `2/3`, full — see `computeRingDistances` from the radar geometry work).
- Home marker: small crosshair or dot, `LCARS_YELLOW` (`0xFFE0`), at center.
- Aircraft blips: small dots (~5px), `LCARS_CYAN` (`0x07FF`) normally, dimmed/smaller if
  `clamped == true` (beyond `max_distance_km`, per `polarToScreen`'s clamping — see the earlier
  radar geometry step).
- Static plot, no sweep animation, consistent with the earlier radar_view spec.
- **Readability fixes (review #5-#8):** ring labels in the small font on each ring at 45 degrees
  up-right (`ringLabelAnchor()`), not at 12 o'clock where blips covered them; an "N" above the
  outer ring (north-up plot); airborne blips get a short **heading tick** along their track
  (`headingTickEnd()`); **ground traffic** is a small muted square (no tick) instead of a blip, so
  the apron next to home no longer becomes one blob; the aircraft shown in the summary panel
  (`rows.front()`) has a **white ring**. The summary panel gained a 5th row,
  `radarSummaryStats()`: "SE 5km  61m" / "S 4km  NA ZIEMI".

**Summary panel, x: 200 to 320px (120px wide), y: 18 to 215px.**
- No frame and no divider (same as Screens 1/2) — just text, starting at x:208.
- Three rows, via `aircraft_summary`, vertically spaced within the shorter panel (row positions
  shifted up slightly from the original 95/130/165 to re-center in the 18-215 zone):
  - Row 1 (~y:82): flight (callsign), bold/emphasized.
  - Row 2 (~y:117): airline — truncate or abbreviate if it doesn't fit the 120px width, don't
    let it overflow into the radar zone.
  - Row 3 (~y:152): origin → destination as IATA codes, e.g. `WAW → FCO` (no country codes
    here — this panel is narrower than Screen 1's featured panel, keep it to what fits).
- **Empty state**: if no aircraft in range, radar still draws its (empty) rings and home
  marker; this panel shows a centered `BRAK LOTU` / em-dash instead of the three rows.

### Screen navigation

- **Order**: Flights (0) → Flight (1) → Radar (2) → wraps back to Flights. Managed by a new
  `screen_nav` module (current index + `nextScreen()`/`prevScreen()` with wraparound — small,
  but worth testing, off-by-one bugs in wraparound logic are easy to introduce).
- **Screens are switched only by the bottom nav buttons.** Left/right edge strips that also
  switched screens (prev/next) were removed on request — taps anywhere above the nav band are
  left to the active screen (Screen 1's table paging taps on its first/last row). Don't
  reintroduce edge swiping without being asked.
- **Bottom nav bar, y: 215 to 240px (`LCARS_BOTTOM_NAV_HEIGHT = 25px`, full 320px width)** — a
  persistent strip, present on all three screens, drawn as **three folder tabs** (the screen
  names — `LOTY`/`TERAZ`/`RADAR` or `FLIGHTS`/`NOW`/`RADAR` — in efontJA_16, one per ~107px
  segment) hanging from a 2px peach rule along the bar's top edge: square top corners, rounded
  bottom ones. The **active tab** is peach, full height, merges into the rule (the "pulled-out
  sheet") and overlaps its neighbours by 2px, black label; **inactive tabs** are recessed — 2px
  below the rule, dark fill (`THEME_CHROME_RECESSED`), lavender outline + label. Geometry is the
  pure `navTabRule()` / `navTabRect()` in `screen_nav` (native-tested); touch zones are still the
  plain segments. (Before: three separate pill buttons, which read as buttons, not tabs.) Not tiny dots — the first
  implementation drew 6px-tall pills and they were effectively invisible on the real board.
  Tapping a segment jumps directly to that
  screen. **This is the fix for the bottom bar being invisible/unreachable** — earlier versions
  of this spec never actually reserved real height for it, which is why it rendered as a sliver.
  All three screens' content areas now stop at `y:215` specifically to make room for this.
  The bar is 25px tall with Font2 labels (it was 15px with tiny labels and nobody could see or
  hit it); `navHitTest()` accepts taps from y:210, 5px above where it draws.
- **Touch calibration** (`include/touch_calibration.h`, handed to LovyanGFX's
  `Touch_XPT2046` config): in LovyanGFX raw terms, **raw X is the vertical axis** (~340 at the
  bottom edge .. ~3900 at the top) and **raw Y the horizontal one, running right-to-left**
  (~3900 at the LEFT edge .. ~200 at the right — passed to LovyanGFX as `y_min > y_max`);
  with `offset_rotation = 0` and `setRotation(1)` LovyanGFX swaps the axes and flips the vertical.
  `touch_input::mapRawTouch()` is a pure copy of that conversion, so native tests check the
  constants against raw taps logged on the real board — both the old library's samples (its raw
  x/y relate to LovyanGFX's as X_L = y_old, Y_L = 4095 - x_old) and LovyanGFX-native ones. The
  first LovyanGFX port forgot that inversion and mirrored the buttons (LOTY opened RADAR); the
  native-sample test pins the direction now. Before any of this was
  measured, every bottom-button tap landed in the header and the buttons never responded. Each
  tap logs `[touch] raw=(x,y) -> screen=(x,y) nav=<action>` to Serial — use that to re-tune if
  taps near an edge feel off (corners were not measured yet). LovyanGFX's `calibrateTouch()`
  is available if a proper 4-corner calibration is ever wanted.
- **Persistence**: current screen index saved to `config_store` as `last_screen` (0/1/2),
  restored on boot. This replaces the old boolean-ish `last_view` field from when there were
  only two screens (table/radar) — rename it, don't keep both fields around.
- **Any screen change resets the auto-cycle timer** (see "Auto screen cycling" below) — whether
  the change came from a manual touch or an automatic switch. Without this, a manual tap could
  be immediately undone by an auto-switch a moment later, which would feel broken.

### Auto screen cycling (new, off by default)

Two new `config_store` fields, both configurable via `config_portal`:
- `auto_cycle_enabled` (bool, **default `false`**).
- `auto_cycle_interval_s` (int, **default `15`**).

**Basic behavior when enabled**: a `millis()`-based timer counts up from the last screen change
(manual or automatic). Once it reaches `auto_cycle_interval_s`, advance to the next screen via
the same `nextScreen()` used for manual navigation (Flights → Flight → Radar → wraps to
Flights), then reset the timer.

**Exception — don't interrupt an imminent overhead moment on Screen 2 ("Flight")**: if the
auto-cycle timer fires while the active screen is `FLIGHT` and the currently-displayed
countdown is "close" (see thresholds below), **defer** the switch — don't advance yet, keep
re-checking every loop iteration, and switch as soon as the condition clears. The interval
timer does not reset while deferred; the switch happens the moment the hold condition clears,
however long that took.

- **Hold condition** (pure, testable — put this in `screen_nav` as e.g.
  `shouldDeferAutoSwitch(currentScreen, cpaFound, tCpaSeconds) -> bool`, taking `t_cpa_seconds`
  as a parameter rather than reaching into `cpa_predictor` itself, same decoupling discipline
  as everywhere else in this project):
  ```
  shouldDeferAutoSwitch = (currentScreen == FLIGHT)
                        && cpaFound
                        && (tCpaSeconds < 6)
                        && (tCpaSeconds > -5)
  ```
- In plain terms: less than 6 seconds until the aircraft is overhead → hold. Keep holding
  through the overhead moment and for 5 seconds after (`t_cpa_seconds` reaching `-5` or below
  clears the hold). If `cpaFound` is false, or the screen isn't `FLIGHT`, there's nothing to
  hold for — switch normally.
- These two constants (`6` and `-5`) are fixed, not exposed in `config_portal` — only
  `auto_cycle_enabled` and `auto_cycle_interval_s` are user-configurable per this request.
- Edge case worth noting, not necessarily guarding against: if a *new* nearest aircraft becomes
  imminent right as the previous hold clears, the hold condition will naturally re-trigger for
  it on the next check — that's expected behavior, not a bug to fix.


## Language — Polish / English

Every on-screen string is shown in Polish or English, chosen on the config page ("Display
language / Jezyk wyswietlacza" select, values `pl` / `en`). Default: Polish.

- **One table, `i18n` module.** All displayed text lives in the `I18N_STRINGS` X-macro list in
  `src/i18n.h` — one line per string with its PL and EN text side by side. The `Str` enum and
  both translation tables are generated from that list, so a string can't be added without both
  translations (it won't compile) and the tables can't drift out of step with the enum. This is
  the usual small-firmware approach on Arduino/ESP32: no gettext, no files on SPIFFS/LittleFS,
  no runtime parsing.
- **Flash, not RAM.** The tables are `static const char *const` arrays of literals — on the
  ESP32 they stay in flash (.rodata). Don't add `PROGMEM` / `F()`; they're AVR-era and no-ops
  here.
- **Never hardcode display text** in a view module — add a `Str` entry and use `tr(Str::X)`.
  Draw code uses the current language (`tr(id)`, set by `i18nSetLanguage()`); pure functions
  that return text take an explicit `Language` (e.g. `computeCountdownDisplay(cpa, lang)`,
  `statusBarScreenName(i, lang)`) so tests can check both languages.
- **Real Polish letters** (`ZBLIŻA SIĘ`, `SKĄD`, `Kraków`): body/table/small text uses
  **efont** 16 / 14 / 10 px (U8g2 format) — but not LovyanGFX's `fonts::efontJA_*` (10,800
  glyphs each, ~1.05 MB of flash together, mostly kanji). `tools/subset_efont.py` copies only
  the needed glyphs, byte for byte with the original metrics, into the generated
  `src/fonts/lcars_efont.c` (~4.4 KB; keeps the efont BSD notice) — same pixels, same layout.
  The set is printable ASCII + ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ + `° ± × · – — … →`, defined once in
  `lcars_theme` (`isFontCodepoint()` / `isFontSafeText()`); the script's `CODEPOINTS` must
  match it, and `fonts_test.cpp` checks every allowed character has a glyph in all three fonts.
  A character outside the set simply isn't drawn, so every on-screen string must be
  `isFontSafeText()` — `i18n_test.cpp`, the city table and the type/airline tables are checked
  against it. To add a character: extend both lists, rerun the script, run the tests. This is
  what lets the ~1.3 MB firmware fit two OTA slots (`min_spiffs.csv`). `fitToWidth()` cuts only on UTF-8
  character boundaries; layout maths uses `utf8Length()` and the fonts' fixed advance
  (`LCARS_FONT_BODY_CHAR_PX` = 8, `LCARS_FONT_SMALL_CHAR_PX` = 5). hexdb.io airport names are
  still ASCII-folded (`asciiFold()`), as they can contain any accent. Headings (Font4) and the
  countdown digits (Font8) are unchanged — they never show Polish text.
- **Length limits** that tests enforce: screen names ≤ 7 chars (header block), table column
  labels must fit their column in Font0.
- **Persistence & apply.** `config_store` keeps it as `language` (NVS key `lang`, numeric index;
  out-of-range → default). After a config-page save, `main.cpp` reloads the config immediately
  (`configPortalSaveCount()`), calls `i18nSetLanguage()` and does a full redraw
  (`RedrawEvents::languageChanged`).
- Not translated: units (`m`, `km`, `m/s`, `MIN`), `--` placeholders, airline/aircraft names
  from hexdb.io.
- **The config web page has its own language** (`Config::web_language`, NVS key `wlang`,
  default PL), independent of the display's: PL/EN switch in the page header (`GET /lang?l=`)
  and a select on the General tab. Its strings are a separate X-macro table, `web_i18n`
  (`WEB_I18N_STRINGS`, `webTr()`); they go into HTML/JS raw, so `web_i18n_test.cpp` forbids
  `" < > \ &` in them.

## Firmware updates (OTA) & panel password

Spec: `spec/done/01-ota-updates.md` (roadmap #1). Released in v1.1.0, tested on the board 2026-10-24.

- **Upload:** `POST /update[?size=N]` (multipart field `firmware`) on the config page —
  Firmware tab (XHR with a progress bar; a plain form post works too) or
  `pio run -e cyd-ota -t upload` (`curl --digest`, password from `$CYD_WEB_PASSWORD`). The file
  is the CI's `cyd-sky-tracker-<version>.bin` (app only). `OtaSession` checks the header before
  anything reaches `Update`, and only calls `Update.end()` (which switches the boot slot) when
  the file carried the **firmware marker** `CYDSKY-FW1|board=<FW_BOARD>|v=<FW_VERSION>`
  (`version.cpp`, `firmwareMarker()`; `FW_BOARD` defaults to `cyd-r` — the "builds for other CYD variants" roadmap item will set it
  per env and reject a mismatch). `firmware.yml` greps the built `.bin` for it. Builds older
  than this feature have no marker, so going back to them needs USB / the web installer.
- **Display:** `config_portal` and `release_check` draw nothing; their `OtaObserver` callbacks
  (start / every 16 KB / end, on core 0) go to `net_task`, which publishes them as
  `NetState::ota` (`OtaProgress`, phase + seq); the UI on core 1 draws `service_screen` from it
  (see "Concurrency — two cores"). The 16 KB callback also does `vTaskDelay(1)` (core 0's task
  watchdog). Success: answer page, ~1.5 s, `ESP.restart()` (`configPortalRestartPending()` keeps
  the network task off the network meanwhile). Failure: "update failed" for 5 s, then a full
  redraw.
- **Rollback:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in this arduino-esp32 (2.0.17).
  `main.cpp` overrides `verifyRollbackLater()` -> true; `net_task` calls
  `esp_ota_mark_app_valid_cancel_rollback()` once `shouldMarkAppValid()` (pending verify, ≥ 30 s
  up, settings page reachable or WiFi setup hotspot up, **and the UI loop alive** — its heartbeat
  `core_link::uiBeatMs` < 2 s old). A new image whose UI loop stops while still on probation is
  restarted (`shouldRestartForRollback()`), so the bootloader goes back to the previous slot
  instead of the board sitting on a frozen screen. OpenSky trouble never rolls back.
  Verified with an image that `abort()`s after 8 s, and (spec 04) with one whose UI loop hangs
  after 8 s: both times the board came back on the previous slot.
- **Password:** `Config::web_password` (NVS `wpass`), optional (default none). When set, *every*
  route calls `authorize()` first: HTTP Digest (`WebServer::authenticate`, realm
  `CYD Sky Tracker`, user `admin` — Basic is accepted too), `AuthThrottle` lockout -> 429 +
  `Retry-After`. Upload callbacks check credentials *before* buffering/writing anything.
  Forgotten password: finger on the screen at power-up, 5 s, YES (`offerWebPasswordReset()`).
- ⚠️ **arduino-esp32 2.x `WebServer` pitfall:** a route registered with an upload callback gets
  that callback for **any** POST body — a non-multipart one (e.g. the empty probe `curl
  --digest` sends first) arrives as "raw", and `server.upload()` then dereferences null and
  crashes the board. Every upload callback starts with `if (!isMultipartRequest()) return;`
  (needs `collectHeaders({"Content-Type"})`), and handlers take the buffered file with
  `takeUpload()` so a later request can't reuse a stale one.

## New versions from GitHub (release_check)

Spec: `spec/done/02-github-release-check.md` (built and tested on the board 2026-10-24).

- **Check:** `GET api.github.com/repos/<FW_UPDATE_REPO>/releases/latest` 2 min after boot, then
  daily (`If-None-Match` ETag -> 304 doesn't count against GitHub's 60/h). Only between OpenSky
  polls and with heap to spare (`canStartNetworkTask()`: WiFi, clock synced, >= 5 s to the next
  poll, >= 50 KB free block). Setting `update_check_enabled` (NVS `updchk`, default on) — it only
  *checks*; installing always needs a YES. A failed connect is retried once.
- **TLS is verified here** (unlike OpenSky / hexdb.io, still `setInsecure()`): `src/github_ca.h`
  holds the *root* CAs (Sectigo E46/R46 + USERTrust ECC/RSA for github.com / api.github.com,
  ISRG X1/X2 for release-assets.githubusercontent.com). `firmware.yml` warns when a host stops
  verifying against them — then add the new root and release. Every URL and every redirect hop
  must pass `isAllowedUpdateUrl()` (https, GitHub hosts, our repo's paths) before connecting;
  redirects are followed by hand. The repo is a compile-time constant, never a setting.
- **Install:** `browser_download_url` of `releaseAssetName(tag, board)` (`cyd-sky-tracker-<tag>.bin`
  for `cyd-r`) -> 302 -> release-assets.githubusercontent.com, streamed into the shared
  `otaFlashSession()` with `OtaExpectations{sha256 (asset "digest", else SHA256SUMS.txt), version
  (= tag, checked against the marker)}`; `Update.end()` only when all match. No hash = no install.
  Runs in `net_task`'s loop (not inside the HTTP handler), ~20 s for 1.36 MB, OTA screen titled
  "POBIERANIE Z GITHUBA".
- **Display prompt:** `shouldShowUpdatePrompt()` — only `Available` (a release newer than a plain
  release build), once per boot, not for a development build, not over the NOW countdown, not
  under a finger. YES installs; NO stores `update_skipped_tag` (NVS `updskip`, not in backups);
  30 s = later (next boot). Screenshot of it: `/screenshot.bmp?screen=3` (demo data,
  `updateDemoState()`).
- **Config page** (Firmware tab, first card): installed / latest / status / checked time,
  *Install*, *Check now* (`POST /update/check`, max once a minute), *Show development versions*
  (`POST /update/prerelease`, NVS `updpre`, default off) which adds the newest GitHub
  pre-release above the latest release with *Install* (`POST /update/install tag=…` accepts only
  the shown tags). Development versions are never offered on the display.
- **Development versions = tags with a suffix** (`v1.3.0-rc.1`, `-beta.N`, `-dev.N`):
  `release.yml` marks them pre-release and skips the project page; `build_site.py` gives them no
  changelog entry. Release one: `git tag v1.3.0-rc.1 && git push origin v1.3.0-rc.1`.
  Every PR gets one automatically (`pr.yml`, see "Versioning & CI"). The board parses only
  `vX.Y.Z-dev.N` with **one** number (uint16), so PR builds use the workflow run number as N and
  put the PR number in the release *title*.
- Screenshots `update_prompt.png`, `web_firmware*.png`: `python tools/screenshots.py --device
  <board>` (web pages from `config_page_test.cpp` in `test_admin` with `SCREENSHOT_OUT`, headless Chrome; the
  display one from the board — LovyanGFX can't build for the PC without SDL).
- ⚠️ Windows: a native test run sometimes reports `ERRORED` with no failed assertion — that's
  application control blocking a fresh test `.exe` (WinError 4551). Rerun that suite
  (`pio test -e native -f test_<suite>`).

## Concurrency — two cores (core_link, net_task)

Spec: `spec/done/04-dual-core.md`. Before it, one `loop()` did everything and every HTTPS request froze
the screen (measured: ~0.8 s per OpenSky poll, up to 5.7 s with the settings page or the GitHub
check). Now:

- **Core 0 — `net_task`** (FreeRTOS task, 16 KB stack, priority 1, next to WiFi and lwIP which
  already run there): `wifi_manager`, `time_sync` (NTP), `config_portal` (WebServer + mDNS),
  `opensky_client`, hexdb.io lookups with their caches (`aircraft_lookup`, `route_lookup`,
  `lookup_store`, `hexdb_http`), `poll_pipeline`, `release_check`, both OTA paths, marking the
  image valid. It owns the `Config` in memory (reloads it after a config-page save,
  `configPortalSaveCount()`; no more NVS read every poll). Each pass ends with
  `vTaskDelay(kNetIdleSleepMs = 5)`.
- **Core 1 — `loop()` in `main.cpp`** (the Arduino loopTask, default 8 KB stack — the 16 KB
  `SET_LOOP_TASK_STACK_SIZE` was only for TLS and is gone): `tft`, touch, `screen_nav`, redraw,
  `backlight`, the update question, service screens. It **sleeps** between passes
  (`ulTaskNotifyTake`, `uiSleepMs()`: touch poll every 20 ms — one GPIO read without a finger,
  LovyanGFX checks the XPT2046 IRQ pin first — the countdown second, the clock minute, the
  prompt timer); `net_task` wakes it when it publishes.
- **Share nothing, three channels** (`core_link`):
  1. `NetState` net -> UI: an immutable snapshot (`shared_ptr<const NetState>`) in a
     `LatestBox` — latest wins, the lock only swaps a pointer, the old value is freed *after*
     unlocking (tested). Rows live in a `DataSnapshot` shared between consecutive NetStates, so
     a health / update / OTA change never copies them. `net_task` publishes only when
     `netStateDiffers()`. The UI turns a new one into redraw events with `diffNetState()`.
  2. `NetCommand` UI -> net: a FreeRTOS queue of 8 POD commands (`InstallTag` = YES on the
     update question, `SkipTag` = NO); never blocks.
  3. Atomics / `Guarded<T>` (a POD copied under a spinlock): `uiBeatMs`, `netBeatMs`,
     `pollingAllowed` (spec 03's night pause, decided by the UI), UI perf numbers, and the
     backlight status that `/light.json` reads (`backlight.cpp`).
- **Rules:** code on core 0 never calls LovyanGFX; code on core 1 never does network I/O. Never
  allocate, free, log or do I/O while holding a `SpinLock` (it is a FreeRTOS critical section).
  `/screenshot.bmp` is the one place the web handler needs the panel: it asks the UI for bands of
  16 rows into two buffers (`ScreenshotSource::begin/requestBand/waitBand/end`, `shot::` in
  `main.cpp`) — the UI reads band k+1 while band k goes out; a timed-out request is cancelled
  under the lock so the UI never writes into a freed buffer. The UI doesn't draw while a capture
  runs.
- **Poll -> two snapshots** (`poll_pipeline`, pure, `buildDataSnapshot()`): phase A right after
  `/states/all` with no lookup budget (cache only, the rest `...`), phase B with the budgets; B
  is published only if it fetched something (`DataSnapshot::fetches`). Results are memoised per
  poll so B doesn't repeat A's lookups.
- **hexdb.io keep-alive** (`hexdb_http`): one TLS session per phase B (`hexdbSessionEnd()`
  after it); a request on a connection the server closed is retried once, fresh. HTTPClient's
  destructor `stop()`s the socket — the HTTPClient has to live as long as the session.
- **Liveness:** no `netBeatMs` for 60 s -> the header shows a network error (stale data isn't
  presented as fresh); no `uiBeatMs` for 2 s -> a new image isn't marked valid (and is restarted,
  see "Firmware updates").
- ⚠️ **Core 0's task watchdog panics** (`CONFIG_ESP_TASK_WDT_PANIC=1`, 5 s, idle task of CPU0
  checked): nothing in `net_task` may keep the CPU for seconds without yielding. Blocking socket
  waits are fine (they yield); a tight loop is not — and neither is a flood of tiny TCP writes:
  each one wakes lwIP (`tiT`) and the WiFi task, all on core 0. v1.4.0 streamed the settings page
  as hundreds of tiny `sendContent()` calls (3 TCP writes each): ~8 s of a fully busy core 0
  per page, and the watchdog restarted the board whenever the page was opened. `sendPage()` now
  collects ~1.4 KB per chunk (0.3 s per page). Send in large pieces.
- **Heap:** ~143 KB free, largest block 57 KB (with ~8 aircraft). The NOW screen's countdown
  sprite (~30 KB) is created in `setup()` (`flightScreenReserveSprite()`) before WiFi and
  `net_task` allocate — created on the first visit to NOW it split the largest block to 41 KB,
  below the 50 KB `release_check` needs for verified TLS (checks silently stopped until reboot).
  Allocate long-lived buffers at boot, not on first use.
- ⚠️ **Flash writes stall both cores** (cache off during erase/write): OTA, NVS (`lookup_store`,
  `saveLastScreen`) — a few ms to tens of ms on the UI, visible in `[perf]`.
- **Perf** (`perf_stats`, pure): the UI measures how much later than planned each pass starts
  (its drawing time + oversleep); `net_task` logs once a minute
  `[perf] ui gap max=85ms p99<20ms slow=0 | net busy 20.3% max=807ms | stack ui=… net=… | heap …
  | cache ac=40 rt=38 ap=12` and serves the same as `GET /perf.json` (`"cache": {ac, rt, ap}` =
  entries in the hexdb.io RAM caches; they must level off at the `lookup_cache` capacities, not
  grow with uptime).

## Brightness & night mode (backlight, sun_times)

Spec: `spec/done/03-brightness-night-mode.md` (built and tested on the board 2026-10-24).

- **Backlight:** `tft.setBrightness()` (LovyanGFX `Light_PWM`, GPIO21). LovyanGFX starts at
  127. Before this feature the board ran at half brightness all the time; `backlightBegin()`
  sets 255 right after `tft.init()`.
- **Light sensor:** LDR on GPIO34 (ADC1), read at **11 dB**. Measured on the board: a lit room
  reads ~0 (with single spikes up to ~150), darkness ~1100-1300, higher = darker. 0 dB saturates
  at 4095 in the dark. In the dark the backlight shines into the sensor (under a cloth: PWM 255
  -> ~260, 127 -> ~450, 10 -> ~1100, 0 -> ~1260). That feedback is not compensated because it
  pushes the right way (darker -> dimmer -> reads darker), so the screen settles at its minimum.
  Curve constants in `backlight.h` (`kLightRawBright` 40, `kLightRawDark` 1000): fixed per
  board, not user settings (spec §8.5).
- **Pure logic** (`backlight`): `LightFilter` (median of 5 + EMA, 2 s warm-up on full
  brightness), `autoBrightnessPct()` (linear between the two constants, 5 % hysteresis),
  `pctToPwm()` (min duty 8 + quadratic), `stepPwm()` ramps (1 s by day, 3 s into / out of the
  night, a wake-up jumps), `nightWindowFor()` / `isNight()` (Off / by the clock, through midnight
  / sunset->sunrise, polar day = never, polar night = always), `decideBacklight()`,
  `onTouchWake()`, `shouldPollOpenSky()`, `lightStatusJson()`. `sun_times` = the sunrise
  equation (UTC minutes; `shiftSunTimes()` to local), tested against api.sunrise-sunset.org
  (±3 min).
- **Rules:** a service screen (OTA progress / failed, update question, GitHub install) gets
  100 % at once (`backlightFullNow()` from the OTA observers). There is no night without a
  synced clock, and the night never brightens the screen (min of day and night level). At
  night with `night_pct` 0 the backlight is off: the first tap only lights it (`max(min, 10 %)`)
  for `night_wake_s` and is **not** passed to `handleTap()`. A tap at night always restarts
  the wake time. `night_pause_polling` (default **off**) skips `openSkyClientPoll()` (in `net_task`, via the
  `core_link::pollingAllowed` flag the UI sets) during
  the night window except within the wake time. Auto-cycle stops and the update question
  isn't shown while the screen is dark (`PromptGate::screenOff`).
- **Settings** (General tab, "Display" card; backup group `display`): `brightness_auto`
  (default on), min / max (10 / 100 %) or fixed (80 %), `night_mode` (default **sunset to
  sunrise**), night from / to (23:00 / 06:30), `night_pct` (default **10 %**, 0 = off),
  `night_wake_s` (60 s, 10-600), `night_pause_polling` (off). The card's "Now" line comes from
  `GET /light.json` every 2 s. The OpenSky tab's credits/day estimate subtracts the night window
  when pausing is on (`openSkyDailyCredits(radius, poll, pausedMinutesPerDay())`).
- Log: `[light] raw=… avg=… -> N% (pwm P)`, `[light] night (sun): 10%`, `[light] sun today:
  sunrise 07:17, sunset 17:25`, `[light] touch at night: awake for 60 s (screen lit)`,
  `[light] polling paused (night)` / `resumed (touch)`.

## Device alerts — the "!" (device_alerts)

Spec: `spec/done/08-device-alerts.md`. One way for the device to say "this needs your
attention", separate from the health dot (the dot = the data feed right now; the "!" = something
to fix or to know, even while data flows — anonymous after a failed login: green dot, red "!").

- **Derived, never remembered** (`collectAlerts(AlertInputs)`, pure): an alert disappears by
  itself when its cause does; nothing to acknowledge, closing the message doesn't clear it.
  v1: `OpenSkyLoginFailed` (error, `OpenSkyAuthState::Mode::LoginFailed`),
  `OpenSkyQuotaExhausted` (error, last answer a 429), `OpenSkyBudgetOverLimit` (warning,
  `openSkyDailyCredits()` > `openSkyDailyLimit(auth)` — 400 anonymous / in LoginFailed, 4000
  with an account; shown right after saving such settings, before the credits run out).
- **A new alert** = a row in `kAlertDefs[]` (severity, `onDisplay`, settings tab, `Str` title,
  `WebStr` title + action, log code) + a rule in `collectAlerts()` + texts in `i18n.h` /
  `web_i18n.h` (PL + EN; numbers via `fillTemplate()` "{}" placeholders) + the body/why case in
  `alertDisplayBody()` / `alertWebWhy()`. Drawing code doesn't change.
- **Flow:** `net_task` derives them every pass (`deviceAlertInputs(cfg)` = `openSkyAuthStatus()`,
  `openSkyQuota()`, the night pause) into `NetState::alerts` / `alertDetails`; logs each change
  once (`[alert] + opensky_budget_over_limit`). `netStateDiffers()` ignores `quotaObservedMs`;
  `diffNetState()` sets `RedrawEvents::alertsChanged` only when the badge
  (`displaySeverity()`) changes -> `kRedrawHeader`.
- **Header badge:** 13 px circle (`alertBadgeColor()`: red `THEME_ALERT`, orange
  `THEME_WARNING` = `LCARS_ORANGE`) with a black "!", y:1..13, 4 px right of `SKY TRACKER`
  (`statusBarZones(..., badge)` -> `alert_x`; the count re-centres between badge and date).
- **Tap -> message screen** (`service_screen`): `alertBadgeHit()` (screen_nav) = x 0 .. badge +
  8 px, y 0..24, checked first in `handleTap()`. Full screen: "UWAGA", up to 3 entries
  (errors first; more -> "+ N więcej — zobacz panel WWW"), "Szczegóły: http://cyd-sky.local",
  **ZAMKNIJ 15 s** button (36 px tall). Pure state machine `alertScreenStep()`: closes on a
  fresh tap on the button (after the opening finger lifted, `acceptPromptTap()`), after exactly
  15 s, or at once when no display alert is left; the 1 Hz countdown repaints only the button.
  While open: no auto-cycle (restarts on close), no update question
  (`PromptGate::alertScreenOpen`); OTA closes it. At night with the screen off the first tap
  only wakes it (backlight rule). `/screenshot.bmp?screen=4` = demo with all three alerts.
- **Web panel:** `.card.alert` above the tab bar (on every tab) when `alerts.any()`: what the
  "!" means, then per alert title, `alertWebWhy()` (HTTP code, renewal time, minimum interval
  `minPollIntervalForLimit()`), action and "→ Przejdź do: <tab>". `sendPage()` computes them
  with the same `deviceAlertInputs()`. CSS `--alert:#ff3333; --warn:#ff8000`, the "!" circle is
  `.bang`. (The old red `.alert` box for "no panel password" is now `.nopw`.)
- **Login state** (`opensky_auth`, first slice of spec 06): `onCredentials()` (other credentials
  -> `Account`, a fair try), `onTokenResult()` (400/401/403 -> `LoginFailed` + code; no answer /
  5xx / 200 without a token = unchanged). `opensky_client` logs the change once (`login failed
  (http=401) — check client_id/secret; using anonymous access` / `login ok — using account`).
  Still spec 06: 30-min wait before retrying a rejected login (now every poll asks), the OpenSky
  tab's red pill, the anonymous retry of a poll rejected with a Bearer token.

## Design language — LCARS-inspired (Star Trek console aesthetic)

Build an **LCARS-inspired** look, not a reproduction of the exact copyrighted LCARS design
system — no ripped bitmap assets, no attempt to recreate the specific trademarked typeface.
The spirit (bold color blocks, rounded "elbow" panels, chunky sidebar) is what we're after; the
execution should be original, built from LovyanGFX primitives.

- **Colour scheme "variant A" — warm chrome, cool content** (chosen 2026-10-17; replaces the
  original orange/magenta/cyan look on all three screens). Navigation (header + bottom buttons)
  and data never share a colour family. View modules use the **role** constants (`THEME_*` in
  `lcars_theme.h`), not palette names, so a future re-colour is one edit:

  | Role | Colour | RGB565 |
  |---|---|---|
  | `THEME_CHROME_ACCENT` — header block (data OK), active nav button | peach `#FF9966` | `0xFCCC` |
  | `THEME_CHROME_TEXT` — header date/time | light peach `#FFCC99` | `0xFE73` |
  | `THEME_CHROME_INACTIVE` — inactive nav buttons | lavender `#9977AA` | `0x9BB5` |
  | `THEME_ON_FILL` — text on any filled bar/button | black | `0x0000` |
  | `THEME_ALERT` — health dot on auth/network error, error "!" | red `#FF3333` | `0xF986` |
  | `THEME_WARNING` — warning "!" (device alerts) | orange `#FF8000` | `0xFC00` |
  | header block while rate-limited | `LCARS_YELLOW` | `0xFFE0` |
  | `THEME_CONTENT_PRIMARY` — callsigns, odd table rows | white | `0xFFFF` |
  | `THEME_CONTENT_SECONDARY` — airline/type/route, even rows, radar blips | sky `#99CCFF` | `0x9E7F` |
  | `THEME_CONTENT_MUTED` — radar rings/labels, clamped blips, "no phase" | lavender | `0x9BB5` |
  | `THEME_PILL_FILL` — altitude/speed/distance chips (sky text) | navy `#102040` | `0x1108` |
  | `THEME_TABLE_HEADER` — table column-header bar | lilac `#CC99CC` | `0xCCD9` |

  Phase icons are coloured (`phaseColor()`): takeoff green `#66CC66` (`0x666C`), landing peach,
  overflight sky, none muted. Table rows alternate white / sky (`tableRowTextColor()`). Screen 2
  countdown: approaching sky, imminent `LCARS_YELLOW`, passed peach, minutes mode lilac, empty
  white. Radar home crosshair stays `LCARS_YELLOW`. Background `LCARS_BLACK`. The original
  `LCARS_ORANGE`/`MAGENTA`/`CYAN` constants remain defined but are no longer drawn.

  These are standard RGB565 16-bit values (same encoding TFT_eSPI's predefined color constants
  use) — they work as raw pixel colors on LovyanGFX the same way, since the ILI9341 panel here
  runs in 16-bit color depth by default. Define them as named constants in `lcars_theme`, don't
  scatter raw hex literals through the view modules.
- **Orientation**: landscape, `setRotation(1)` or `setRotation(3)` depending on which way the
  board ends up physically mounted (USB port up vs. down) — both are valid, pick whichever
  matches the physical mounting once decided, don't hardcode a guess.
- **No scrolling, ever**: every screen is drawn at fixed, pixel-perfect coordinates sized to
  exactly fit 320x240 — "paginated" (Screen 1's table) means swapping which fixed set of rows
  is shown, never a smooth/scrolled viewport. If content doesn't fit, it gets paginated or
  truncated, not scrolled.
- **Shapes**: rounded-rectangle panels, an "elbow" sidebar/panel (a rectangle with one corner
  swept into an arc rather than a plain right angle) as a persistent motif — see Screen 1's
  featured-flight frame for a concrete example of this shape with exact bounds and color.
  Pill-shaped labels for the header bar's screen-name block and the bottom nav segments.
- **Typography**: LovyanGFX's built-in fonts sized for legibility at this resolution — bold,
  chunky, minimal decoration. Numeric/status readouts in a monospace-feeling font if available.
- **Chrome stays constant across all three screens**: same header bar, same bottom nav bar —
  only the content area between them changes per screen.
- **New primitives needed for this iteration**: the LCARS swept-corner "elbow" frame (see
  Screen 1's featured panel), bottom-nav dot/segment indicators. WiFi signal bars were drawn up
  as a pure `rssiToBars()` function but have no current home in the layout — hold off drawing
  that primitive until/unless it gets a spot again.
- Keep this as its own module (see `lcars_theme` below) so colors/shapes are defined once and
  reused, not re-picked per screen.

## Code conventions

Arduino framework for ESP32 (not raw ESP-IDF). One `.h`/`.cpp` pair per module in `src/`,
each with a single responsibility:

- `wifi_manager` — WiFi connect + captive-portal fallback (WiFiManager library), no display or
  networking-to-OpenSky logic here.
- `config_portal` — local web page (WebServer or ESPAsyncWebServer) for lat/lon, radius, OpenSky
  client_id/secret, poll interval, and now `auto_cycle_enabled` / `auto_cycle_interval_s` (see
  "Auto screen cycling"). Reachable via mDNS (e.g. `cyd-sky.local`), same pattern as
  micro-radar's `microradar.local`.
  The page HTML is rendered by the pure **`config_page`** module (`renderConfigPage(model,
  sink)`, streamed chunk by chunk, native-tested): five tabs — **Location** (lat/lon, radius +
  credits per request), **OpenSky client** (account status, client id/secret, credentials-file
  upload, poll interval + estimated credits per day vs. the 400/4000 limit), **General**
  (display + web-panel language, auto-cycle, flight-phase thresholds, panel password),
  **Backup** (download / restore, below), **Firmware** (OTA update, see "Firmware updates (OTA)
  & panel password"); one Save for the first three (hidden on Backup and Firmware), the active
  tab round-trips via `?tab=` / a hidden field. The saved `client_secret` is **never written
  into the page** (empty field + "saved" placeholder; empty on save = keep it, a
  `clear_credentials` checkbox removes both).
  **Backup / restore:** `GET /backup.json[?secrets=1]` downloads all settings as JSON
  (grouped like the tabs, `"format": "cyd-sky-tracker-config"`, `format_version` 1; OpenSky
  credentials and the panel password (`"security": {"web_password"}`) only with `secrets=1`); `POST /restore` (multipart `backup_file`) applies such a
  file on top of the current settings — missing keys keep their value, wrong-typed keys are
  skipped and listed, other JSON is rejected; then `saveConfig()` (sanitizes) and a reload.
  The JSON logic is the pure, native-tested **`config_backup`** module. Look: dark (`#17181c`), muted greys,
  thin borders, the display's peach `#ff9966` as the only accent (active tab, buttons, focus)
  with dark text on it.
  Also serves `GET /screenshot.bmp[?screen=N]`: the panel read back (LovyanGFX `readRect`,
  panel `readable = true`) and streamed as a 24-bit BMP — for UX reviews without a camera.
  Pixels come from the UI core in bands (`configPortalSetScreenshotSource()`, see "Concurrency —
  two cores"); BMP header/row math and the bottom-up band order are the pure `screenshot` module
  (native-tested). `?screen=N` switches to that screen first. `GET /perf.json`: `perf_stats`.
  Runs on core 0 (`net_task`).
- `web_auth` — the panel password, pure: `validateWebPassword()` (8–64 printable ASCII, "" =
  none), `decidePasswordChange()` (the General tab's two fields + "remove"), `decideAuth()`
  (allow / 401 challenge / 429 locked), `AuthThrottle` (5 wrong in a row -> 60 s lock), the
  power-up reset gesture timing (`resetHoldState()`).
- `ota_update` — pure OTA logic: `checkImageHeader()` (0xE9 magic, chip id, `0xFF` = the merged
  `-full.bin`), `checkImageSize()`, `FirmwareMarkerScanner` (finds `CYDSKY-FW1|board=…|v=…`
  across chunk boundaries), `OtaSession` (start/feed/finish/abort over an `OtaWriter`
  interface — `config_portal` plugs in arduino-esp32 `Update`, tests a fake),
  `otaResultMessage()` / `otaResultHttpStatus()`, `shouldMarkAppValid()`.
- `release_check` — new versions on GitHub (see "New versions from GitHub"): pure version
  parsing/comparison (`parseFwVersion`, `decideOffer`), release JSON (`parseLatestRelease`,
  `pickLatestPrerelease`), URL allow-list, schedule, `shouldShowUpdatePrompt()`; adapter (TLS check
  + download into `otaFlashSession()`) behind `#ifdef ARDUINO`.
- `service_screen` — full-screen maintenance displays: OTA progress (title, "v1 → v2", bar,
  percent / KB, "do not power off", then restarting / failed) and the password-reset hold +
  YES/NO confirm. Geometry/text pure and tested, drawing behind `#ifdef ARDUINO`.
- `core_link` — what crosses between the cores: `LatestBox`, `Guarded<T>`, `SpinLock`,
  `NetState` / `DataSnapshot` / `OtaProgress`, `NetCommand`, `diffNetState()`,
  `netStateDiffers()`, `uiSleepMs()`, `heartbeatStale()`; device globals behind `#ifdef ARDUINO`.
  See "Concurrency — two cores".
- `net_task` — the network task on core 0 (device-only): owns WiFi, web page, OpenSky,
  hexdb.io, GitHub, OTA, the in-memory Config; publishes `NetState`, takes `NetCommand`s.
- `poll_pipeline` — pure: one `/states/all` answer -> `DataSnapshot` (sort, budgeted lookups via
  injected `LookupPorts`, phases, airports, CPA + Screen 2's pick); phase A = `cacheOnly()`.
  This used to be ~100 untested lines in `main.cpp`.
- `perf_stats` — pure: `GapStats` (UI pass lateness, buckets, p99), `BusyStats`, the `[perf]`
  line and `/perf.json`.
- `hexdb_http` — device-only: the shared hexdb.io HTTPS GET with keep-alive per batch.
- `device_alerts` — the "!" alerts (see "Device alerts"): definitions table, `collectAlerts()`,
  texts, badge colours, `minPollIntervalForLimit()`; pure + `deviceAlertInputs()` adapter.
- `opensky_auth` — did the OpenSky login work (`OpenSkyAuthState`, `openSkyDailyLimit()`); pure.
- `opensky_client` — token fetch/refresh + `/states/all` polling. Returns a plain struct/array
  of aircraft. **No TFT/drawing code in this module** — must be swappable/testable independently
  of the visualization.
- `aircraft_lookup` — `icao24` → airline/aircraft-type enrichment via hexdb.io, with a
  size-limited RAM cache (`lookup_cache`) over the NVS copy (`lookup_store`), so a given
  `icao24` is only ever fetched once. No TFT
  code, no OpenSky polling logic — purely a lookup + cache layer sitting between
  `opensky_client`'s output and the view modules.
- `route_lookup` — `callsign` → origin/destination IATA codes, and airport code → country code,
  both via hexdb.io, each with its own cache (see "Route & airport lookup"). Same rules as
  `aircraft_lookup`: no TFT code, no blocking, degrade to "—" on a miss.
- `flight_phase` — pure classification function (distance + vertical_rate + on_ground →
  NONE/TAKEOFF/LANDING/OVERFLIGHT), see "Flight phase" above. No I/O of any kind — this is the
  easiest module in the project to get full test coverage on, there's no excuse to skip it.
- `time_sync` — NTP setup (`configTzTime`) and sync-state, plus pure date/time formatting
  functions that take a `time_t` parameter rather than reading the wall clock internally (see
  "Time & WiFi status"). No TFT code.
- `cpa_predictor` — pure closest-point-of-approach time math (see "Flight ETA"). No I/O of any
  kind, and arguably the single easiest-to-fully-test module in the project — it's just
  trigonometry and a division.
- `screen_nav` — current screen index (0/1/2) + `nextScreen()`/`prevScreen()` with wraparound,
  touch-zone hit-testing for the left/right edge strips and bottom nav segments (reuse the
  `hitTest` pattern from Milestone 9), and the auto-cycle timer + `shouldDeferAutoSwitch()` (see
  "Auto screen cycling") — takes `t_cpa_seconds` as a parameter, doesn't call `cpa_predictor`
  itself. No TFT drawing, no OpenSky/hexdb.io calls.
- `aircraft_names` — built-in tables (pure): **airline from the callsign's ICAO prefix**
  (`SXS6JZ` -> SunExpress; 3 letters + digit only, so registrations like `SPKAX` are left alone)
  for when hexdb.io doesn't know the airframe; and **friendly aircraft types** from hexdb.io's
  `ICAOTypeCode` (`B38M` -> "737 MAX 8", `E75L` -> "E175", <= 12 chars to fit the TYP column)
  instead of its long, truncated `Type` strings. `aircraft_summary` uses hexdb.io first, then
  these tables, then `...` / `--`.
- `lookup_cache` — header-only `LookupCache<V>`: the LRU RAM cache behind `aircraft_lookup` and
  `route_lookup` (spec 05). `find()` refreshes an entry, `contains()` doesn't (the per-poll
  budget asks about every aircraft — asking isn't using), `put()` drops the least recently used
  when full. Capacities `kAircraftCacheCapacity` 96, `kRouteCacheCapacity` 96,
  `kAirportCacheCapacity` 64 — each must hold one poll's working set (all aircraft in range),
  or every poll would go back to NVS. Sizes: `aircraftLookupCacheSize()` etc. -> `[perf]`.
- `lookup_store` — persists the hexdb.io caches (aircraft / route / airport) in NVS
  (namespaces `lk_ac`, `lk_rt`, `lk_ap`; record = version + fields joined by 0x1F; confirmed
  misses stored too). Lookups check RAM, then NVS, then hexdb.io; NVS hits count as free in the
  per-poll budget. When NVS is full that kind's namespace is wiped (settings live in their own
  namespace, untouched). Encode/decode is pure and native-tested.
- `place_names` — readable place names for the detail views (pure): built-in IATA -> city table
  (PL + EN, ~120 airports likely over Poland), else hexdb.io airport name minus "International
  Airport", else IATA / ICAO; plus `asciiFold()` (UTF-8 -> drawable ASCII; hexdb.io names contain
  en dashes and accents). hexdb.io has no city field, hence the table.
- `i18n` — PL/EN string table (X-macro), `tr()`, current language, `pl`/`en` codes. See
  "Language". Pure, no I/O.
- `redraw_policy` — pure `computeRedrawMask(events, currentScreen)`: turns "what happened"
  (first draw, screen/page change, new poll, minute rollover, health change, countdown tick)
  into which regions to repaint (full / content / header text / countdown). See "Redraw policy".
  No TFT code — `main.cpp` acts on the mask (the events come from `diffNetState()` and the UI's
  own timers).
- `lcars_theme` — shared color palette, panel/elbow/pill drawing helpers, fonts, plus the WiFi
  signal-bars and bottom-nav-segment primitives. All view/screen modules call into this rather
  than defining their own colors/shapes.
- `status_bar` — draws the shared chrome: the header bar per "Screen chrome" (orange
  `fillRoundRect` block on the left labeled with the active screen name, date `DD.MM.YYYY` + time
  from `time_sync` to its right), **and** the bottom nav bar's dot/segment indicators from
  "Screen navigation" (highlighting whichever index `screen_nav` reports as current). Both
  called once per frame from `main.cpp`, before/after dispatching to the active screen — kept
  here rather than in `screen_nav` so `screen_nav` itself stays TFT-free and fully testable;
  `status_bar` just reads the current index, it doesn't own navigation state or hit-testing.
- `aircraft_summary` — shared identity-block renderer (flight, airline, aircraft type, origin →
  destination). Used by `featured_panel` (Screen 1's top zone), `flight_screen` (Screen 2), and
  `radar_view`'s right-hand panel (Screen 3) — centralized specifically so this rendering isn't
  duplicated three times across three screens.
- `featured_panel` — draws Screen 1's top zone, now built on top of `aircraft_summary` for the
  identity-block portion plus its own altitude/speed/distance stat chips. Takes one enriched
  aircraft record as input, uses `lcars_theme` for chrome. No networking code.
- `table_view` — draws Screen 1's content area (below the header): calls `featured_panel` for
  the top zone, draws the paginated row list for everything else. Takes the enriched aircraft
  array + home position as input, uses `lcars_theme` for chrome. No networking code.
- `flight_screen` — draws Screen 2's content area: calls `aircraft_summary` for the identity
  block, draws the seconds/minutes countdown using `cpa_predictor`'s output. No networking code.
- `radar_view` — draws Screen 3's content area: the radar circle in a left-side bounding rect it
  receives (not the full screen anymore) plus a call to `aircraft_summary` for the right-side
  panel. Same enriched-aircraft input contract as the other screens. No networking code.
- `backlight` — automatic brightness from the light sensor, night mode, touch wake-up,
  "may we poll OpenSky at night" (see "Brightness & night mode"); pure logic + an `#ifdef
  ARDUINO` adapter (ADC, `tft.setBrightness()`, `backlightStatus()` for the config page).
- `sun_times` — sunrise / sunset from date + lat/lon (pure; no network).
- `config_store` — Preferences/NVS read/write for all persisted settings, including
  `last_screen` (0/1/2) — replaces the old two-screen `last_view` field — and
  `auto_cycle_enabled` / `auto_cycle_interval_s` (see "Auto screen cycling").

Other rules:
- Never hardcode WiFi or OpenSky secrets in source files.
- Prefer `millis()`-based non-blocking timing over `delay()` so touch input and rendering stay
  responsive between polls.
- Keep `opensky_client` fully decoupled from `table_view`/`flight_screen`/`radar_view` — that
  boundary is what lets all three screens share one data source without duplicating polling
  logic.
- `table_view`, `flight_screen`, and `radar_view` should not duplicate color/shape definitions —
  anything visual that appears on more than one screen belongs in `lcars_theme`, and anything
  that's specifically the aircraft identity block belongs in `aircraft_summary`.

## Testing — logic gets covered first, not "later"

Test-first is the default here, not a cleanup pass bolted on at the end:

- Before implementing any non-trivial piece of logic — bbox math, distance/bearing calculations,
  sorting aircraft by distance, altitude bucketing, token-expiry timing, cache hit/miss behavior
  in `aircraft_lookup`/`route_lookup`, flight-phase classification, CPA time prediction,
  RSSI-to-bars/date-time formatting, screen-index wraparound, `shouldDeferAutoSwitch`'s hold
  condition (boundary cases at exactly `6`/`-5` seconds matter here), credit-cost calculation,
  JSON parsing/mapping, etc. — write the test for it in `test/` first, or at the very least
  alongside it, then implement until it passes. "Add tests later" is not a real plan on a solo
  hobby project — later doesn't happen, so don't rely on it.
- This is exactly why the module boundaries in "Code conventions" above are non-negotiable:
  `opensky_client`, `aircraft_lookup`, `route_lookup`, `flight_phase`, `time_sync`,
  `cpa_predictor`, `screen_nav`, `table_view`, `flight_screen`, `radar_view`, `featured_panel`,
  `aircraft_summary`, and `lcars_theme` all keep their actual logic separate from LovyanGFX /
  WiFiClientSecure / HTTPClient / touch-read calls specifically so that logic can run in
  PlatformIO's `native` environment — no ESP32, no display, no network required, just
  `pio test -e native`.
- Only the thin adapter code that directly calls hardware/SDK APIs (the actual TFT draw calls,
  the actual HTTP request, the actual touch read) is allowed to go untested by Unity. Everything
  that adapter code delegates to should not be — if a function is pure enough to unit test, it
  needs one.
- When asked to add a new module or feature, default to proposing the test file alongside (or
  before) the implementation file — don't wait to be asked for tests as a separate step.
- Before considering any milestone from the step plan "done," run `pio test -e native` and
  confirm the new logic introduced in that milestone has coverage — not just that the firmware
  compiles and uploads. Compiling is not the same as correct.
- **Layout** (spec 07, details in `test/README.md`): **six suites**, one executable each,
  grouped by the contexts of spec 12 — `test_airspace` (phase, CPA, poll pipeline),
  `test_enrichment` (hexdb.io lookups, caches, name tables), `test_opensky`, `test_display`
  (views, theme, nav, touch, backlight, i18n), `test_admin` (config page / store / backup, web
  auth, OTA, release check, version, screenshot, device alerts), `test_platform` (core_link, perf, WiFi, time,
  sun). A module's tests are `test/test_<suite>/<module>_test.cpp`: an anonymous namespace plus
  one `run_<module>_tests()`; the suite's `main.cpp` calls them and is the only place with
  `setUp()` (-> `resetAllForTesting()`; new global state gets its reset there). One module:
  `PLATFORMIO_BUILD_FLAGS="-DTEST_MODULE=<module>"`.
- **Assertions that print values** (`test/support/test_support.h`): `ASSERT_ENUM_EQ`,
  `ASSERT_STR_EQ`, `ASSERT_CONTAINS` / `ASSERT_NOT_CONTAINS` (string or vector of strings), and
  for JSON `json_assert.h` (`assertJsonBool(doc, "general.update_check", false)`) — never
  `TEST_ASSERT_TRUE(a == b)` for one value or a hand-written `contains()`. Test data:
  `fixtures.h` (`makeAircraft()`, `makeRow()`, `makeConfig()`, `stubPorts()`).
- The `native` env builds with `-Wall -Wextra -Werror` (tests and `src/`); CI also runs
  `native-asan` (AddressSanitizer + UBSan, Linux only — MinGW has no sanitizer runtime).

## platformio.ini starting point

```ini
[env:cyd]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.partitions = min_spiffs.csv  ; two 1.875 MB OTA slots; NVS at the same offset as before
lib_deps =
    lovyan03/LovyanGFX
    tzapu/WiFiManager
    bblanchon/ArduinoJson
```
Unlike TFT_eSPI, LovyanGFX takes no display `build_flags` — the panel/bus/backlight pinout is
set in C++ via a custom `LGFX_Device` subclass, kept in `include/LGFX_CYD.hpp`:

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

 public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host = HSPI_HOST; cfg.spi_mode = 0;  // VSPI is the touch controller's
      cfg.freq_write = 40000000; cfg.freq_read = 16000000;
      cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_dc = 2;
      _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs = 15; cfg.pin_rst = -1; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 320;
      _panel_instance.config(cfg); }
    { auto cfg = _light_instance.config();
      cfg.pin_bl = 21; cfg.pwm_channel = 7;
      _light_instance.config(cfg); _panel_instance.setLight(&_light_instance); }
    { auto cfg = _touch_instance.config();   // lgfx::Touch_XPT2046 member, own SPI bus
      cfg.spi_host = VSPI_HOST; cfg.bus_shared = false; cfg.freq = 1000000;
      cfg.pin_sclk = 25; cfg.pin_mosi = 32; cfg.pin_miso = 39; cfg.pin_cs = 33; cfg.pin_int = 36;
      cfg.x_min = kTouchRawXMin; /* ...see touch_calibration.h */ cfg.offset_rotation = 0;
      _touch_instance.config(cfg); _panel_instance.setTouch(&_touch_instance); }
    setPanel(&_panel_instance);
  }
};
```

Also add a native test environment — this was missing from this file, but the "Testing" section
above depends on `pio test -e native` existing:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -I test/support          ; shared assertions + fixtures (test/README.md)
    -Wall -Wextra -Werror
; No LovyanGFX/Arduino.h here on purpose — this env only compiles modules whose testable
; logic is plain C++ (structs, functions), which is exactly what "Code conventions" and
; "Testing" above require. If a file won't compile under `native`, that's a signal it still
; has hardware-only code mixed into logic that should be pure.
```

This is a starting point, not gospel — verify it compiles and renders correctly on the real
board before building anything on top of it; TFT/panel setups are notoriously fiddly and small
mistakes here waste hours of debugging later.

## Versioning & CI

- **Version = git tag.** `tools/pio_version.py` (PlatformIO `extra_scripts = pre:`) runs
  `git describe --tags --always --dirty` (or takes `$FW_VERSION`) and passes it as the
  `FW_VERSION` macro to `src/version.cpp` only (build middleware — a new version recompiles one
  file). `firmwareVersion()` returns it, `"dev"` when not injected (native tests). Shown **only**
  in the config web page footer — not on the display.
- The script also writes `.pio/build/cyd/cyd-sky-tracker-<version>.bin` (copy of firmware.bin)
  and `version.txt`.
- **GitHub Actions — four workflows** (`.github/workflows/`):
  - `firmware.yml` — push to main / manual, and reusable (`workflow_call`, inputs `version` =
    forced `FW_VERSION`, `ref` = commit): runs `pio test -e native`, `pio test -e native-asan`,
    the BDD check (`tools/test_check_bdd.py`, `tools/check_bdd.py`), builds, uploads artifact `cyd-sky-tracker-<version>` with
    `cyd-sky-tracker-<version>.bin` (app, OTA or 0x10000), `…-full.bin` (merged image, 0x0),
    `bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `version.txt`, `SHA256SUMS.txt`.
  - `site.yml` — changes under `site/`, `docs/screenshots`, `docs/icons` on main / manual, and
    reusable: builds the page and deploys it to Pages. On its own it takes the installer's
    firmware from the **latest GitHub Release** (no release -> page without the installer);
    called with `firmware-artifact` it uses that artifact.
  - `release.yml` — tags `v*`: calls `firmware.yml`, creates the GitHub Release with its files,
    then calls `site.yml` with the same artifact.
  - `pr.yml` — every push to a PR against main: version = next patch after the latest release +
    `-dev.<run number>` (`v1.4.2` -> `v1.4.3-dev.57`), `firmware.yml` on the PR's head commit with
    it, then (same-repo PRs only) a pre-release `v1.4.3-dev.57 · PR #12` (the PR's previous one is
    deleted first) and one PR comment kept up to date. Closing the PR deletes its dev releases
    and tags. Tags made with `GITHUB_TOKEN` don't trigger `release.yml` — on purpose.
  Platform and library versions are pinned in `platformio.ini` so CI and local builds match —
  bump them deliberately (and rerun `tools/subset_efont.py` after a LovyanGFX bump).
- Release: `git tag v1.2.0 && git push origin v1.2.0`.
- **Project page** (GitHub Pages, https://alasota.github.io/CYD-Flight-Tracker/): source in
  `site/` (static HTML/CSS/JS, PL/EN — every text is a `.en` / `.pl` element pair, CSS shows
  one; light, humorous tone). `tools/build_site.py` assembles `_site/` = `site/` +
  `docs/screenshots` + `docs/icons` + `firmware/` (bootloader, partitions, boot_app0, app +
  `manifest.json` for the ESP Web Tools install button). The manifest lists the four parts
  separately, **not** the merged -full.bin, so an update without "Erase device" keeps NVS
  (settings). Published by `site.yml` (see above).
  The page's "What's next" section is generated from **`ROADMAP.md`** (replaces the
  `<!-- ROADMAP -->` marker in `site/index.html`): `## Planned` / `## In progress` / `## Done`
  headings, each with a **rated table**, rows in priority order — `# | Item | Value | Effort |
  Risk | Rating | Verdict`. Item and Verdict cells carry their Polish text in an inline
  `<!-- pl: … -->` comment (hidden on GitHub); Value/Effort/Risk use fixed words
  (high/medium/low, small/medium/large) that `build_site.py` translates (`WORDS_PL`); Rating is
  ⭐ stars. Editing ROADMAP.md on main republishes the page.
  The "Current version" badge (`<!-- VERSION -->`) and the "What changed" section
  (`<!-- CHANGELOG -->`) come from git: one entry per `v*` tag (newest first, newest expanded)
  listing the non-merge commit subjects since the previous tag, max 15 + a GitHub compare
  link. Hence `site.yml` checks out with `fetch-depth: 0`. Commit subjects are shown as they
  are (not translated) — write them so they read well in a changelog.
  `site/img/device.jpg` is a placeholder for a real photo: 1600×1000 px JPG, ≤ ~300 KB.

## Spec → PR harness (`/implement`)

One spec (`spec/planned/NN-*.md`) or one GitHub issue -> its own branch -> a PR, done by agents:
`/implement 05` / `/implement #12` (`.claude/skills/implement/SKILL.md`, the orchestrator) runs
`spec-analyst` (plan + BDD `.feature`, then **stops for the owner's approval**) -> `test-author`
(failing tests first) -> `implementer` -> `docs-writer` (docs, screenshots, spec to `done/`) ->
`reviewer` (up to two fix rounds) -> PR from `.github/pull_request_template.md`. Shared rules:
`.claude/harness/conventions.md`; hand-over files in `.claude/work/<slug>/` (git-ignored).
- **BDD:** `spec/**/NN-name.feature` next to its spec; every Scenario is tagged
  `@test=<suite>/<file>.cpp:<function>` (a Unity test that must exist and be `RUN_TEST`ed) or
  `@manual` (checked on the board, listed in the PR). `tools/check_bdd.py` enforces it in CI;
  `--list` / `--manual <feature>` print the PR tables.
- Branches `spec/NN-name` / `issue/N-name`; the agents never commit to main or merge — merging
  and tagging releases stay with the owner.
- PR creation uses `gh` (not installed on this machine yet: `winget install GitHub.cli`,
  `gh auth login`); without it the orchestrator leaves a compare link + `pr.md`.

## Commands

- Build: `pio run`
- Upload: `pio run -t upload`
- Monitor: `pio device monitor -b 115200`
- Test (native, logic only, no hardware): `pio test -e native` — one suite:
  `pio test -e native -f test_display`; with sanitizers (Linux): `pio test -e native-asan`
- BDD check: `python tools/check_bdd.py` (its own tests: `python tools/test_check_bdd.py`)
- Implement a spec / issue on a branch up to a PR: `/implement 05`, `/implement #12`
- Screenshots of the config page (General, OpenSky, Firmware tabs) and the update question:
  `python tools/screenshots.py [--device cyd-sky.local]`

## Out of scope for MVP

- Historical flight tracks (`/flights/*`, `/tracks/*` endpoints) — only live `/states/all`.
- MQTT / Home Assistant integration — this is a standalone display, not a sensor node.
- Multi-aircraft trajectory prediction or trails.
- Own-receiver mode (`/states/own`) — this project consumes network-wide data, not a personal
  ADS-B feeder.
- Animated screen transitions (slide/fade between Flights/Flight/Radar) — instant redraw is
  fine, don't spend time on transition animation.
- Turn-aware flight path prediction — `cpa_predictor` is straight-line extrapolation only, on
  purpose (see "Flight ETA"); don't try to model turns or holding patterns.
