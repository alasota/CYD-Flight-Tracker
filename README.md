# CYD Sky Tracker 🛸

*"Space: the final frontier. Also, the airspace directly above the grill."*

I live near an airport in Warsaw, which means every barbecue with friends eventually hits the
same moment: someone tilts their head back, points a kielbasa-holding hand at the sky, and asks
*"co to za samolot?"* — what plane is that? Nobody knows. Someone opens Flightradar24 on their
phone, the grill smoke drifts into someone's eyes, and the argument about whose guess was
closest gets tabled indefinitely, honor unrestored.

This project exists to end that argument before it starts. A $15 ESP32 "Cheap Yellow Display"
sits by the grill and tells you, in real time: **which airline**, **which flight**, and
**which aircraft type** just went over your head — so bets about "I bet that's a 737" can
finally be settled with data instead of vibes, and everyone can get back to the kielbasa.

Powered by the [OpenSky Network](https://opensky-network.org/), dressed up like the bridge of a
starship that definitely does not belong to any copyright holder in particular.

No soldering. No radar dish. No red shirts were harmed in the making of this firmware.

## What is this thing

It polls live aircraft state vectors for a bounding box around your house (or your grill, same
thing), looks up the airline and aircraft type for anything worth arguing about, and shows it
all on a 2.8" TFT in a UI *inspired by* a certain 24th-century computer interface without being,
you know, *that* interface, because Paramount has lawyers and we have a hobby budget.

Built as a spiritual sibling of [micro-radar](https://github.com/AnthonySturdy/micro-radar) —
same idea (cheap board, live sky, OpenSky API), completely different outfit, and now an actual
answer to "what plane is that." Think of it as the away-team variant with better bragging rights.

## Features

- 📋 **Table view** — a sortable, scannable list of every aircraft in range: **airline, flight,
  aircraft type**, altitude, speed, distance. This is the bet-settling column, front and center.
  Closest aircraft gets highlighted, because it's the one you can actually go outside and squint at.
- 🎯 **Radar view** *(optional, secondary, still in space-dock)* — the classic circular sweep,
  just wearing a much cooler uniform.
- 🖖 **LCARS-inspired chrome** — black background, chunky color blocks, rounded "elbow" panels,
  pill buttons. An homage, not a photocopy. All shapes hand-drawn in TFT_eSPI, zero ripped assets.
- 📡 Lives entirely on the ESP32 — no server, no phone app, no subscription, no away mission
  required.
- 🔐 Speaks OpenSky's current OAuth2 `client_credentials` auth (their old Basic Auth was
  decommissioned — this firmware didn't get that memo late).
- 🧭 Works anonymously too, just with a smaller communications budget (see Credits, below —
  the API kind, not the "roll them" kind).

## Hardware

- **ESP32-2432S028R** a.k.a. the "Cheap Yellow Display" (CYD) — ESP32-WROOM-32 + 2.8" 320x240
  ILI9341 TFT + resistive touch, all on one board, for about the price of a fancy coffee.
- A USB-C cable that actually carries data (you'd be amazed how many don't).
- That's it. No dilithium crystals required.

> ⚠️ CYD boards come in a few flavors. This project assumes the classic ILI9341 +
> resistive-touch "R" variant. If your colors are inverted or touch does nothing, you may have
> the ST7789/capacitive "C" variant — check before assuming the firmware is broken.

## Software you'll need

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- A free account at [opensky-network.org](https://opensky-network.org/) if you want the good
  rate limits (4,000 credits/day instead of 400 — think of it as upgrading from a shuttlecraft
  to the actual ship)

## Getting it running

```bash
git clone <this-repo>
cd cyd-sky-tracker
pio run                 # build
pio run -t upload       # flash it, engage
pio device monitor -b 115200   # watch the logs scroll by dramatically
```

On first boot the board can't find a saved WiFi network, so it opens its own hotspot for setup.
Connect to it from your phone, hand over your WiFi credentials, and the board will reboot onto
your network like nothing happened.

Once it's on your network, open its local config page (mDNS hostname is in `CLAUDE.md`) and set:

- Home latitude/longitude (the center of your scan — i.e., where your desk is)
- Scan radius (bigger radius = more planes = more API credits burned per request — choose your
  engagement range wisely)
- Poll interval
- Your OpenSky `client_id` / `client_secret`, if you made an account

## How it works, briefly

```
WiFi/config portal → opensky_client (OAuth2 + /states/all)
                            │
                            ▼
              aircraft_lookup (icao24 → airline + type, cached)
                            │
                            ▼
              table_view / radar_view (shared LCARS chrome)
```

Networking and rendering are kept firmly separated, so nobody has to relive the trauma of
untangling API calls from `tft.drawPixel()` at 2am.

## Credits & attribution

- Architecture inspiration: [micro-radar](https://github.com/AnthonySturdy/micro-radar) by
  Anthony Sturdy — go look at the original, it's a lovely little desk radar and does the
  circular-plot thing properly.
- Flight data: [The OpenSky Network](https://opensky-network.org/) — a community-run,
  volunteer-fed ADS-B network. If you like this project, consider running a receiver and
  feeding them data; it's how the whole thing stays free.
- Visual style: *inspired by* a famous sci-fi computer interface, built from scratch with
  original shapes and an open font. Not affiliated with, endorsed by, or officially sanctioned
  by any studio that owns the actual thing. We just think black backgrounds and orange
  rectangles are extremely funky.

## Disclaimer

This project tells you about airplanes. It does not control airplanes, talk to airplanes, or
grant you any authority over airspace whatsoever. If a real plane is doing something alarming,
this device's suggested course of action is the same as everyone else's: call the appropriate
authorities, not your ESP32.

## License

MIT. Live long, and go touch some grass while you wait for the next flight to enter your bbox.
