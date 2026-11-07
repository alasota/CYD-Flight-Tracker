"""Tests for tools/rewrite_includes.py:  python tools/test_rewrite_includes.py

RewriteTest feeds the pure functions strings and runs the CLI on temporary trees (never on this
repository); MoveTableTest pins the stage-1, stage-2, stage-4 and stage-7 move tables (spec 12
plan) against src/.
"""

import posixpath
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_layers  # noqa: E402
import rewrite_includes as ri  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(__file__).resolve().parent / "rewrite_includes.py"

# Files in the flat src/ (+ src/fonts/) before stage 1.
STAGE_0_FILE_COUNT = 94
# Stage-1 paths whose layer folder stage 2 renames (all but shared/, platform/, */adapters/).
STAGE_2_FILE_COUNT = 80
# Stage 4 renames one module (spec 12 section 4.5): the WebServer routes become web_server.
STAGE_4_RENAMES = {
    "admin/adapters/config_portal.h": "admin/adapters/web_server.h",
    "admin/adapters/config_portal.cpp": "admin/adapters/web_server.cpp",
}
# Stage 7 renames the settings module after the glossary (spec 12 section 3): Config is Settings.
STAGE_7_RENAMES = {
    "admin/core/config_store.h": "admin/core/settings.h",
    "admin/core/config_store.cpp": "admin/core/settings.cpp",
}
# A stage-1 path in a layer folder stage 2 renames: <ctx>/domain/, <ctx>/app/ or the root app/.
_OLD_LAYER_RE = re.compile(r"^(?:(airspace|enrichment|opensky|display|admin)/(domain|app)/|app/)")


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
    "src/display/core/screens/table_view.h": '#pragma once\n#include "core_link.h"\n',
    "src/display/core/screens/table_view.cpp": '#include "table_view.h"\n#include <vector>\n',
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
                self.assertEqual(folder + name, ri.MOVES_STAGE_1.get(name))

    # Scenario: Tabele przeniesień etapów 1, 2 i 4 zgadzają się z drzewem
    def test_move_table_matches_the_tree(self):
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES_STAGE_1))
        finals = {old: ri.final_path(old) for old in ri.MOVES_STAGE_1}
        missing = [f"{old} -> {new}" for old, new in finals.items()
                   if not (ROOT / "src" / new).is_file()]
        self.assertEqual([], missing, "final paths that don't exist")
        left_stage_0 = [old for old in ri.MOVES_STAGE_1 if (ROOT / "src" / old).exists()]
        self.assertEqual([], left_stage_0, "stage-0 flat paths still present")
        # Every path a file had between two tables (before the last table that changes it).
        left = []
        for old in ri.MOVES_STAGE_1:
            path = old
            for stage, table in enumerate(ri.STAGES, 1):
                path = table.get(path, path)
                if path != finals[old] and (ROOT / "src" / path).exists():
                    left.append(f"{path} (after table {stage})")
        self.assertEqual([], left, "intermediate paths still present")

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
                self.assertEqual(new, ri.MOVES_STAGE_1.get(old))

    def test_every_destination_is_a_context_layer_path(self):
        # Final paths: stage-1 destinations in domain/ / app/ no longer classify after stage 2.
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES_STAGE_1))
        finals = [ri.final_path(old) for old in ri.MOVES_STAGE_1]
        bad = [p for p in finals if check_layers.classify("src/" + p) in (None, "test")]
        self.assertEqual([], bad)

    def test_no_two_files_share_a_destination_and_none_is_renamed(self):
        # Stages 1 and 2 only move files; stage 4 renames exactly config_portal -> web_server,
        # stage 7 exactly config_store -> settings.
        self.assertEqual(STAGE_0_FILE_COUNT, len(ri.MOVES_STAGE_1))
        self.assertEqual(4, len(ri.STAGES), "STAGES = the stage 1, 2, 4 and 7 tables")
        self.assertIs(ri.MOVES_STAGE_1, ri.STAGES[0])
        self.assertIs(ri.MOVES_STAGE_2, ri.STAGES[1])
        self.assertIs(getattr(ri, "MOVES_STAGE_4", None), ri.STAGES[2])
        self.assertIs(getattr(ri, "MOVES_STAGE_7", None), ri.STAGES[3])
        expected_renames = {1: {}, 2: {}, 3: STAGE_4_RENAMES, 4: STAGE_7_RENAMES}
        for stage, table in enumerate(ri.STAGES, 1):
            with self.subTest(table=stage):
                self.assertEqual(len(table), len(set(table.values())))
                renamed = {o: n for o, n in table.items()
                           if posixpath.basename(o) != posixpath.basename(n)}
                self.assertEqual(expected_renames[stage], renamed)
        finals = [ri.final_path(old) for old in ri.MOVES_STAGE_1]
        self.assertEqual(len(finals), len(set(finals)), "two files share a final path")

    def test_moves_is_the_last_stage(self):
        # --move applies MOVES: the table of the newest stage.
        self.assertIs(ri.STAGES[-1], ri.MOVES)

    def test_final_path_follows_all_stages(self):
        cases = {
            "fonts/lcars_efont.c": "display/core/theme/fonts/lcars_efont.c",
            "main.cpp": "main/main.cpp",
            "config_portal.h": "admin/adapters/web_server.h",
            "config_portal.cpp": "admin/adapters/web_server.cpp",
            "web_i18n.cpp": "admin/core/web_i18n.cpp",
            "screenshot.cpp": "display/services/screenshot.cpp",
            "sun_times.h": "shared/sun_times.h",
            "core_link.cpp": "platform/core_link.cpp",
            "hexdb_http.h": "enrichment/adapters/hexdb_http.h",
        }
        for old, new in cases.items():
            with self.subTest(file=old):
                self.assertEqual(new, ri.final_path(old))

    # Scenario Outline: Tabela etapu 4 zmienia nazwę config_portal na web_server (§4.5) i nic
    # więcej
    def test_stage_4_renames_config_portal_to_web_server(self):
        table = getattr(ri, "MOVES_STAGE_4", None)
        self.assertIsNotNone(table, "rewrite_includes.py has no MOVES_STAGE_4")
        self.assertIs(table, ri.STAGES[2], "MOVES_STAGE_4 is the third table in STAGES")
        self.assertEqual(STAGE_4_RENAMES, table)
        rows = [("config_portal.h", "admin/adapters/web_server.h"),
                ("config_portal.cpp", "admin/adapters/web_server.cpp")]
        for old, new in rows:
            with self.subTest(file=old):
                self.assertEqual(new, ri.final_path(old))
                self.assertTrue((ROOT / "src" / new).is_file(), f"src/{new}")
                self.assertFalse((ROOT / "src/admin/adapters" / old).exists(),
                                 f"src/admin/adapters/{old}")
        # hexdb_http keeps its name: section 4.2 calls it hexdb_http too.
        self.assertEqual("enrichment/adapters/hexdb_http.h", ri.final_path("hexdb_http.h"))

    # Scenario: Moduł ustawień nazywa się settings, a jego testy settings_test.cpp
    def test_stage_7_renames_config_store_to_settings(self):
        table = getattr(ri, "MOVES_STAGE_7", None)
        self.assertIsNotNone(table, "rewrite_includes.py has no MOVES_STAGE_7")
        self.assertIs(table, ri.STAGES[-1], "MOVES_STAGE_7 is the last table in STAGES")
        self.assertIs(table, ri.MOVES)
        self.assertEqual(STAGE_7_RENAMES, table)
        rows = [("config_store.h", "admin/core/settings.h"),
                ("config_store.cpp", "admin/core/settings.cpp")]
        for old, new in rows:
            with self.subTest(file=old):
                self.assertEqual(new, ri.final_path(old))
                self.assertTrue((ROOT / "src" / new).is_file(), f"src/{new}")
                self.assertFalse((ROOT / "src/admin/core" / old).exists(),
                                 f"src/admin/core/{old}")
        # The stage-4 rename and the other settings modules keep their final paths.
        self.assertEqual("admin/adapters/web_server.h", ri.final_path("config_portal.h"))
        self.assertEqual("admin/core/config_backup.h", ri.final_path("config_backup.h"))

    # Scenario Outline: Etap 2 zmienia tylko nazwę katalogu warstwy, nie plik
    def test_stage_2_renames_the_layer_folder(self):
        rows = [
            ("display/domain/screens/table_view.cpp", "display/core/screens/table_view.cpp"),
            ("display/domain/theme/fonts/lcars_efont.c", "display/core/theme/fonts/lcars_efont.c"),
            ("enrichment/domain/lookup_cache.h", "enrichment/core/lookup_cache.h"),
            ("admin/domain/firmware/version.cpp", "admin/core/firmware/version.cpp"),
            ("airspace/app/poll_pipeline.cpp", "airspace/services/poll_pipeline.cpp"),
            ("display/app/screenshot.cpp", "display/services/screenshot.cpp"),
            ("app/main.cpp", "main/main.cpp"),
            ("app/net_task.h", "main/net_task.h"),
        ]
        for old, new in rows:
            with self.subTest(file=old):
                self.assertEqual(new, ri.MOVES_STAGE_2.get(old))

    # Scenario Outline: admin/app/ rozdziela się: czyste moduły do admin/core/, trasy WebServer
    # do admin/adapters/
    def test_admin_app_splits_into_core_and_adapters(self):
        rows = [
            ("config_page.cpp", "admin/core/"),
            ("config_page.h", "admin/core/"),
            ("web_i18n.cpp", "admin/core/"),
            ("web_i18n.h", "admin/core/"),
            ("config_portal.cpp", "admin/adapters/"),
            ("config_portal.h", "admin/adapters/"),
        ]
        for name, folder in rows:
            with self.subTest(file=name):
                self.assertEqual(folder + name, ri.MOVES_STAGE_2.get("admin/app/" + name))

    # Scenario: Etap 2 nie rusza shared/, platform/ ani plików, które już leżą w adapters/
    def test_stage_2_leaves_shared_platform_and_adapters_alone(self):
        table = ri.MOVES_STAGE_2
        self.assertEqual(STAGE_2_FILE_COUNT, len(table))
        expected_keys = {p for p in ri.MOVES_STAGE_1.values() if _OLD_LAYER_RE.match(p)}
        self.assertEqual(STAGE_2_FILE_COUNT, len(expected_keys))
        self.assertEqual(sorted(expected_keys), sorted(table))
        untouchable = [k for k in table
                       if k.startswith(("shared/", "platform/")) or "/adapters/" in k]
        self.assertEqual([], untouchable)
        renamed = [(o, n) for o, n in table.items()
                   if posixpath.basename(o) != posixpath.basename(n)]
        self.assertEqual([], renamed)


