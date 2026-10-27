/* The display's three body fonts: efont 16 / 14 / 10 px (U8g2 format), cut
 * down by tools/subset_efont.py to the characters isFontSafeText() allows —
 * printable ASCII, the 18 Polish letters and ° ± × · – — … →. Same pixels
 * and metrics as LovyanGFX's fonts::efontJA_16 / _14 / _10, ~4 KB instead
 * of ~1.05 MB of flash. Plain const arrays: on the ESP32 they stay in flash.
 * Pure data — the native test test_fonts reads them too. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t lcars_efont_16[];
extern const uint8_t lcars_efont_14[];
extern const uint8_t lcars_efont_10[];

#ifdef __cplusplus
}
#endif
