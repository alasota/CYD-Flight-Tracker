// fit_to_width — the one truncation helper: cuts a string to a pixel width
// with "..." so text never overflows its cell or panel (CLAUDE.md "Code
// conventions": no scrolling — paginate or truncate).
//
// Pure — the width comes from an injected TextMeasure — tested under
// `pio test -e native`.
#pragma once

#include <functional>
#include <string>

// Rendered pixel width of a string in whatever font is current. On the
// device this wraps gfx.textWidth() (lcars_theme's gfxTextMeasure()); tests
// pass a fixed-pitch fake.
using TextMeasure = std::function<int(const std::string &)>;

// `text` cut down so that measure(result) <= maxPx — the one truncation
// helper for every place a string must not spill out of its cell/panel
// (docs/screens.md "Screen 1": table cells, the identity line, Screen 3's
// airline row):
//   - already fits                -> returned unchanged.
//   - otherwise                   -> longest head + "..." that fits, with
//     trailing spaces trimmed off the head ("LOT..." not "LOT ...").
//     ASCII "...", not "…" — bitmap fonts only cover 0x20-0x7E.
//   - not even "X..." fits        -> longest plain head that fits.
//   - maxPx <= 0 / nothing fits   -> "".
std::string fitToWidth(const std::string &text, int maxPx, const TextMeasure &measure);
// (Cuts only on UTF-8 character boundaries — utf8IsCharBoundary() — so a
// multi-byte Polish letter is never split, see isFontSafeText().)
