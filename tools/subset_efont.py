#!/usr/bin/env python3
"""Cut the three efont fonts the display uses down to the characters it needs.

LovyanGFX's efontJA_* fonts carry ~10,800 glyphs each (kanji, kana, ...) and
take ~1.05 MB of flash together. The display only ever draws printable ASCII,
the 18 Polish letters and a few symbols (see isFontSafeText() in
lcars_theme). This script copies exactly those glyphs, byte for byte, out of
LovyanGFX's U8g2-format arrays into
src/display/domain/theme/fonts/lcars_efont.c — so every pixel
and every metric (the 23-byte header is kept, only glyph count and section
offsets change) is identical to the original fonts.

Usage (from the repo root, after `pio run` has fetched LovyanGFX):
    python tools/subset_efont.py

Change CODEPOINTS together with lcars_theme's kFontExtraCodepoints; the
native test fonts_test.cpp (test_display) checks that every character isFontSafeText() accepts
has a glyph in every subset font.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(REPO, ".pio", "libdeps", "cyd", "LovyanGFX", "src", "lgfx", "Fonts",
                      "efont", "lgfx_efont_ja.c")
COPYRIGHT = os.path.join(os.path.dirname(SOURCE), "COPYRIGHT.txt")
OUT_C = os.path.join(REPO, "src", "display", "domain", "theme", "fonts", "lcars_efont.c")
# The generated .c includes its header by the full path from src/ (spec 12, -I src).
INCLUDE_LINE = '#include "display/domain/theme/fonts/lcars_efont.h"'

# (array in LovyanGFX, array written here, role)
FONTS = [
    ("lgfx_efont_ja_16", "lcars_efont_16", "LCARS_FONT_BODY - body text, 16 px"),
    ("lgfx_efont_ja_14", "lcars_efont_14", "LCARS_FONT_TABLE - table rows, 14 px"),
    ("lgfx_efont_ja_10", "lcars_efont_10", "LCARS_FONT_SMALL - small labels, 10 px"),
]

POLISH = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
SYMBOLS = "°±×·–—…→"  # degree, plus-minus, times, middle dot, en/em dash, ellipsis, arrow
CODEPOINTS = sorted(set(range(0x20, 0x7F)) | {ord(c) for c in POLISH + SYMBOLS})

HEADER_SIZE = 23


def r16(b, i):
    return (b[i] << 8) | b[i + 1]


def decode_c_string(lit):
    """Bytes of one C string literal body (no quotes)."""
    out = bytearray()
    i = 0
    simple = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11,
              "\\": 92, "'": 39, '"': 34, "?": 63}
    while i < len(lit):
        c = lit[i]
        if c != "\\":
            out += c.encode("latin-1")
            i += 1
            continue
        n = lit[i + 1]
        if n in "01234567":
            j = i + 1
            while j < len(lit) and j < i + 4 and lit[j] in "01234567":
                j += 1
            out.append(int(lit[i + 1:j], 8) & 0xFF)
            i = j
        elif n == "x":
            j = i + 2
            while j < len(lit) and lit[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(lit[i + 2:j], 16) & 0xFF)
            i = j
        else:
            out.append(simple[n])
            i += 2
    return bytes(out)


def read_array(text, name):
    m = re.search(r"const uint8_t " + name + r"\[(\d+)\]\s*=", text)
    if not m:
        sys.exit("array %s not found in %s" % (name, SOURCE))
    size = int(m.group(1))
    # Concatenated string literals up to the ';' outside any literal (the
    # data itself contains ';' characters).
    literal = re.compile(r'\s*"((?:[^"\\]|\\.)*)"')
    pos, pieces = m.end(), []
    while True:
        lm = literal.match(text, pos)
        if not lm:
            break
        pieces.append(decode_c_string(lm.group(1)))
        pos = lm.end()
    if text[pos:].lstrip()[:1] != ";":
        sys.exit("%s: unexpected text after the data" % name)
    data = b"".join(pieces)
    # The declared size includes the literal's terminating NUL.
    if len(data) + 1 != size:
        sys.exit("%s: decoded %d bytes, declared %d" % (name, len(data), size))
    return data + b"\0"


def glyphs(font):
    """{encoding: raw glyph record (encoding + size + bitmap)} of a U8g2 font."""
    out = {}
    p = HEADER_SIZE
    while font[p + 1] != 0:  # 8-bit section: [enc][size][data...]
        out[font[p]] = font[p:p + font[p + 1]]
        p += font[p + 1]
    lut = HEADER_SIZE + r16(font, 21)
    p = lut + r16(font, lut)  # first lookup entry: offset from the table to the glyphs
    while r16(font, p) != 0:  # unicode section: [enc hi][enc lo][size][data...]
        out[r16(font, p)] = font[p:p + font[p + 2]]
        p += font[p + 2]
    return out


def lookup(font, enc):
    """Glyph bitmap the way LovyanGFX's U8g2font::getGlyph() finds it."""
    p = HEADER_SIZE
    if enc <= 255:
        if enc >= ord("a"):
            p += r16(font, 19)
        elif enc >= ord("A"):
            p += r16(font, 17)
        while font[p + 1]:
            if font[p] == enc:
                return font[p + 2:p + font[p + 1]]
            p += font[p + 1]
        return None
    p += r16(font, 21)
    lut = p
    while True:
        p += r16(font, lut)
        e = r16(font, lut + 2)
        lut += 4
        if e >= enc:
            break
    while True:
        e = r16(font, p)
        if e == 0:
            return None
        if e == enc:
            return font[p + 3:p + font[p + 2]]
        p += font[p + 2]


