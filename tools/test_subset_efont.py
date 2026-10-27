"""Tests for tools/subset_efont.py:  python tools/test_subset_efont.py

Only where it writes and what it includes — generating the fonts needs LovyanGFX's sources
under .pio/ (fonts_test.cpp in test_display checks the glyphs themselves).
"""

import inspect
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import subset_efont  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FONTS_DIR = ROOT / "src" / "display" / "domain" / "theme" / "fonts"


class OutputTest(unittest.TestCase):
    # Scenario: Generator fontów pisze do nowego katalogu i dołącza nagłówek pełną ścieżką
    def test_writes_fonts_into_their_new_directory_with_full_path_include(self):
        out_c = Path(os.path.normpath(subset_efont.OUT_C))
        self.assertEqual(FONTS_DIR / "lcars_efont.c", out_c)
        self.assertTrue(out_c.is_file(), out_c)

        include = getattr(subset_efont, "INCLUDE_LINE", None)
        self.assertEqual('#include "display/domain/theme/fonts/lcars_efont.h"', include)
        committed = out_c.read_text(encoding="utf-8").splitlines()
        self.assertIn(include, committed)
        # main() writes that constant, not a literal of its own.
        body = inspect.getsource(subset_efont.main)
        self.assertIn("INCLUDE_LINE", body)
        self.assertNotIn('#include "lcars_efont.h"', body)


if __name__ == "__main__":
    unittest.main()
