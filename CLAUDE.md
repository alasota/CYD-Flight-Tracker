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
- Touch: **XPT2046_Touchscreen** (PaulStoffregen) — used for the table/radar view toggle button
  and table paging/scrolling (see Visualization concept). Stays on its own library/SPI bus,
  independent of the LovyanGFX display bus.
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
  - **Cache every successful lookup by `icao24` in RAM (and ideally NVS)** — an aircraft's
    airline/type essentially never changes, so a given `icao24` should only ever be looked up
    once per boot (or ever, if cached to flash). Re-fetching on every poll would be both rude
    and pointless.
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
    (mostly the same handful of nearby airports repeating), so it's cheap to keep in RAM even
    without NVS persistence.
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
- Icons (drawn via `lcars_theme` primitives, not bitmap assets): up chevron = takeoff, down
  chevron = landing, sideways chevron = overflight, nothing/blank = `NONE`.

## Time & WiFi status (for the header bar)

Needed because every screen now shows a real clock, not just uptime — this is a new
requirement, the firmware didn't sync wall-clock time before.

- **NTP sync**: after WiFi connects (in `wifi_manager` or right after in `main.cpp`), call
  `configTzTime(TZ_WARSAW, "pool.ntp.org", "time.nist.gov")` where
  `TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3"` — the standard POSIX TZ rule for Europe/Warsaw
  (correctly handles CEST DST switches). This is a stable, long-standing EU DST rule, but
  double-check it hasn't changed if clocks ever look off by an hour.
- Until the first sync completes, `time_sync` must report "not synced yet" — the header bar
  should show a placeholder (e.g. `--:--`) rather than the garbage epoch-zero date ESP32 starts
  with.
- `time_sync` module: `isTimeSynced()`, plus **pure, testable** formatting functions that take
  a `time_t` as a parameter rather than calling `time(nullptr)` internally — e.g.
  `formatDate(time_t) -> "DD.MM.YYYY"`, `formatTime(time_t) -> "HH:MM"`. Keeping the epoch as an
  injected parameter (instead of reading the wall clock inside the function) is what makes this
  testable without depending on when the test happens to run.
- **WiFi signal**: `rssiToBars(int rssi_dbm) -> 0..4`, pure function, from `WiFi.RSSI()`.
  Rough default thresholds (adjust after seeing real readings): ≥ -55 dBm → 4, -55..-65 → 3,
  -65..-75 → 2, -75..-85 → 1, below that → 0. Kept as a utility function even though the
  current header layout (see "Screen chrome" below) doesn't have a slot for it — say if you
  want it added back somewhere and I'll find room.
- **Stardate (decorative, not canonical Star Trek lore)**: the header shows `STARDATE: XXXX.XX`
  instead of a real calendar date. Compute it with a small deterministic formula from the real
  date rather than hardcoding a static string — e.g. `stardate = (year - 2323) * 1000 +
  day_of_year * 2.7378` (one of many fan approximations; there's no official formula, so this
  is purely flavor text, tune the constants if you want a different "feel" to the numbers). Pure
  function, same `time_t`-in / string-out pattern as `formatDate`/`formatTime`, lives in
  `time_sync` alongside them.

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

**Exact pixel spec, y: 0 to 25px, full width (320px):**
- LCARS block on the left: filled rounded rectangle (`fillRoundRect`), color `LCARS_ORANGE`
  (`0xFC00`). Label the screen name inside it in small bold text (`FLIGHTS` / `FLIGHT` /
  `RADAR`) — this is how each screen stays identifiable now that the header's main text is
  stardate/time rather than a screen-name label; keep the block wide enough for the longest
  name (`FLIGHTS`, 7 characters) at whatever font size fits 25px tall comfortably.
- To the right of that block: `STARDATE: XXXX.XX` and the real time `HH:MM:SS`, monospace /
  built-in Adafruit-style font, color `LCARS_CYAN` (`0x07FF`) or white.
- **WiFi bars are dropped from this header** in this revision — the earlier three-zone design
  (date/time left, name center, WiFi right) is superseded by this spec. Say if you want signal
  strength back somewhere; there's no slot for it here right now.

Define `LCARS_HEADER_HEIGHT = 25` as a shared constant in `lcars_theme` that every screen's
content starts below — keeps all three screens visually aligned instead of each guessing its
own offset. Drawn by a dedicated `status_bar` module, called once per frame from `main.cpp`
before dispatching to whichever screen is active — screens themselves don't need to know
`status_bar` exists, they just draw their content starting at `y = LCARS_HEADER_HEIGHT`.

### Screen 1 — "Flights" (default view, prioritize this first)

**Exact pixel spec** (below the 25px header), no scrolling — everything drawn at fixed
coordinates to fit the 320x240 frame, consistent with "Design language" below:

