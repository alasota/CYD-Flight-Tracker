#include "display/core/text/font_set.h"

namespace {
// Beyond printable ASCII: ą ć ę ł ń ó ś ź ż Ą Ć Ę Ł Ń Ó Ś Ź Ż and ° ± × · – — … →.
// Keep in step with tools/subset_efont.py's CODEPOINTS (test_fonts checks the fonts).
const uint16_t kFontExtraCodepoints[] = {
    0x0105, 0x0107, 0x0119, 0x0142, 0x0144, 0x00F3, 0x015B, 0x017A, 0x017C,
    0x0104, 0x0106, 0x0118, 0x0141, 0x0143, 0x00D3, 0x015A, 0x0179, 0x017B,
    0x00B0, 0x00B1, 0x00D7, 0x00B7, 0x2013, 0x2014, 0x2026, 0x2192};
}  // namespace

bool isFontCodepoint(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  for (uint16_t extra : kFontExtraCodepoints) {
    if (cp == extra) return true;
  }
  return false;
}

bool isFontSafeText(const std::string &text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp;
    size_t len;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else {
      return false;  // 4-byte sequences / stray continuation bytes: never in the fonts
    }
    if (i + len > text.size()) return false;
    for (size_t k = 1; k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(text[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!isFontCodepoint(cp)) return false;
    i += len;
  }
  return true;
}

bool fontHasGlyph(const uint8_t *font, uint16_t cp) {
  // Same search as LovyanGFX's U8g2font::getGlyph().
  const uint8_t *p = font + 23;
  if (cp <= 255) {
    if (cp >= 'a') {
      p += (font[19] << 8) | font[20];
    } else if (cp >= 'A') {
      p += (font[17] << 8) | font[18];
    }
    for (; p[1] != 0; p += p[1]) {
      if (p[0] == cp) return true;
    }
    return false;
  }
  p += (font[21] << 8) | font[22];
  const uint8_t *lut = p;
  uint16_t e;
  do {
    p += (lut[0] << 8) | lut[1];
    e = static_cast<uint16_t>((lut[2] << 8) | lut[3]);
    lut += 4;
  } while (e < cp);
  for (; (e = static_cast<uint16_t>((p[0] << 8) | p[1])) != 0; p += p[2]) {
    if (e == cp) return true;
  }
  return false;
}
