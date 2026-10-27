"""Tests for tools/rewrite_includes.py:  python tools/test_rewrite_includes.py

RewriteTest feeds the pure functions strings and runs the CLI on temporary trees (never on this
repository); MoveTableTest pins the stage-1 move table (spec 12 plan) against src/.
"""

import posixpath
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_layers  # noqa: E402
import rewrite_includes as ri  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(__file__).resolve().parent / "rewrite_includes.py"

# Files in the flat src/ (+ src/fonts/) before stage 1.
STAGE_0_FILE_COUNT = 94


def run_cli(root, *args):
    proc = subprocess.run([sys.executable, str(TOOL), "--root", str(root), *args],
                          capture_output=True)
    out = (proc.stdout + proc.stderr).decode("utf-8", "replace").replace("\\", "/")
    return proc.returncode, out


def write_tree(root, files):
    for rel, data in files.items():
        p = Path(root) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))


def read_tree(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in sorted(Path(root).rglob("*")) if p.is_file()}


SHORT_TREE = {
    "src/display/domain/screens/table_view.h": '#pragma once\n#include "core_link.h"\n',
    "src/display/domain/screens/table_view.cpp": '#include "table_view.h"\n#include <vector>\n',
    "src/platform/core_link.h": "#pragma once\n",
    "test/support/test_support.h": "#pragma once\n",
    "test/test_display/table_view_test.cpp": '#include "table_view.h"\n'
                                             '#include "test_support.h"\n',
}


class MoveTableTest(unittest.TestCase):
    # Scenario Outline: Plik z czystą i sprzętową połową trafia tam, gdzie będzie jego czysta część
    def test_mixed_files_land_with_their_pure_half(self):
        rows = [
            ("opensky_client.cpp", "opensky/domain/"),
            ("lookup_store.cpp", "enrichment/domain/"),
            ("table_view.cpp", "display/domain/screens/"),
            ("backlight.cpp", "display/domain/"),
            ("config_portal.cpp", "admin/app/"),
            ("release_check.cpp", "admin/domain/firmware/"),
            ("time_sync.cpp", "platform/"),
        ]
        for name, folder in rows:
            with self.subTest(file=name):
                self.assertEqual(folder + name, ri.MOVES.get(name))

    # Scenario: Tabela przeniesień zgadza się z drzewem po przeniesieniu
    def test_move_table_matches_the_tree(self):
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES))
        missing = [new for new in ri.MOVES.values() if not (ROOT / "src" / new).is_file()]
        left = [old for old in ri.MOVES if (ROOT / "src" / old).exists()]
        self.assertEqual([], missing, "destinations that don't exist")
        self.assertEqual([], left, "old flat paths still present")

    def test_whole_hardware_files_and_owner_decisions(self):
        expected = {
            "main.cpp": "app/main.cpp",
            "net_task.cpp": "app/net_task.cpp",
            "net_task.h": "app/net_task.h",
            "hexdb_http.cpp": "enrichment/adapters/hexdb_http.cpp",
            "github_ca.h": "admin/adapters/github_ca.h",
            "fonts/lcars_efont.c": "display/domain/theme/fonts/lcars_efont.c",
            "fonts/lcars_efont.h": "display/domain/theme/fonts/lcars_efont.h",
            "screenshot.cpp": "display/app/screenshot.cpp",
            "lcars_theme.h": "display/domain/theme/lcars_theme.h",
            "version.cpp": "admin/domain/firmware/version.cpp",
            "core_link.h": "platform/core_link.h",
            "sun_times.cpp": "shared/sun_times.cpp",
            "lookup_cache.h": "enrichment/domain/lookup_cache.h",
            "poll_pipeline.cpp": "airspace/app/poll_pipeline.cpp",
        }
        for old, new in expected.items():
            with self.subTest(file=old):
                self.assertEqual(new, ri.MOVES.get(old))

    def test_every_destination_is_a_context_layer_path(self):
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES))
        bad = [new for new in ri.MOVES.values()
               if check_layers.classify("src/" + new) in (None, "test")]
        self.assertEqual([], bad)

    def test_no_two_files_share_a_destination_and_none_is_renamed(self):
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES))
        self.assertEqual(len(ri.MOVES), len(set(ri.MOVES.values())))
        renamed = [(o, n) for o, n in ri.MOVES.items()
                   if posixpath.basename(o) != posixpath.basename(n)]
        self.assertEqual([], renamed)


class MoveCliTest(unittest.TestCase):
    def test_move_runs_git_mv_drops_empty_folders_and_skips_done_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/table_view.h": "#pragma once\n",
                             "src/fonts/lcars_efont.h": "#pragma once\n"})
            subprocess.run(["git", "init", "-q"], cwd=tmp, check=True)
            subprocess.run(["git", "add", "src"], cwd=tmp, check=True)
            code, out = run_cli(tmp, "--move")
            self.assertEqual(0, code, out)
            self.assertIn("moved 2 file(s)", out)
            self.assertTrue((Path(tmp) / "src/display/domain/screens/table_view.h").is_file())
            self.assertTrue((Path(tmp) / "src/display/domain/theme/fonts/lcars_efont.h").is_file())
            self.assertFalse((Path(tmp) / "src/fonts").exists())
            staged = subprocess.run(["git", "diff", "--cached", "--name-only"], cwd=tmp,
                                    capture_output=True, text=True, check=True).stdout
            self.assertIn("src/display/domain/screens/table_view.h", staged)
            code, out = run_cli(tmp, "--move")
            self.assertEqual(0, code, out)
            self.assertIn("moved 0 file(s)", out)


