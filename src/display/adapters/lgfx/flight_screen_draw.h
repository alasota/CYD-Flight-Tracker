// flight_screen_draw — the LovyanGFX half of flight_screen: Screen 2's
// identity panel, countdown zone and status strip (docs/screens.md "Screen 2"),
// and the 8-bit sprite the 1 Hz countdown is pushed through. Hardware adapter,
// not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/flight_screen.h"

// Draws all three zones for a `screenWidth`-px display. `hasNearest` ==
// false (no aircraft in range) leaves the identity panel empty and forces
// the countdown/status into their empty state regardless of `cpa`.
void drawFlightScreen(LGFX &gfx, const TrackedFlight &nearest, bool hasNearest,
                      const CpaPrediction &cpa, int16_t screenWidth, FeedHealth health);

// Repaints only the countdown zone + status strip — the 1 Hz
// tick path (CLAUDE.md "Redraw policy"), leaving the identity panel,
// header and bottom nav untouched. Renders into an off-screen 8-bit
// LGFX_Sprite and pushes it in one go so the digits don't flicker; if the
// sprite can't be allocated (low heap) it falls back to drawing straight
// to `gfx`. drawFlightScreen() calls this for its lower two zones.
void drawFlightCountdown(LGFX &gfx, bool hasNearest, const CpaPrediction &cpa,
                         int16_t screenWidth, FeedHealth health);

// Allocates the countdown sprite now (setup(), before WiFi / net_task) so it
// doesn't later split the largest free heap block. Safe to call again.
void flightScreenReserveSprite(int16_t screenWidth);
