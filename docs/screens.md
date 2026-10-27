# Screens — what the display shows

How the three screens, the header and the tab bar look and behave **now**. One version only;
how they got this way is in [history.md](history.md), the drawing rules (what to repaint when)
in CLAUDE.md "Redraw policy". Numbers below come from the code constants named next to them —
if they disagree, the code wins and this file gets fixed.

Three screens, cycled in this fixed order: **Flights → Flight (NOW) → Radar → back to
Flights**. All share the header and the tab bar; only the content area between them changes.

| Region | y (px) | Constant |
|---|---|---|
| Header | 0..15 | `LCARS_HEADER_HEIGHT` = 15 |
| Content | 18..215 | `LCARS_CONTENT_TOP` (header + 3 px gap), `LCARS_CONTENT_BOTTOM` |
| Tab bar | 215..240 | `LCARS_BOTTOM_NAV_HEIGHT` = 25 |

Every screen is drawn at fixed coordinates in 320×240 landscape — no scrolling, ever. Too much
content is paginated (the table) or truncated, not scrolled.

## Screen chrome — header bar and tab bar

Drawn by `status_bar` (header) and `drawBottomNav()` (`lcars_theme`, geometry from
`screen_nav`), called by the UI (`main.cpp`) when the redraw mask says so. Screens don't know
the chrome exists; they draw inside the content area.

**Header, y 0..15, three zones, nothing filled** (`statusBarZones()`, native-tested so the zones
never overlap for the longest texts in either language):

```
SKY TRACKER  !        ● 7 LOTÓW          18.10.2026 09:11
```

- **Left:** app name `kStatusBarAppName` ("SKY TRACKER"), `LCARS_FONT_SMALL`, dim lavender
  (`THEME_CHROME_DIM`).
- **Device-alert "!"** (only while an alert is active): a 13 px circle (`kAlertBadgeDiameter`),
  red `THEME_ALERT` for an error, orange `THEME_WARNING` for a warning, black "!", y 1..13,
  4 px right of the name. Tapping it opens the alert screen — see architecture.md "Device
  alerts".
- **Centre:** a **health dot** (`statusBarHealthColor()`: green = data OK, yellow =
  rate-limited / backing off, red = auth or network error — the list may be stale) and the
  **number of aircraft in range** in white with the right plural (PL `1 LOT` / `3 LOTY` /
  `7 LOTÓW`, EN `1 FLIGHT` / `7 FLIGHTS`; `99+` above 99; `--` until the first good poll).
  Centred in the gap between the name (or the "!") and the date.
- **Right:** date `DD.MM.YYYY` (light peach, `THEME_CHROME_TEXT`) and time `HH:MM` (white),
  both `LCARS_FONT_CLOCK` (DejaVu12). Placed with measured widths — the font is proportional.
  No seconds: the header is repainted once a minute, `HH:MM:SS` would sit frozen. Before the
  first NTP sync: `--.--.----` and `--:--`.
- A **1 px lilac rule** along the header's bottom edge.
- Repainted on its own (`kRedrawHeader`) on a new poll (count), a health change (dot), an alert
  badge change and a minute rollover (clock).

**Tab bar, y 215..240** — three folder tabs hanging from a 2 px peach rule (`kNavTabRulePx`)
along the bar's top edge, one per ~107 px segment, labels `LOTY` / `TERAZ` / `RADAR`
(EN `FLIGHTS` / `NOW` / `RADAR`) in `LCARS_FONT_BODY`. Square top corners, rounded bottom ones
(`kNavTabCornerRadius` 6).
- **Active tab:** peach (`THEME_CHROME_ACCENT`), full height, merges into the rule, overlaps
  each neighbour by 2 px, black label.
- **Inactive tabs:** recessed 2 px below the rule, dark fill `THEME_CHROME_RECESSED`, lavender
  outline + label.
- Geometry: the pure `navTabRule()` / `navTabRect()` in `screen_nav` (native-tested). Touch
  zones are the plain segments; `navHitTest()` accepts taps from y 210, 5 px above the bar
  (`kBottomNavTouchExtra`).

