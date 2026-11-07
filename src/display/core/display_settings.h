// display_settings — the display's settings objects (spec 12 §7): its
// language, the screen it restores on boot, and auto screen cycling. Settings
// (admin/core/config_store.h) holds them as `display` and `auto_cycle`; the
// UI reads them from NetState (backlight's own object is in backlight.h).
//
// Header-only, no logic: the clamps live in config_store (the aggregate keeps
// its invariants). ⚠️ env:cyd is gnu++11 — with default member initializers
// these structs aren't aggregates there: assign members, never `X{…}`.
#pragma once

#include <cstdint>

#include "display/core/i18n.h"  // Language

struct DisplaySettings {
  // On-screen language (CLAUDE.md "Language"), chosen on the config page.
  // Stored in NVS as its numeric index; an out-of-range stored value is
  // reset to kDefaultLanguage by sanitizeConfig().
  Language language = kDefaultLanguage;
  // Which of the three screens was showing at last power-down (0/1/2),
  // restored on boot (see docs/screens.md "Screen navigation").
  int last_screen = 0;
};

// Auto screen cycling (docs/screens.md "Auto screen cycling"). Off by default;
// when enabled, the active screen advances every interval_s seconds (the
// Screen 2 "imminent overhead" deferral lives in
// screen_nav::shouldDeferAutoSwitch()). Both are set on the config page.
struct AutoCycleSettings {
  bool enabled = false;
  uint32_t interval_s = 15;
};
