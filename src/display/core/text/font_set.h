// font_set — the characters the display fonts carry, defined once
// (CLAUDE.md "Language — Polish / English"): printable ASCII, the 18 Polish
// letters and ° ± × · – — … →. tools/subset_efont.py cuts the efont subsets
// in theme/fonts/ to exactly this set; every on-screen string is checked
// against it.
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <cstdint>
#include <string>

// True if every character of `text` is printable ASCII, one of the 18
// Polish letters (ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ) or one of ° ± × · – — … →, as
// well-formed UTF-8 — the set the display fonts carry
// (theme/fonts/lcars_efont.c holds nothing else). On-screen strings (i18n
// table, city names) are tested against it.
bool isFontSafeText(const std::string &text);

// The same set, per Unicode code point.
bool isFontCodepoint(uint32_t cp);

// True if the U8g2-format `font` has a glyph for `cp`, found the way
// LovyanGFX's U8g2font::getGlyph() looks it up. Pure — test_fonts uses it
// to check theme/fonts/lcars_efont.c against isFontCodepoint().
bool fontHasGlyph(const uint8_t *font, uint16_t cp);