Empty screens say **why** they are empty (`emptyStateText(health, shortForm)`, `status_bar`):
only with a healthy feed "BRAK LOTÓW W ZASIĘGU" / "BRAK LOTU"; credits exhausted "LIMIT OPENSKY
WYCZERPANY" / "LIMIT API"; login rejected even anonymously "BŁĄD LOGOWANIA OPENSKY"; no answer
"BRAK DOSTĘPU DO OPENSKY" ("BŁĄD API" short). Aircraft from the last good poll stay on screen;
`main.cpp` repaints the content when the health changes and the list is empty.

## Screen 1 — Flights (LOTY, the default view)

**Featured flight, y 18..95** (`featured_panel`, `kPanelTop` / `kPanelHeight`) — the first row
of `sortRowsForDisplay()`, i.e. the nearest aircraft in the air. No frame: text and chips sit
on black, left-aligned.
- **Identity line** (y +6): callsign, airline, aircraft type, phase icon — `fitSummaryIdentity()`
  (`aircraft_summary`, pure, tested). The phase icon is pinned just inside the right edge, so it
  can never be pushed off-screen. The callsign is never cut; the airline is cut (`...`) to fill
  what's left between callsign + type and the icon; the type only if it alone doesn't fit.
- **Route line** (y +28, `LCARS_FONT_BODY`, sky): cities, not codes —
  "Antalya (TR) -> Warszawa (PL)" (`formatRowRouteCities()`; drops the country codes, then
  shortens both names, if it doesn't fit).
- **Stat chips** (y +50, 22 px tall, navy `THEME_PILL_FILL`, sky text,
  `featured_panel::featuredPillTexts()`): in the air altitude (m) / speed (km/h) / distance
  (km). **On the ground** one double-width `NA ZIEMI` / `ON GROUND` chip + distance. A missing
  value shows `--`, never a fake `0`. The distance chip reads e.g. "SE 6km" (`compassPoint()`:
  N/NE/E/SE/S/SW/W/NW) next to a "where to look" arrow with a stem (`ArrowShape`, bearing from
  home, screen top = north).
- No aircraft: the empty-state text, centred in the panel.

**Flights table, y 98..215** (`table_view`).
- **Column header** at y 98..113 (`kColHeaderY`, `kTableHeaderHeightPx` 15): lilac
  `THEME_TABLE_HEADER` bar, black labels in `LCARS_FONT_SMALL`, each drawn at its own column's x
  plus `kTableHeaderLabelPadPx` (4 px), a 1 px black `|` at every column boundary.
  Labels: PL `LOT`, `ODL`, `SKĄD`, `DOKĄD`, `TYP`, `FAZA` / EN `FLIGHT`, `DIST`, `FROM`, `TO`,
  `TYPE`, `PHASE`. Every label in both languages must fit its column (native-tested).
- **Columns** — one table, `kColumns` in `table_view.cpp`, shared by header and rows
  (`tableColumns()`; tests check they don't overlap and fit 320 px):

  | Column | x | width | Content |
  |---|---|---|---|
  | LOT / FLIGHT | 2 | 70 | callsign (8 chars) |
  | ODL / DIST | 72 | 46 | distance from home, "8km" |
  | SKĄD / FROM | 118 | 34 | origin IATA code |
  | DOKĄD / TO | 152 | 34 | destination IATA code |
  | TYP / TYPE | 186 | 102 | friendly aircraft type, ≤ 12 chars ("737 MAX 8") |
  | FAZA / PHASE | 288 | 30 | phase icon |

- **Rows:** 5 per page (`kTableRowsPerPage`), first at y 116, 17 px step (`kFirstRowY`,
  `kRowStep`; rows at 116, 133, 150, 167, 184, the last ends at 201 — a `static_assert` checks
  it stays above 215). Font `LCARS_FONT_TABLE` (efont 14 px, 7 px per character), 1 px smaller
  than the header strip. Text alternates white / sky (`tableRowTextColor()`); ground rows are
  muted lavender (`tableRowColor(i, onGround)`).
- Every cell is cut to its column (`fitToWidth()`, UTF-8 safe; `.` / `...` when there's room,
  hard cut otherwise) with 2 px padding (`kTableCellPadPx`). Callsign and phase icon always fit.
- **Placeholders:** `...` = still loading (the per-poll lookup budget hasn't reached it —
  `AircraftRow::info_pending` / `route_pending` / `*_airport_pending`), `--` = looked up,
  nothing known. Airport codes: IATA from hexdb.io's airport record, the ICAO code when hexdb.io
  has no record (`rowAirportCode()`).
- **Order** (`sortRowsForDisplay()`): aircraft in the air, nearest first; then aircraft on the
  ground, nearest first; then rows without a position. The featured panel (and Screen 3's
  summary) therefore shows the nearest aircraft you can see in the sky, never a parked jet; the
  table lists the rest in the same order, without the featured one. The lookup budget follows
  this order too.
- **On the ground** is inferred (`opensky_client::inferOnGround()`, folded into
  `Aircraft::on_ground`): OpenSky's flag, OR speed below `kTaxiSpeedMps` (15 m/s), OR neither
  altitude nor speed reported. Null altitude / speed are tracked (`has_altitude` /
  `has_velocity`) and shown as `--`. Ground rows get the top-view plane in FAZA.
- **Paging:** more than 5 aircraft → tap the first data row to page back, the row-tall strip
  just above the tab bar to page forward (`main.cpp`, zones one `tableRowHeightPx()` tall).
  Paging swaps which 5 are shown; it never scrolls. "1/2" (`tablePageLabel()`) right-aligned in the TYP column header when
  there is more than one page.

## Screen 2 — Flight (NOW / TERAZ)

One aircraft, one number that matters: when will it be overhead. In code it's `kScreenFlight` /
`flight_screen`; on screen it's called `TERAZ` / `NOW`.

- **Which aircraft** (`pickNowAircraft()`, poll_pipeline puts it in `DataSnapshot` as
  `nowRowIndex`): every aircraft's CPA is predicted (`cpa_predictor`, including
  `CpaPrediction::miss_distance_km`, how far from home the closest point is); shown is the
  airborne one whose CPA passes within `kOverheadRadiusKm` (5 km) and comes soonest (not more
  than 10 s in the past). Without such a candidate: the nearest airborne aircraft, no countdown.
- **Identity panel, y 18..88** (`kIdentityTop`, `kIdentityHeight` 70), no frame, three lines:
  identity (`fitSummaryIdentity()`, same rules as Screen 1), city route
  (`formatRowRouteCities()`), and a stats line — a "where to look" arrow (`directionArrow()`)
  plus `nowStatsText()` ("3km  236m  266km/h" / "6km  NA ZIEMI").
- **Countdown zone, y 92..186** (`kCountdownTop`, `kCountdownHeight`), centred, drawn into an
  8-bit `LGFX_Sprite` (~30 KB, reserved at boot by `flightScreenReserveSprite()`; drawn directly
  if the allocation failed). `computeCountdownDisplay(cpa, lang, anyAircraft, health)` (pure)
  picks the mode:

  | t_cpa (s) | Shows | Colour | Status strip |
  |---|---|---|---|
  | > 60 | `~4` + `MIN` (`LCARS_FONT_HEADING`, smaller — it's an estimate) | lilac | `SZACOWANY CZAS` / `ESTIMATED TIME` |
  | 10 < t ≤ 60 | `42` + `s` (`fonts::Font8`, ~75 px digits) | sky | `ZBLIŻA SIĘ` / `APPROACHING` |
  | 0 ≤ t ≤ 10 | seconds | `LCARS_YELLOW` | `NAD TOBĄ` / `OVERHEAD` |
  | -10 ≤ t < 0 | seconds, negative | peach | `MINĄŁ` / `PASSED` |
  | no prediction, or t < -10 | `--` | white | `NIE PRZELECI NAD TOBĄ` / `NOT PASSING OVERHEAD` if there are aircraft, else the empty-state text |

  While the CPA is still ahead (t > 0) a small muted caption `NAD TOBĄ ZA` / `OVERHEAD IN`
  (`CountdownDisplay::caption`) sits above the digits; the digits are centred below it.
- **Status strip, y 190..215** (`kStatusTop`, `kStatusHeight` 25), centred label from the table.
- **Ticking:** the countdown ticks every second between polls — the UI (`main.cpp`,
  `currentCpa()`) extrapolates the poll's prediction to now with `extrapolateCpa()` and repaints
  only the countdown zone and the status strip; each new poll re-syncs it. Once t drops below
  -10 the next poll simply picks again.
- Speeds everywhere are km/h (OpenSky's m/s × 3.6).

## Screen 3 — Radar

Left: radar plot; right: summary of the featured (nearest airborne) aircraft. No frame, no
divider. Constants in `radar_view.cpp`, geometry in `radar_geometry`.

**Radar, x 0..190, y 18..215.** Centre (95, 117), radius 85 px (`kRadarZoneW`, `kRadarMargin`).
- 3 rings at thirds of the radius (28 / 57 / 85 px), muted lavender, each labelled with its
  distance in km (`computeRingDistances()`: max/3, 2·max/3, max) in `LCARS_FONT_SMALL` at
  45° up-right on the ring (`ringLabelAnchor()`), where blips rarely land. An "N" above the
  outer ring (north-up plot).
- Home: a small `LCARS_YELLOW` crosshair at the centre.
- Airborne aircraft: sky blips (radius 4) with a short **heading tick** along their track
  (`headingTickEnd()`, 11 px). Beyond the range (`polarToScreen()` clamps them to the edge):
  smaller (radius 3), muted. **Ground traffic:** a small muted square, no tick, so the apron
  next to home doesn't become one blob. The aircraft shown in the summary panel
  (`rows.front()`) has a white ring.
- Static plot — no sweep animation.

**Summary panel, x 200..320, text from x 208, y 18..215**, five rows: callsign (heading font,
y 58), airline (y 92), origin city (y 118), "-> destination city" (y 138), and
`radarSummaryStats()` (y 168: "SE 5km  61m" / "S 4km  NA ZIEMI"). Every row is cut to the
panel width. No aircraft: the radar still draws its rings and home, the panel shows the short
empty-state text ("BRAK LOTU").

## Screen navigation

- **Order:** Flights (0) → Flight (1) → Radar (2) → wraps to Flights. `screen_nav`: current
  index, `nextScreen()` / `prevScreen()` with wraparound, `set()`.
- **Screens are switched only by the tab bar.** Tapping a segment jumps to that screen. There
  are no edge-swipe zones; taps above the tab bar belong to the active screen (Screen 1's table
  paging) or to the header's "!" (`alertBadgeHit()`, checked first).
- **Touch calibration** (`include/touch_calibration.h`, handed to LovyanGFX's
  `Touch_XPT2046`): in LovyanGFX raw terms **raw X is the vertical axis** (~340 at the bottom
  .. ~3900 at the top) and **raw Y the horizontal one, running right-to-left** (~3900 at the
  left .. ~200 at the right — passed as `y_min > y_max`); with `offset_rotation = 0` and
  `setRotation(1)` LovyanGFX swaps the axes and flips the vertical. `touch_input::mapRawTouch()`
  is a pure copy of that conversion, so native tests check the constants against raw taps
  logged on the board (also the old library's samples: X_L = y_old, Y_L = 4095 − x_old). Each
  tap logs `[touch] raw=(x,y) -> screen=(x,y) nav=<action>` — use it to re-tune if taps near an
  edge feel off (corners not measured yet). LovyanGFX's `calibrateTouch()` exists if a proper
  4-corner calibration is ever wanted.
- **Persistence:** the screen index is saved as `last_screen` (0/1/2) and restored on boot.
  Whether a change is written depends on its cause (`ScreenChangeCause`,
  `shouldPersistScreenChange()`): `Touch` always, `AutoCycle` at most every 10 min
  (`kScreenPersistMinIntervalMs`), `Screenshot` never (a `/screenshot.bmp?screen=N` switch is
  only for the capture; the previous screen comes back afterwards).
- **Any screen change resets the auto-cycle timer** — manual or automatic — so a tap is never
  undone by an auto-switch a moment later.

## Auto screen cycling

Off by default. Settings (General tab): `auto_cycle_enabled` (default false),
`auto_cycle_interval_s` (default 15).

- A `millis()` timer counts from the last screen change; at the interval the UI advances with
  the same `nextScreen()` as manual navigation and restarts the timer.
- **Don't interrupt an imminent pass on NOW:** `shouldDeferAutoSwitch(currentScreen, cpaFound,
  tCpaSeconds)` (`screen_nav`, pure) holds the switch while
  `currentScreen == FLIGHT && cpaFound && tCpaSeconds < 6 && tCpaSeconds > -5`
  (`kAutoSwitchHoldBeforeS`, `kAutoSwitchHoldAfterS` — fixed, not settings). The timer doesn't
  restart while deferred; the switch happens the moment the hold clears. A new aircraft that
  becomes imminent right then simply holds again — expected.
- Only with a home location set, and not while the screen is dark at night (nobody sees it).
  The alert screen and the service screens (OTA, update question) take over the display and
  pause it; closing the alert screen restarts the timer.

## Design language — LCARS-inspired

An **LCARS-inspired** look (Star Trek console): bold colour blocks, pills, rounded tabs — built
from LovyanGFX primitives, original execution, no copied assets or trademarked typeface.

- **Colour scheme "variant A" — warm chrome, cool content.** Navigation (header + tabs) and
  data never share a colour family. View modules use the **role** constants (`THEME_*` in
  `lcars_theme.h`), not palette names, so a re-colour is one edit:

  | Role | Colour | RGB565 |
  |---|---|---|
  | `THEME_CHROME_ACCENT` — active tab, tab-bar rule | peach `#FF9966` | `0xFCCC` |
  | `THEME_CHROME_TEXT` — header date | light peach `#FFCC99` | `0xFE73` |
  | `THEME_CHROME_INACTIVE` — inactive tab outline + label | lavender `#9977AA` | `0x9BB5` |
  | `THEME_CHROME_DIM` — header app name | lavender | `0x9BB5` |
  | `THEME_CHROME_RECESSED` — inactive tab fill | dark grey | `0x2124` |
  | `THEME_ON_FILL` — text on any filled bar / button | black | `0x0000` |
  | `THEME_ALERT` — red health dot, error "!" | red `#FF3333` | `0xF986` |
  | `THEME_WARNING` — warning "!" (= `LCARS_ORANGE`) | orange `#FF8000` | `0xFC00` |
  | `THEME_CONTENT_PRIMARY` — callsigns, odd table rows | white | `0xFFFF` |
  | `THEME_CONTENT_SECONDARY` — airline / type / route, even rows, radar blips | sky `#99CCFF` | `0x9E7F` |
  | `THEME_CONTENT_MUTED` — radar rings / labels, clamped blips, ground rows | lavender | `0x9BB5` |
  | `THEME_PILL_FILL` — stat chips (sky text) | navy `#102040` | `0x1108` |
  | `THEME_TABLE_HEADER` — table column-header bar | lilac `#CC99CC` | `0xCCD9` |

  Also: `LCARS_YELLOW` (`0xFFE0`) for the radar home crosshair, the "imminent" countdown and the
  rate-limited health dot; phase icons coloured by `phaseColor()` (takeoff green `#66CC66`
  `0x666C`, landing peach, overflight sky, ground / none muted); background `LCARS_BLACK`.
  Standard RGB565 values — the ILI9341 runs in 16-bit colour.
- **Fonts** (`lcars_theme`): `LCARS_FONT_BODY` efont 16 px (8 px per char), `LCARS_FONT_TABLE`
  efont 14 px (7 px), `LCARS_FONT_SMALL` efont 10 px (5 px) — subsets with Polish letters, see
  CLAUDE.md "Language"; `LCARS_FONT_CLOCK` DejaVu12 (header date / time);
  `LCARS_FONT_HEADING` Font4 and `fonts::Font8` (countdown digits) never show Polish text.
- **Orientation:** landscape, `setRotation(1)`. If the board is ever mounted the other way up,
  `setRotation(3)` — and the touch mapping must follow.
- **No scrolling, ever** — see the top of this file.
- **No frames.** Text and chips sit directly on black; there are no elbow frames or dividers.
  Don't add them back without being asked.
- Shared drawing helpers live in `lcars_theme` (pills, tabs, phase icons, direction arrow);
  anything that's the aircraft identity block lives in `aircraft_summary`. Views don't define
  their own colours or shapes.

## Flight phase — takeoff / landing / overflight

`flight_phase` (pure). Inputs: distance from home, vertical rate, `on_ground`, and two
settings: `near_airport_km` (default 15 km, a guess, tune after watching real traffic) and
`climb_rate_threshold_mps` (default 1.5 m/s). `Phase`: `NONE`, `TAKEOFF`, `LANDING`,
`OVERFLIGHT`.

- `on_ground` → `NONE` (no phase while taxiing).
- distance > `near_airport_km` → `NONE` — not near the airport, no icon.
- else vertical rate > threshold → `TAKEOFF`; < −threshold → `LANDING`; else `OVERFLIGHT`
  (near the airport but level — transiting).

**Icons** — 16×12 px pixel-art planes (`lcars_theme`: `phaseIconKind(phase, onGround)`,
`phaseIconRows()` = char grids `# d k g .`, `drawPhaseIcon()` draws them as fastHLine runs):

| Kind | Picture | Colour |
|---|---|---|
| takeoff | side view climbing ~18°, runway strip under the tail | green |
| landing | side view descending, runway under the nose | peach |
| overflight | side view, level | sky |
| ground (any phase) | top view | muted lavender |
| none | nothing | — |

Used in the table's FAZA column and at the right end of the identity line (all screens). The
README shows them from `docs/icons/phase_*.png` — regenerate those if the grids change.

## Flight ETA — closest point of approach

"How long until it's overhead": `cpa_predictor` (pure), straight-line extrapolation at the
current speed and track — fine for the near-term answer the NOW screen needs; real aircraft
turn, so it degrades further out. Turn-aware prediction is out of scope.

1. Home and aircraft to local flat x/y in km (equirectangular around the home latitude):
   `dx = (lon_ac − lon_home) · 111.32 · cos(lat_home)`, `dy = (lat_ac − lat_home) · 111.32`.
2. Velocity from speed (m/s → km/s) and track (0 = north, clockwise):
   `vx = v · sin(track)`, `vy = v · cos(track)`.
3. `t_cpa = −(dx·vx + dy·vy) / (vx² + vy²)`; `miss_distance_km` = distance at that time.

- `vx² + vy²` ≈ 0 (stationary) → `found = false`, no division by zero.
- **Signed result:** positive = CPA ahead, negative = it happened that many seconds ago.
- **Between polls** the UI extrapolates the poll's prediction to now (`extrapolateCpa()`), so
  the countdown ticks every second instead of jumping every `poll_interval`; each poll re-syncs.

## Time — the header clock

- **NTP:** after WiFi connects, `net_task` calls `configTzTime(TZ_WARSAW, "pool.ntp.org",
  "time.nist.gov")` with `TZ_WARSAW = "CET-1CEST,M3.5.0,M10.5.0/3"` (POSIX rule for
  Europe/Warsaw, handles CEST). If the clock is ever off by an hour, check the EU DST rule.
- Until the first sync `isTimeSynced()` is false and the header shows `--.--.----` / `--:--`,
  not the ESP32's 1970 date.
- `time_sync` formatting is pure and takes the `time_t` as a parameter (testable regardless of
  when the test runs): `formatDate()` → `"DD.MM.YYYY"` (zero-padded, e.g. `07.03.2026`),
  `formatTime()` → `"HH:MM"` (24 h), `minuteOf()` (changes exactly when `formatTime()` does).
- The header has no WiFi signal indicator.