def subset(font, keep):
    src = glyphs(font)
    missing = [hex(c) for c in keep if c not in src]
    if missing:
        sys.exit("glyphs missing from the source font: " + ", ".join(missing))

    small = [c for c in keep if c <= 255]
    large = [c for c in keep if c > 255]

    body = bytearray()
    pos_a = pos_A = None
    for c in small:
        if pos_A is None and c >= ord("A"):
            pos_A = len(body)
        if pos_a is None and c >= ord("a"):
            pos_a = len(body)
        body += src[c]
    body += b"\0\0"  # end of the 8-bit section (size byte 0)

    pos_unicode = len(body)
    body += bytes([0, 4, 0xFF, 0xFF])  # one lookup entry: glyphs right after it, covers all
    for c in large:
        body += src[c]
    body += b"\0\0"  # end of the unicode section (encoding 0)

    header = bytearray(font[:HEADER_SIZE])
    header[0] = len(keep) & 0xFF  # glyph_cnt (informational)
    for i, v in ((17, pos_A), (19, pos_a), (21, pos_unicode)):
        header[i], header[i + 1] = v >> 8, v & 0xFF
    out = bytes(header + body)

    # Self-check: every kept glyph found, by LovyanGFX's own search, with the
    # same bytes as in the original font.
    for c in keep:
        if lookup(out, c) != lookup(font, c) or lookup(out, c) is None:
            sys.exit("self-check failed for U+%04X" % c)
    return out


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    return "const uint8_t %s[%d] = {\n%s\n};\n" % (name, len(data), "\n".join(lines))


def main():
    if not os.path.exists(SOURCE):
        sys.exit("LovyanGFX not found at %s - run `pio run` first" % SOURCE)
    with open(SOURCE, encoding="latin-1") as f:
        text = f.read()
    with open(COPYRIGHT, encoding="utf-8") as f:
        copyright_text = f.read().strip()

    parts = []
    for src_name, out_name, role in FONTS:
        original = read_array(text, src_name)
        data = subset(original, CODEPOINTS)
        print("%-16s %7d B -> %5d B (%d glyphs)" % (src_name, len(original), len(data),
                                                     len(CODEPOINTS)))
        parts.append("/* %s, from LovyanGFX %s */\n%s" % (role, src_name, c_array(out_name, data)))

    chars = "".join(chr(c) for c in CODEPOINTS)
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* GENERATED by tools/subset_efont.py - do not edit by hand.\n *\n")
        f.write(" * Subsets of LovyanGFX's efontJA_16 / _14 / _10 (U8g2 format) holding only\n")
        f.write(" * the characters the display draws:\n *   %s\n" % chars)
        f.write(" * Glyph bitmaps and font metrics are copied unchanged.\n *\n")
        f.write(" * Font data: efont, " + "\n * ".join(copyright_text.splitlines()) + "\n */\n")
        f.write(INCLUDE_LINE + "\n\n")
        f.write("\n".join(parts))
    print("wrote", os.path.relpath(OUT_C, REPO))


if __name__ == "__main__":
    main()
