"""Tests for tools/check_comments.py:  python tools/test_check_comments.py

The unit tests feed the pure functions {relpath: text} dicts; RepoTest runs them on this
repository and pins the state spec 11 leaves behind (blocking in CI).
"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_comments  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def show(findings, limit=30):
    lines = [check_comments.format_finding(f) for f in findings[:limit]]
    if len(findings) > limit:
        lines.append(f"... and {len(findings) - limit} more")
    return "\n" + "\n".join(lines)


# ---- §1 removed symbols ---------------------------------------------------------------------

class RemovedSymbolsTest(unittest.TestCase):
    # Scenario: Usunięty symbol, który wrócił do kodu, jest zgłaszany z plikiem i linią
    def test_removed_symbol_in_source_is_reported(self):
        files = {"src/foo.cpp": "  drawElbowFrame(gfx, 0, 0, 10, 10, 4, 2, 0);\n"}
        found = check_comments.find_removed_symbols(files)
        self.assertEqual(1, len(found), show(found))
        self.assertEqual("src/foo.cpp", found[0].path)
        self.assertEqual(1, found[0].line)
        self.assertIn("drawElbowFrame", found[0].message)
        self.assertIn("src/foo.cpp:1", check_comments.format_finding(found[0]))

    # Scenario: Dłuższa nazwa zaczynająca się od usuniętego symbolu nie jest zgłaszana
    def test_longer_name_with_removed_prefix_is_not_reported(self):
        files = {"src/foo.cpp": "auto r = phaseIconRows(kind);\n"
                                "auto k = phaseIconKind(phase, onGround);\n"
                                "drawPanelBorder();\n"}
        found = check_comments.find_removed_symbols(files)
        self.assertEqual([], found, show(found))

    # Scenario: LCARS_ORANGE nie jest zgłaszany jako usunięty
    def test_lcars_orange_is_not_reported(self):
        files = {"src/lcars_theme.h": "constexpr uint16_t THEME_WARNING = LCARS_ORANGE;\n"}
        found = check_comments.find_removed_symbols(files)
        self.assertEqual([], found, show(found))
        self.assertNotIn("LCARS_ORANGE", check_comments.REMOVED_SYMBOLS)

    def test_symbol_in_comment_is_reported_too(self):
        # Criterion 2 is "grep finds nothing" — a comment naming a removed symbol counts.
        files = {"src/bar.h": "int x;\n// see elbowArcPoint() for the seam\n"}
        found = check_comments.find_removed_symbols(files)
        self.assertEqual(1, len(found), show(found))
        self.assertEqual(2, found[0].line)
        self.assertIn("elbowArcPoint", found[0].message)

    def test_drawelbow_does_not_hide_drawelbowframe(self):
        # Both are removed; one line with each gives two findings, not one.
        files = {"src/a.cpp": "drawElbow(g);\ndrawElbowFrame(g);\n"}
        found = check_comments.find_removed_symbols(files)
        self.assertEqual([1, 2], sorted(f.line for f in found), show(found))

    def test_removed_symbols_list_is_complete(self):
        # Spec 11 §1 plus the owner's extras (drawElbow, drawHeaderBlock, LCARS_BLUE_VIOLET,
        # LCARS_FONT_NUMERIC) and rssiToBars (never existed, only described).
        expected = {
            "viewToggleButtonBounds", "kToggleButtonWidth", "kToggleButtonHeight",
            "kToggleButtonMargin", "drawViewToggleButton", "drawElbowFrame", "elbowArcPoint",
            "ElbowArcPoint", "drawVerticalDivider", "drawPanel", "kPanelCornerRadius",
            "phaseIcon", "formatSummaryIdentity", "LCARS_MAGENTA", "LCARS_CYAN", "LCARS_AMBER",
            "LCARS_ROSE", "LCARS_PALE_BLUE", "LCARS_BLUE_VIOLET", "RefreshNow", "drawElbow",
            "drawHeaderBlock", "LCARS_FONT_NUMERIC", "rssiToBars",
        }
        missing = expected - set(check_comments.REMOVED_SYMBOLS)
        self.assertEqual(set(), missing)


# ---- §2 main.cpp in net-module comments ------------------------------------------------------

class MainCppCommentsTest(unittest.TestCase):
    # Scenario: Komentarz „main.cpp reloads” w module sieciowym jest zgłaszany
    def test_main_cpp_in_net_module_comment_is_reported(self):
        files = {"src/config_portal.cpp": "// main.cpp reloads the config\n"}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("src/config_portal.cpp:1", check_comments.format_finding(found[0]))

    # Scenario: Komentarz o 8 KB stosu loopTask w module sieciowym jest zgłaszany
    def test_looptask_in_net_module_comment_is_reported(self):
        files = {"src/opensky_client.cpp": "// loopTask's stack is only 8KB\n"}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("src/opensky_client.cpp:1", check_comments.format_finding(found[0]))

    # Scenario: main.cpp w komentarzu modułu UI nie jest zgłaszany
    def test_main_cpp_in_ui_module_comment_is_not_reported(self):
        files = {"src/redraw_policy.h": "// main.cpp gathers the events each loop\n"}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual([], found, show(found))

    # Scenario: main.cpp poza komentarzem (w napisie w kodzie) nie jest zgłaszany
    def test_main_cpp_outside_comment_is_ignored(self):
        files = {"src/net_task.cpp": 'const char *f = "main.cpp";\n'}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual([], found, show(found))

    # Scenario: Komentarz blokowy też jest sprawdzany
    def test_block_comment_is_checked(self):
        files = {"src/route_lookup.h": "/* budget kept by\n main.cpp */\n"}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("src/route_lookup.h:2", check_comments.format_finding(found[0]))

    def test_main_cpp_file_itself_is_not_checked(self):
        files = {"src/main.cpp": "// main.cpp draws the screens\n",
                 "src/net_task.cpp": "int x;\n// unlike main.cpp, this runs on core 0\n"}
        found = check_comments.find_main_cpp_comments(files)
        self.assertEqual([("src/net_task.cpp", 2)], [(f.path, f.line) for f in found], show(found))

    def test_string_with_slashes_is_not_a_comment(self):
        text = 'const char *u = "http://main.cpp/x"; // ok\n'
        comments = check_comments.extract_comments(text)
        self.assertEqual(1, len(comments), comments)
        self.assertEqual(1, comments[0][0])
        self.assertIn("ok", comments[0][1])
        self.assertNotIn("http", comments[0][1])
        found = check_comments.find_main_cpp_comments({"src/net_task.cpp": text})
        self.assertEqual([], found, show(found))

    def test_net_modules_include_the_core0_modules(self):
        for stem in ("config_portal", "opensky_client", "aircraft_lookup", "route_lookup",
                     "lookup_budget", "poll_pipeline", "release_check", "net_task"):
            self.assertIn(stem, check_comments.NET_MODULES)
        for stem in ("main", "redraw_policy", "status_bar", "table_view"):
            self.assertNotIn(stem, check_comments.NET_MODULES)


# ---- CLI --------------------------------------------------------------------------------------

def write_tree(root, files):
    for rel, text in files.items():
        p = Path(root) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")


ONE_FINDING_TREE = {
    "CLAUDE.md": "# CLAUDE.md\n\n## Testing\n",
    "src/config_portal.cpp": "// main.cpp reloads the config\nint x;\n",
}

CLEAN_TREE = {
    "CLAUDE.md": "# CLAUDE.md\n\n## Testing\n",
    "src/net_task.cpp": "// net_task reloads the config\nint x;\n",
}


def run_cli(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = check_comments.main(argv)
    return code, out.getvalue()


class CliTest(unittest.TestCase):
    # Scenario: Bez --strict skrypt tylko ostrzega
    def test_findings_only_warn_by_default(self):
        with tempfile.TemporaryDirectory() as d:
            write_tree(d, ONE_FINDING_TREE)
            code, out = run_cli(["--root", d])
        self.assertIn("src/config_portal.cpp:1", out)
        self.assertEqual(0, code)

    # Scenario: Z --strict zgłoszenie kończy skrypt błędem
    def test_strict_mode_fails_on_findings(self):
        with tempfile.TemporaryDirectory() as d:
            write_tree(d, ONE_FINDING_TREE)
            code, out = run_cli(["--strict", "--root", d])
        self.assertNotEqual(0, code, out)

    def test_strict_mode_passes_clean_tree(self):
        with tempfile.TemporaryDirectory() as d:
            write_tree(d, CLEAN_TREE)
            code, out = run_cli(["--strict", "--root", d])
        self.assertEqual(0, code, out)


# ---- §3 doc references ------------------------------------------------------------------------

class DocRefsTest(unittest.TestCase):
    # Scenario: Odwołanie w komentarzu do sekcji, której nie ma w dokumencie, jest zgłaszane
    def test_reference_to_missing_section_is_reported(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n\n## Project\n\n## Testing\n"}
        files = {"src/table_view.h": '// see CLAUDE.md "Screen 1"\n'}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("src/table_view.h:1", check_comments.format_finding(found[0]))
        self.assertIn("Screen 1", found[0].message)

    # Scenario: Odwołanie pasuje do nagłówka, który się od niego zaczyna
    def test_reference_matches_section_heading_prefix(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n",
                "docs/screens.md": "# Screens\n\n## Screen 1 — Flights\n\ntext\n"}
        files = {"src/table_view.h": '// docs/screens.md "Screen 1"\n'}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual([], found, show(found))

    # Scenario: Stare odwołanie „CLAUDE.md review notes” jest zgłaszane
    def test_old_review_notes_reference_is_reported(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n"}
        files = {"src/config_portal.cpp": "// see CLAUDE.md review notes 1.5\n"}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("src/config_portal.cpp:1", check_comments.format_finding(found[0]))
        self.assertIn("docs/review-notes-2026.md", found[0].message)

    def test_reference_to_missing_file_is_reported(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n\n## X\n"}
        files = {"src/a.h": '// docs/nope.md "X"\n'}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("docs/nope.md", found[0].message)

    def test_possessive_form_is_recognised(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n\n## Testing\n"}
        files = {"src/a.h": '// CLAUDE.md\'s "Screen navigation" says so\n'}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual(1, len(found), show(found))
        self.assertIn("Screen navigation", found[0].message)
        docs["CLAUDE.md"] += "\n## Screen navigation\n"
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual([], found, show(found))

    def test_heading_level_does_not_matter(self):
        docs = {"CLAUDE.md": "# CLAUDE.md\n", "docs/screens.md": "# S\n\n### Deep heading here\n"}
        files = {"test/x_test.cpp": '// docs/screens.md "Deep heading"\n'
                                    '// docs/screens.md "Missing one"\n'}
        found = check_comments.find_bad_doc_refs(files, docs)
        self.assertEqual([("test/x_test.cpp", 2)], [(f.path, f.line) for f in found], show(found))


# ---- §3 CLAUDE.md size ------------------------------------------------------------------------

class ClaudeMdSizeTest(unittest.TestCase):
    # Scenario: Granica długości CLAUDE.md to 250 linii
    def test_line_limit_boundary(self):
        self.assertEqual([], check_comments.check_claude_md_size("x\n" * 249))
        found = check_comments.check_claude_md_size("x\n" * 250)
        self.assertEqual(1, len(found), found)
        self.assertEqual("CLAUDE.md", found[0].path)

    def test_trailing_newline_does_not_count_as_a_line(self):
        # 249 lines, the last one ending in "\n", is 249 — not 250.
        self.assertEqual([], check_comments.check_claude_md_size("x\n" * 248 + "x\n"))
        self.assertEqual([], check_comments.check_claude_md_size("x\n" * 248 + "x"))
        self.assertEqual(1, len(check_comments.check_claude_md_size("x\n" * 249 + "x")))


# ---- the repository itself (blocking in CI) ---------------------------------------------------

class RepoTest(unittest.TestCase):
    def source_files(self, dirs):
        files = check_comments.load_tree(ROOT, dirs)
        self.assertGreater(len(files), 50, f"load_tree found only {len(files)} files in {dirs}")
        return files

    # Scenario: Żaden usunięty symbol nie występuje w src/ ani w test/
    def test_removed_symbols_absent_from_src_and_test(self):
        self.assertIn("drawElbowFrame", check_comments.REMOVED_SYMBOLS)
        found = check_comments.find_removed_symbols(self.source_files(("src", "test")))
        self.assertEqual([], found, show(found))

    # Scenario: Żaden komentarz w module rdzenia sieciowego nie przypisuje pracy main.cpp
    def test_no_net_module_comment_mentions_main_cpp(self):
        self.assertIn("net_task", check_comments.NET_MODULES)
        found = check_comments.find_main_cpp_comments(self.source_files(("src",)))
        self.assertEqual([], found, show(found))

    # Scenario: Każde odwołanie do sekcji dokumentacji w src/ i test/ trafia w istniejący nagłówek
    def test_doc_references_in_src_and_test_resolve(self):
        docs = check_comments.load_docs(ROOT)
        self.assertIn("CLAUDE.md", docs)
        for name in ("docs/screens.md", "docs/data-sources.md", "docs/architecture.md",
                     "docs/history.md", "docs/review-notes-2026.md"):
            self.assertIn(name, docs)
        found = check_comments.find_bad_doc_refs(self.source_files(("src", "test")), docs)
        self.assertEqual([], found, show(found))

    # Scenario: CLAUDE.md w repozytorium ma mniej niż 250 linii
    def test_claude_md_under_250_lines(self):
        text = (ROOT / "CLAUDE.md").read_text(encoding="utf-8")
        self.assertLess(len(text.splitlines()), 250)
        self.assertEqual([], check_comments.check_claude_md_size(text))

    # Scenario: Notatki z przeglądu są w docs/, szablonowe README PlatformIO zniknęły
    def test_moved_and_template_files(self):
        self.assertTrue((ROOT / "docs" / "review-notes-2026.md").is_file())
        self.assertFalse((ROOT / "review-notes.md").exists())
        self.assertFalse((ROOT / "lib" / "README").exists())
        include_readme = ROOT / "include" / "README"
        if include_readme.exists():
            text = include_readme.read_text(encoding="utf-8")
            self.assertNotIn("This directory is intended for project header files", text)
            self.assertNotIn("PlatformIO", text)

    # Scenario: rssiToBars() nie jest opisywane jako istniejąca funkcja
    def test_rssi_to_bars_not_described_as_existing(self):
        screens = ROOT / "docs" / "screens.md"
        self.assertTrue(screens.is_file())
        places = {"CLAUDE.md": (ROOT / "CLAUDE.md").read_text(encoding="utf-8"),
                  "docs/screens.md": screens.read_text(encoding="utf-8")}
        for p in sorted((ROOT / "src").rglob("*")):
            if p.is_file():
                places[p.relative_to(ROOT).as_posix()] = p.read_text(encoding="utf-8",
                                                                      errors="replace")
        hits = [name for name, text in places.items() if "rssiToBars" in text]
        self.assertEqual([], hits)


if __name__ == "__main__":
    unittest.main()