def git_tree(tmp, files):
    write_tree(tmp, files)
    subprocess.run(["git", "init", "-q"], cwd=tmp, check=True)
    subprocess.run(["git", "add", "src"], cwd=tmp, check=True)


def staged_names(tmp):
    return subprocess.run(["git", "diff", "--cached", "--name-only"], cwd=tmp,
                          capture_output=True, text=True, check=True).stdout


class MoveCliTest(unittest.TestCase):
    def test_move_runs_git_mv_drops_empty_folders_and_skips_done_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            # Stage-1 paths and the stage-2 table, in-process: --move itself applies the newest
            # table (stage 4), which empties no folder.
            git_tree(tmp, {"src/display/domain/screens/table_view.h": "#pragma once\n",
                           "src/app/main.cpp": "int main() { return 0; }\n"})
            with mock.patch.object(ri, "MOVES", ri.MOVES_STAGE_2):
                self.assertEqual(2, ri.move_files(Path(tmp)))
            self.assertTrue((Path(tmp) / "src/display/core/screens/table_view.h").is_file())
            self.assertTrue((Path(tmp) / "src/main/main.cpp").is_file())
            self.assertFalse((Path(tmp) / "src/display/domain").exists())
            self.assertFalse((Path(tmp) / "src/app").exists())
            staged = staged_names(tmp)
            self.assertIn("src/display/core/screens/table_view.h", staged)
            self.assertIn("src/main/main.cpp", staged)
            with mock.patch.object(ri, "MOVES", ri.MOVES_STAGE_2):
                self.assertEqual(0, ri.move_files(Path(tmp)))

    def test_move_applies_the_stage_4_rename(self):
        with tempfile.TemporaryDirectory() as tmp:
            # In-process with the stage-4 table: --move itself applies the newest table.
            git_tree(tmp, {"src/admin/adapters/config_portal.h": "#pragma once\n",
                           "src/admin/adapters/config_portal.cpp":
                               '#include "admin/adapters/config_portal.h"\n',
                           "src/admin/adapters/github_ca.h": "#pragma once\n"})
            with mock.patch.object(ri, "MOVES", ri.MOVES_STAGE_4):
                self.assertEqual(2, ri.move_files(Path(tmp)))
            adapters = Path(tmp) / "src/admin/adapters"
            self.assertTrue((adapters / "web_server.h").is_file())
            self.assertTrue((adapters / "web_server.cpp").is_file())
            self.assertFalse((adapters / "config_portal.h").exists())
            self.assertTrue((adapters / "github_ca.h").is_file())
            staged = staged_names(tmp)
            self.assertIn("src/admin/adapters/web_server.h", staged)
            self.assertIn("src/admin/adapters/web_server.cpp", staged)
            with mock.patch.object(ri, "MOVES", ri.MOVES_STAGE_4):
                self.assertEqual(0, ri.move_files(Path(tmp)))

    # Edge case: --move applies the newest table, stage 7's config_store -> settings; the
    # default rewrite then fixes the include, and a second --move moves nothing.
    def test_move_applies_the_stage_7_rename(self):
        with tempfile.TemporaryDirectory() as tmp:
            git_tree(tmp, {"src/admin/core/config_store.h": "#pragma once\n",
                           "src/admin/core/config_store.cpp":
                               '#include "admin/core/config_store.h"\n',
                           "src/admin/core/config_backup.h": "#pragma once\n"})
            code, out = run_cli(tmp, "--move")
            self.assertEqual(0, code, out)
            self.assertIn("moved 2 file(s)", out)
            core = Path(tmp) / "src/admin/core"
            self.assertTrue((core / "settings.h").is_file())
            self.assertTrue((core / "settings.cpp").is_file())
            self.assertFalse((core / "config_store.h").exists())
            self.assertTrue((core / "config_backup.h").is_file())
            staged = staged_names(tmp)
            self.assertIn("src/admin/core/settings.h", staged)
            self.assertIn("src/admin/core/settings.cpp", staged)
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            self.assertEqual(b'#include "admin/core/settings.h"\n',
                             (core / "settings.cpp").read_bytes())
            code, out = run_cli(tmp, "--move")
            self.assertEqual(0, code, out)
            self.assertIn("moved 0 file(s)", out)


