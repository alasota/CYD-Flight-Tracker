// utf8 — character counting and character boundaries in UTF-8 text, for
// the layout maths with the efont fonts (CLAUDE.md "Language — Polish /
// English": a Polish letter is two bytes but one fixed advance).
//
// Pure — no Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <cstddef>
#include <string>

// Number of characters (UTF-8 code points) in `utf8`, not bytes — for
// layout maths with the efont fonts (fixed advance per ASCII character).
size_t utf8Length(const std::string &utf8);

// True if byte `i` of `s` starts a character (i.e. isn't a UTF-8
// continuation byte 10xxxxxx), or `i` is at / past the end — the only
// places a string may be cut (fitToWidth()).
bool utf8IsCharBoundary(const std::string &s, size_t i);