class RewriteTest(unittest.TestCase):
    INDEX_PATHS = ["display/domain/screens/table_view.h", "platform/core_link.h",
                   "display/domain/theme/fonts/lcars_efont.h"]

    def index(self):
        return ri.build_index(self.INDEX_PATHS)

    def test_index_maps_basenames_to_src_relative_paths(self):
        self.assertEqual({"table_view.h": "display/domain/screens/table_view.h",
                          "core_link.h": "platform/core_link.h",
                          "lcars_efont.h": "display/domain/theme/fonts/lcars_efont.h"},
                         self.index())

    # Scenario: Krótki include przeniesionego nagłówka dostaje pełną ścieżkę
    def test_short_include_becomes_full_path(self):
        self.assertEqual('#include "display/domain/screens/table_view.h"\n',
                         ri.rewrite_text('#include "table_view.h"\n', self.index()))

    def test_include_with_an_old_subdirectory_becomes_full_path(self):
        self.assertEqual('#include "display/domain/theme/fonts/lcars_efont.h"  // efont\n',
                         ri.rewrite_text('#include "fonts/lcars_efont.h"  // efont\n',
                                         self.index()))

    # Scenario: Include'y systemowe, z test/support/, z include/ i lokalne nagłówki testu
    # zostają bez zmian
    def test_system_support_and_include_dir_headers_unchanged(self):
        kept = ['#include <vector>', '#include <ArduinoJson.h>', '#include "test_support.h"',
                '#include "LGFX_CYD.hpp"', '#include "github_fixtures.h"']
        text = "\n".join(kept + ['#include "table_view.h"']) + "\n"
        out = ri.rewrite_text(text, self.index()).splitlines()
        self.assertEqual(kept, out[:5])
        # The control line in the same file is rewritten, so the tool did run on it.
        self.assertEqual('#include "display/domain/screens/table_view.h"', out[5])

    def test_full_path_include_is_not_touched(self):
        text = '#include "display/domain/screens/table_view.h"\n#include "platform/core_link.h"\n'
        self.assertEqual(text, ri.rewrite_text(text, self.index()))

    # Scenario: Drugie uruchomienie niczego nie zmienia
    def test_second_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, SHORT_TREE)
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            first = read_tree(tmp)
            self.assertIn(b'#include "display/domain/screens/table_view.h"',
                          first["test/test_display/table_view_test.cpp"])
            self.assertIn(b'#include "platform/core_link.h"',
                          first["src/display/domain/screens/table_view.h"])
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            self.assertEqual(first, read_tree(tmp))
            code, out = run_cli(tmp, "--check")
            self.assertEqual(0, code, out)

    def test_check_mode_exits_one_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, SHORT_TREE)
            before = read_tree(tmp)
            code, out = run_cli(tmp, "--check")
            self.assertEqual(1, code, out)
            self.assertEqual(before, read_tree(tmp))

    # Scenario: Dwa nagłówki o tej samej nazwie w src/ to błąd, a nie zgadywanie
    def test_duplicate_header_name_is_an_error(self):
        with self.assertRaises(ValueError) as ctx:
            ri.build_index(["a/domain/x.h", "b/domain/x.h"])
        self.assertIn("a/domain/x.h", str(ctx.exception))
        self.assertIn("b/domain/x.h", str(ctx.exception))
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/a/domain/x.h": "", "src/b/domain/x.h": "",
                             "src/a/domain/y.cpp": '#include "x.h"\n'})
            before = read_tree(tmp)
            code, out = run_cli(tmp)
            self.assertNotEqual(0, code, out)
            self.assertIn("a/domain/x.h", out)
            self.assertIn("b/domain/x.h", out)
            self.assertEqual(before, read_tree(tmp))

    # Scenario: Komentarz za include'em i końce linii CRLF zostają
    def test_trailing_comment_and_crlf_preserved(self):
        text = '#include "core_link.h"  // Guarded<T>\r\n#include <vector>\r\n'
        self.assertEqual('#include "platform/core_link.h"  // Guarded<T>\r\n#include <vector>\r\n',
                         ri.rewrite_text(text, self.index()))
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/platform/core_link.h": b"#pragma once\r\n",
                             "src/app/x.cpp": text.encode("utf-8")})
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            data = (Path(tmp) / "src/app/x.cpp").read_bytes()
        self.assertIn(b'#include "platform/core_link.h"  // Guarded<T>\r\n', data)
        self.assertEqual(data.count(b"\n"), data.count(b"\r\n"))


if __name__ == "__main__":
    unittest.main()