**Featured flight section, y: 28 to 105px (~77px tall).**
- Frame: an LCARS-style bordered panel in `LCARS_MAGENTA` (`0xF81F`), with the classic LCARS
  swept/cut corner — one corner (top-left) cut at an angle or arc rather than a plain rounded
  rect. LovyanGFX has arc primitives (`fillArc`/`drawArc`) — verify the exact method names
  against the installed LovyanGFX version when implementing, don't assume from memory. This is
  the same "elbow" shape already described in "Design language" — just now with a specific
  color and bounds.
- Highlighted text (top of the frame): flight (callsign), airline, aircraft type, phase icon —
  what's currently Line 1 of the featured panel, unchanged in content.
- Detail line below it (bottom of the frame): the rest — origin → destination with country
  codes, plus the altitude/speed/distance stat chips — what's currently Line 2 + stat chips,
  unchanged in content, rendered smaller (8px / LovyanGFX's equivalent of Adafruit "Font 2" —
  again, verify the exact font reference name for LovyanGFX rather than assuming the TFT_eSPI
  name carries over).
- Empty state ("no aircraft in range") still applies here, same as before — just now within
  these exact bounds.

**Flights table section, y: 108 to 238px (~130px tall).**
- Sub-header bar: `LCARS_CYAN` (`0x07FF`) background, black text, label `FLIGHTS`. (Yes, this
  repeats the word already shown in the header's orange block — that's fine, LCARS designs
  repeat short labels as a matter of style, not a bug to fix.)
- Column header row at **y: 125px**: `LCARS_ORANGE` (`0xFC00`) background, black text, one
  text string with column labels separated by `|` (not per-column shaded cells) — e.g.
  `LOT | LINIA | SKĄD | DOKĄD | TYP | FAZA` matching the columns already defined below.
