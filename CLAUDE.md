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
- **Nice-to-have, not MVP**: hexdb.io also exposes a route lookup
  (`https://hexdb.io/api/v1/route/icao/{callsign}` → origin/destination airports). Interesting
  for later, not required to settle "what plane is that."

## Visualization concept — data table (primary) + radar (secondary)

Two screens, toggled by a touch tap on a persistent LCARS-style corner button. Both share the
same "Design language" chrome below so switching feels like changing a display mode on one
console, not jumping between two different apps.

### Screen 1 — Aircraft table (default view, prioritize this first)

- Scrollable/paged list, one row per aircraft, columns: **airline, flight (callsign), aircraft
  type, altitude, speed, distance from home**. This is the whole point of the project — get
  these columns right before worrying about anything else. Truncate/abbreviate rather than
  shrinking the font past readability on a 320x240 screen.
- Airline and aircraft type come from `aircraft_lookup` (see "Aircraft identification" above);
  if a lookup hasn't resolved yet or the aircraft isn't in the database, show the row anyway
  with airline/type as "—" rather than hiding it or blocking the table.
- Sort by distance ascending (closest aircraft on top) — recompute sort on every poll.
- Row count per page depends on the row height chosen for legibility; page/scroll via touch if
  more aircraft are in range than fit on screen. Don't cram to fit everything at the cost of
  readability — paging is fine.
- Highlight the closest row (e.g. brighter accent color / bolder) since it's the one most likely
  visible outside.
- This is the priority screen — get it solid before spending time on the radar screen.

### Screen 2 — Radar (optional, secondary, build after the table works)

- Classic circular sweep plot, home at center, aircraft as blips positioned by bearing +
  distance, but re-skinned entirely in the LCARS palette/shapes from "Design language" —
  this is what makes it different from micro-radar's radar, not the underlying polar-plot math.
- Sweep line animation optional and low priority; a static "last refresh" plot is fine for v1.
- Tapping a blip can jump to that aircraft's row on the table screen (nice-to-have, not MVP).

## Design language — LCARS-inspired (Star Trek console aesthetic)

Build an **LCARS-inspired** look, not a reproduction of the exact copyrighted LCARS design
system — no ripped bitmap assets, no attempt to recreate the specific trademarked typeface.
The spirit (bold color blocks, rounded "elbow" panels, chunky sidebar) is what we're after; the
execution should be original, built from LovyanGFX primitives.

- **Palette**: black background; a small set of saturated accent colors (e.g. amber/orange,
  blue-violet, salmon/rose, pale blue) used as solid blocks — pick 3–4 and reuse them
  consistently across both screens rather than introducing new colors per element.
- **Shapes**: rounded-rectangle panels, an "elbow" sidebar (a vertical bar that curves into a
  horizontal header) as a persistent frame element, pill-shaped buttons/labels for headers and
  the view-toggle control.
- **Typography**: LovyanGFX's built-in fonts sized for legibility at this resolution — bold,
  chunky, minimal decoration. Numeric/status readouts in a monospace-feeling font if available.
- **Chrome stays constant across both screens**: same sidebar, same header treatment, same
  corner toggle button — only the content area (table vs. radar) changes.
- Keep this as its own module (see `lcars_theme` below) so colors/shapes are defined once and
  reused, not re-picked per screen.

## Code conventions

Arduino framework for ESP32 (not raw ESP-IDF). One `.h`/`.cpp` pair per module in `src/`,
each with a single responsibility:

- `wifi_manager` — WiFi connect + captive-portal fallback (WiFiManager library), no display or
  networking-to-OpenSky logic here.
- `config_portal` — local web page (WebServer or ESPAsyncWebServer) for lat/lon, radius, OpenSky
  client_id/secret, poll interval. Reachable via mDNS (e.g. `cyd-sky.local`), same pattern as
  micro-radar's `microradar.local`.
- `opensky_client` — token fetch/refresh + `/states/all` polling. Returns a plain struct/array
  of aircraft. **No TFT/drawing code in this module** — must be swappable/testable independently
  of the visualization.
- `aircraft_lookup` — `icao24` → airline/aircraft-type enrichment via hexdb.io, with an
  in-memory (ideally NVS-backed) cache so a given `icao24` is only ever looked up once. No TFT
  code, no OpenSky polling logic — purely a lookup + cache layer sitting between
  `opensky_client`'s output and the view modules.
- `lcars_theme` — shared color palette, panel/elbow/pill drawing helpers, fonts. Both view
  modules call into this rather than defining their own colors/shapes.
- `table_view` — draws Screen 1 (aircraft table). Takes the aircraft array + home position as
  input, uses `lcars_theme` for chrome. No networking code.
- `radar_view` — draws Screen 2 (LCARS-skinned radar). Same input contract as `table_view`, same
  theme module. No networking code.
- `config_store` — Preferences/NVS read/write for all persisted settings, including which view
  (table/radar) was last active.

Other rules:
- Never hardcode WiFi or OpenSky secrets in source files.
- Prefer `millis()`-based non-blocking timing over `delay()` so touch input and rendering stay
  responsive between polls.
- Keep `opensky_client` fully decoupled from `table_view`/`radar_view` — that boundary is what
  lets the two screens share one data source without duplicating polling logic.
- `table_view` and `radar_view` should not duplicate color/shape definitions — anything visual
  that appears on both belongs in `lcars_theme`.

## Testing — logic gets covered first, not "later"

Test-first is the default here, not a cleanup pass bolted on at the end:

- Before implementing any non-trivial piece of logic — bbox math, distance/bearing calculations,
  sorting aircraft by distance, altitude bucketing, token-expiry timing, cache hit/miss behavior
  in `aircraft_lookup`, credit-cost calculation, JSON parsing/mapping, etc. — write the test for
  it in `test/` first, or at the very least alongside it, then implement until it passes. "Add
  tests later" is not a real plan on a solo hobby project — later doesn't happen, so don't rely
  on it.
- This is exactly why the module boundaries in "Code conventions" above are non-negotiable:
  `opensky_client`, `aircraft_lookup`, `table_view`, `radar_view`, and `lcars_theme` all keep
  their actual logic separate from LovyanGFX / WiFiClientSecure / HTTPClient / XPT2046 calls
  specifically so that logic can run in PlatformIO's `native` environment — no ESP32, no
  display, no network required, just `pio test -e native`.
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