class RewriteTest(unittest.TestCase):
    INDEX_PATHS = ["display/core/screens/table_view.h", "platform/core_link.h",
                   "display/core/theme/fonts/lcars_efont.h"]

    def index(self):
        return ri.build_index(self.INDEX_PATHS)

    def test_index_maps_basenames_to_src_relative_paths(self):
        self.assertEqual({"table_view.h": "display/core/screens/table_view.h",
                          "core_link.h": "platform/core_link.h",
                          "lcars_efont.h": "display/core/theme/fonts/lcars_efont.h"},
                         self.index())

    # Scenario: Krótki include przeniesionego nagłówka dostaje pełną ścieżkę
    def test_short_include_becomes_full_path(self):
        self.assertEqual('#include "display/core/screens/table_view.h"\n',
                         ri.rewrite_text('#include "table_view.h"\n', self.index()))

    def test_include_with_an_old_subdirectory_becomes_full_path(self):
        self.assertEqual('#include "display/core/theme/fonts/lcars_efont.h"  // efont\n',
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
        self.assertEqual('#include "display/core/screens/table_view.h"', out[5])

    def test_full_path_include_is_not_touched(self):
        text = '#include "display/core/screens/table_view.h"\n#include "platform/core_link.h"\n'
        self.assertEqual(text, ri.rewrite_text(text, self.index()))

    # Scenario: Drugie uruchomienie niczego nie zmienia
    def test_second_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, SHORT_TREE)
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            first = read_tree(tmp)
            self.assertIn(b'#include "display/core/screens/table_view.h"',
                          first["test/test_display/table_view_test.cpp"])
            self.assertIn(b'#include "platform/core_link.h"',
                          first["src/display/core/screens/table_view.h"])
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
            ri.build_index(["a/core/x.h", "b/core/x.h"])
        self.assertIn("a/core/x.h", str(ctx.exception))
        self.assertIn("b/core/x.h", str(ctx.exception))
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/a/core/x.h": "", "src/b/core/x.h": "",
                             "src/a/core/y.cpp": '#include "x.h"\n'})
            before = read_tree(tmp)
            code, out = run_cli(tmp)
            self.assertNotEqual(0, code, out)
            self.assertIn("a/core/x.h", out)
            self.assertIn("b/core/x.h", out)
            self.assertEqual(before, read_tree(tmp))

    # Scenario: Komentarz za include'em i końce linii CRLF zostają
    def test_trailing_comment_and_crlf_preserved(self):
        text = '#include "core_link.h"  // Guarded<T>\r\n#include <vector>\r\n'
        self.assertEqual('#include "platform/core_link.h"  // Guarded<T>\r\n#include <vector>\r\n',
                         ri.rewrite_text(text, self.index()))
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/platform/core_link.h": b"#pragma once\r\n",
                             "src/main/x.cpp": text.encode("utf-8")})
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            data = (Path(tmp) / "src/main/x.cpp").read_bytes()
        self.assertIn(b'#include "platform/core_link.h"  // Guarded<T>\r\n', data)
        self.assertEqual(data.count(b"\n"), data.count(b"\r\n"))

    # Scenario: Include ze starą nazwą warstwy dostaje nową pełną ścieżkę
    def test_old_layer_path_becomes_the_new_full_path(self):
        user = ('#include "display/domain/i18n.h"  // Language\n'
                '#include "app/net_task.h"\n'
                '#include <vector>\n')
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/display/core/i18n.h": "#pragma once\n",
                             "src/main/net_task.h": "#pragma once\n",
                             "src/display/core/screens/x.cpp": user})
            before = read_tree(tmp)
            code, out = run_cli(tmp, "--check")
            self.assertEqual(1, code, out)
            self.assertEqual(before, read_tree(tmp))
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            lines = (Path(tmp) / "src/display/core/screens/x.cpp").read_text(
                encoding="utf-8").splitlines()
            self.assertEqual(['#include "display/core/i18n.h"  // Language',
                              '#include "main/net_task.h"', '#include <vector>'], lines)
            code, out = run_cli(tmp, "--check")
            self.assertEqual(0, code, out)

    # Scenario: Include pliku o zmienionej nazwie dostaje nową pełną ścieżkę
    def test_renamed_header_include_becomes_the_new_full_path(self):
        full = b'#include "admin/adapters/config_portal.h"  // configPortalBegin()\r\n'
        short = b'#include "config_portal.h"\r\n#include <vector>\r\n'
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/admin/adapters/web_server.h": b"#pragma once\r\n",
                             "src/main/main.cpp": full,
                             "test/test_admin/x_test.cpp": short})
            before = read_tree(tmp)
            code, out = run_cli(tmp, "--check")
            self.assertEqual(1, code, out)
            self.assertEqual(before, read_tree(tmp))
            code, out = run_cli(tmp)
            self.assertEqual(0, code, out)
            after = read_tree(tmp)
            self.assertEqual(b'#include "admin/adapters/web_server.h"  // configPortalBegin()\r\n',
                             after["src/main/main.cpp"])
            self.assertEqual(b'#include "admin/adapters/web_server.h"\r\n#include <vector>\r\n',
                             after["test/test_admin/x_test.cpp"])
            code, out = run_cli(tmp, "--check")
            self.assertEqual(0, code, out)

    def test_renamed_basename_next_to_a_live_header_of_that_name_is_an_error(self):
        # The old name of a renamed file must not belong to another header: the short form
        # would be ambiguous, like two headers of one name.
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/admin/adapters/web_server.h": "",
                             "src/display/core/config_portal.h": "",
                             "src/main/x.cpp": '#include "config_portal.h"\n'})
            before = read_tree(tmp)
            code, out = run_cli(tmp)
            self.assertNotEqual(0, code, out)
            self.assertIn("display/core/config_portal.h", out)
            self.assertEqual(before, read_tree(tmp))


if __name__ == "__main__":
    unittest.main()