- **Exactly 5 data rows**, starting at **y: 140px**, **18px step** (rows at y = 140, 158, 176,
  194, 212) — this replaces the earlier "row count depends on row height" language with a fixed
  number. If more than 5 aircraft are in range, page via touch (Milestone 9's mechanism) rather
  than trying to fit more — pagination swaps which 5 are shown, it is not scrolling.
- Columns per row: **flight (callsign), airline, from (IATA code), to (IATA code), aircraft
  type, phase icon**. Airport codes only — no country codes, no room for them at 18px row
  height.
- Airline/type come from `aircraft_lookup`; from/to come from `route_lookup`; phase from
  `flight_phase`. Any of these being unresolved/not-found shows "—" in that cell, never blocks
  the row.
- Sorted by distance ascending, **excluding** whichever aircraft is currently shown in the
  featured panel above.
- This is the priority screen — get it solid before spending time on the other two.

### Screen 2 — "Flight" (single nearest aircraft + overhead countdown)

Deliberately minimal — one aircraft, one number that matters: when will it actually be
overhead. **Exact pixel spec**, below the 25px header, no scrolling:

**Identity panel, y: 28 to 78px (50px tall, x: 10 to 310).**
- Same LCARS elbow frame as Screen 1's featured panel — `LCARS_MAGENTA` (`0xF81F`) border,
  top-left corner swept/cut, not a plain rounded rect.
- Line 1 (~y:40): flight (callsign), airline, aircraft type, phase icon — via
  `aircraft_summary`, same content as Screen 1's featured panel Line 1.
- Line 2 (~y:62, smaller font, 8px / LovyanGFX's Font-2 equivalent): origin → destination as
  IATA + country code, e.g. `WAW (PL) → FCO (IT)` — same as Screen 1's Line 2.

**Countdown zone, y: 82 to 195px (113px tall), horizontally centered.**
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

**Status strip, y: 200 to 238px (38px tall), centered text.**
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
- When `t_cpa_seconds` drops below -10, don't hold onto a stale prediction — next poll
  recomputes "nearest aircraft" and its CPA as normal, so the screen naturally moves on to
  whatever is nearest now.

### Screen 3 — "Radar" (left: radar, right: nearest-aircraft summary)

**Exact pixel spec**, below the 25px header, content area y: 28 to 238px (210px tall):

**Divider, x: 192 to 200px (8px wide), full content height.**
- A thin vertical `LCARS_ORANGE` (`0xFC00`) accent bar — the same "elbow sidebar" motif from
  "Design language", here doing double duty as the visual split between radar and summary.

**Radar, x: 0 to 190px, y: 28 to 238px.**
- Center at `(95, 133)`, radius `85px` — fits with margin on all sides within this zone.
- 3 concentric rings at radius `28px` / `57px` / `85px` (thirds of max radius), each labeled
  in small text near the top of the ring with its corresponding distance in km (derived from
  `max_distance_km / 3`, `2/3`, full — see `computeRingDistances` from the radar geometry work).
- Home marker: small crosshair or dot, `LCARS_YELLOW` (`0xFFE0`), at center.
- Aircraft blips: small dots (~5px), `LCARS_CYAN` (`0x07FF`) normally, dimmed/smaller if
  `clamped == true` (beyond `max_distance_km`, per `polarToScreen`'s clamping — see the earlier
  radar geometry step).
- Static plot, no sweep animation, consistent with the earlier radar_view spec.

**Summary panel, x: 200 to 320px (120px wide), y: 28 to 238px.**
- Same LCARS elbow frame convention as Screen 1/2 — `LCARS_MAGENTA` (`0xF81F`) border, top-left
  corner (the one facing the divider) swept/cut.
- Three rows, via `aircraft_summary`, vertically spaced within the panel:
  - Row 1 (~y:95): flight (callsign), bold/emphasized.
  - Row 2 (~y:130): airline — truncate or abbreviate if it doesn't fit the 120px width, don't
    let it overflow into the radar zone.
  - Row 3 (~y:165): origin → destination as IATA codes, e.g. `WAW → FCO` (no country codes
    here — this panel is narrower than Screen 1's featured panel, keep it to what fits).
- **Empty state**: if no aircraft in range, radar still draws its (empty) rings and home
  marker; this panel shows a centered `BRAK LOTU` / em-dash instead of the three rows.

### Screen navigation

- **Order**: Flights (0) → Flight (1) → Radar (2) → wraps back to Flights. Managed by a new
  `screen_nav` module (current index + `nextScreen()`/`prevScreen()` with wraparound — small,
  but worth testing, off-by-one bugs in wraparound logic are easy to introduce).
- **Touch zones, below the header**: narrow strips at the **left and right edges** (roughly the
  outer 15-20% of width each) switch screens — left edge = previous, right edge = next. The
  **middle majority of the screen is deliberately left alone** for screen-specific interactions
  — this matters concretely on Screen 1, where the table already uses touch for pagination
  (Milestone 9); a full-width left/right split would fight with that.
- **Bottom nav bar**: a persistent thin strip at the very bottom of the screen, present on all
  three screens, showing three tappable dots/segments (one per screen), the active one
  highlighted. Tapping a segment jumps directly to that screen. This is a proposed default, not
  something explicitly specified — a single cyclic "next" button would also satisfy "przycisk
  na dole ekranu" if you'd rather keep it simpler; flag it if you want that instead.
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


## Design language — LCARS-inspired (Star Trek console aesthetic)

Build an **LCARS-inspired** look, not a reproduction of the exact copyrighted LCARS design
system — no ripped bitmap assets, no attempt to recreate the specific trademarked typeface.
The spirit (bold color blocks, rounded "elbow" panels, chunky sidebar) is what we're after; the
execution should be original, built from LovyanGFX primitives.

- **Palette — exact values, not vague color names**:
  - Background: `LCARS_BLACK` = `0x0000`
  - Orange: `LCARS_ORANGE` = `0xFC00`
  - Magenta/pink: `LCARS_MAGENTA` = `0xF81F`
  - Cyan/blue: `LCARS_CYAN` = `0x07FF`
  - Yellow: `LCARS_YELLOW` = `0xFFE0`

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
- `opensky_client` — token fetch/refresh + `/states/all` polling. Returns a plain struct/array
  of aircraft. **No TFT/drawing code in this module** — must be swappable/testable independently
  of the visualization.
- `aircraft_lookup` — `icao24` → airline/aircraft-type enrichment via hexdb.io, with an
  in-memory (ideally NVS-backed) cache so a given `icao24` is only ever looked up once. No TFT
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
- `lcars_theme` — shared color palette, panel/elbow/pill drawing helpers, fonts, plus the WiFi
  signal-bars and bottom-nav-segment primitives. All view/screen modules call into this rather
  than defining their own colors/shapes.
- `status_bar` — draws the shared chrome: the header bar per "Screen chrome" (orange
  `fillRoundRect` block on the left labeled with the active screen name, stardate + real time
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
  WiFiClientSecure / HTTPClient / XPT2046 calls specifically so that logic can run in
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

## platformio.ini starting point

```ini
[env:cyd]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    lovyan03/LovyanGFX
    paulstoffregen/XPT2046_Touchscreen
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

 public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST; cfg.spi_mode = 0;
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
    -std=c++17
; No LovyanGFX/Arduino.h here on purpose — this env only compiles modules whose testable
; logic is plain C++ (structs, functions), which is exactly what "Code conventions" and
; "Testing" above require. If a file won't compile under `native`, that's a signal it still
; has hardware-only code mixed into logic that should be pure.
```

This is a starting point, not gospel — verify it compiles and renders correctly on the real
board before building anything on top of it; TFT/panel setups are notoriously fiddly and small
mistakes here waste hours of debugging later.

## Commands

- Build: `pio run`
- Upload: `pio run -t upload`
- Monitor: `pio device monitor -b 115200`
- Test (native, logic only, no hardware): `pio test -e native`

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
