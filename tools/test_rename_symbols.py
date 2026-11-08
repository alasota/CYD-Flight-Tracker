"""Tests for tools/rename_symbols.py:  python tools/test_rename_symbols.py

Spec 12 stage 7 (section 3, the glossary): every rename commit is `python
tools/rename_symbols.py KEY`. What the tests expect of the tool:

- `RENAMES_STAGE_7`: ordered entries `(key, [(old, new), ...], kind)`; `kind` is "compound"
  (renamed in code and in comments outside "quoted text"), "word" (a plain English word --
  `Aircraft`, `Config` -- renamed in code only; every comment hit is reported) or "prefix"
  (`configPortal` followed by an upper-case letter, so WiFiManager's `setConfigPortalBlocking`
  and the SDK's `configTzTime` keep their names);
- `rename_text(text, pairs, kind)` -> `(new_text, review)`: identifiers in code replaced as whole
  tokens (`Config2`, `_Config`, `Config_t`, `makeConfig` are other tokens); "strings" (with
  escapes), 'chars', raw strings `R"d(...)d"` and `#include "..."` paths never change; `review` is
  `[(line, old name)]` (1-based) of the comment hits left as they are (word kind, or inside
  "quoted text" of a comment). Line endings are kept;
- CLI `KEY [--check] [--root DIR]`, `--list`: scope `src/**` and `test/**` `.h/.hpp/.c/.cpp`
  without the generated `src/admin/core/web_assets.h` and `src/display/core/theme/fonts/*`.
  Default: rewrite and print `path:line` of every hit left for review. `--check`: write nothing,
  print `path:line` of every hit the rewrite would change, exit 1 if there is one (0 if none).
  An unknown key exits non-zero, writes nothing and names the key.

RepoTest checks the repository itself with its own lexer (not the tool's), so it is red per
entry until that entry's rename commit.
"""

import importlib
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
TOOL = TOOLS / "rename_symbols.py"
sys.path.insert(0, str(TOOLS))
from test_check_layers import declaration_lines  # noqa: E402

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}

# The .feature's Examples: (old, new, the file from the repository root that declares the new
# name after stage 8, which moved ScanArea and AirspaceSnapshot to their own headers).
GLOSSARY_EXAMPLES = [
    ("BoundingBox", "ScanArea", "src/opensky/core/scan_area.h"),
    ("computeBoundingBox", "computeScanArea", "src/opensky/core/scan_area.h"),
    ("OpenSkyHealth", "FeedHealth", "src/airspace/core/feed_health.h"),
    ("DataSnapshot", "AirspaceSnapshot", "src/airspace/core/airspace_snapshot.h"),
    ("buildDataSnapshot", "buildAirspaceSnapshot", "src/airspace/services/poll_pipeline.h"),
    ("LookupPorts", "EnrichmentPort", "src/airspace/services/poll_pipeline.h"),
    ("AircraftRow", "TrackedFlight", "src/airspace/core/tracked_flight.h"),
    ("Aircraft", "AircraftState", "src/airspace/core/aircraft_state.h"),
    ("configPortalBegin", "webServerBegin", "src/admin/adapters/web_server.h"),
    ("configPortalRestartPending", "webServerRestartPending", "src/admin/adapters/web_server.h"),
    ("Config", "Settings", "src/admin/core/settings.h"),
    ("defaultConfig", "defaultSettings", "src/admin/core/settings.h"),
    ("sanitizeConfig", "sanitizeSettings", "src/admin/core/settings.h"),
    ("loadConfig", "loadSettings", "src/admin/core/settings.h"),
    ("saveConfig", "saveSettings", "src/admin/core/settings.h"),
    ("configToBackupJson", "settingsToBackupJson", "src/admin/core/config_backup.h"),
    ("restoreConfigFromBackupJson", "restoreSettingsFromBackupJson",
     "src/admin/core/config_backup.h"),
    ("reloadConfig", "reloadSettings", "src/main/net_task.cpp"),
    ("makeConfig", "makeSettings", "test/support/fixtures.h"),
]

# Every (old, new) pair the table must carry: the Examples plus the rest of the plan's inventory.
PLAN_PAIRS = {(old, new) for old, new, _ in GLOSSARY_EXAMPLES} - {
    ("configPortalBegin", "webServerBegin"),
    ("configPortalRestartPending", "webServerRestartPending")} | {
    ("configPortal", "webServer")}

GENERATED = ["src/admin/core/web_assets.h", "src/display/core/theme/fonts/lcars_efont.c"]

AIRCRAFT = [("Aircraft", "AircraftState")]
CONFIG = [("Config", "Settings")]


def code_only(text):
    """`text` with comments, "strings", 'chars' and raw strings blanked (newlines kept, so line
    numbers stay) -- the test's own lexer, independent of the tool's."""
    out, i, n = [], 0, len(text)

    def blank(s):
        return "".join(ch if ch == "\n" else " " for ch in s)

    while i < n:
        c = text[i]
        prev = text[i - 1] if i else ""
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif text.startswith('R"', i) and (not (prev.isalnum() or prev == "_") or prev in "8LuU"):
            p = text.find("(", i + 2)
            delim = text[i + 2:p]
            j = text.find(")" + delim + '"', p)
            j = n if j < 0 else j + len(delim) + 2
        elif c == '"' or (c == "'" and not (prev.isalnum() or prev == "_")):
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j += 1
        else:
            out.append(c)
            i += 1
            continue
        out.append(blank(text[i:j]))
        i = j
    return "".join(out)


def repo_sources(*dirs):
    return {p.relative_to(ROOT).as_posix(): p.read_text(encoding="utf-8", errors="replace")
            for d in dirs for p in sorted((ROOT / d).rglob("*"))
            if p.is_file() and p.suffix in SOURCE_SUFFIXES}


def code_hits(files, pattern):
    """["path:line"] of `pattern` (a regex) in the code of `files` (comments, strings out)."""
    rx = re.compile(pattern)
    hits = []
    for path, text in files.items():
        code = code_only(text)
        hits += [f"{path}:{code.count(chr(10), 0, m.start()) + 1}" for m in rx.finditer(code)]
    return hits


def word(name):
    return r"\b" + re.escape(name) + r"\b"


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


# A small tree for the CLI: CRLF and LF files, generated files and a file outside the scope.
CLI_TREE = {
    "src/admin/core/store.h": b"#pragma once\r\nstruct Config {\r\n  int x;\r\n};\r\n"
                              b"// Config holds the settings\r\n",
    "src/main/net.cpp": b'#include "admin/core/store.h"\nstatic Config cfg;\n'
                        b'const char *k = "Config";\n',
    "test/test_admin/store_test.cpp": b"static void test_x() { Config c{}; (void)c; }\n",
    "src/admin/core/web_assets.h": b"// generated\nstatic const char *Config = \"x\";\n",
    "src/display/core/theme/fonts/lcars_efont.c": b"int Config = 1;\n",
    "tools/helper.py": b"Config = 1\n",
}

# CLI_TREE after `python tools/rename_symbols.py Config`.
CLI_TREE_RENAMED = {
    **CLI_TREE,
    "src/admin/core/store.h": b"#pragma once\r\nstruct Settings {\r\n  int x;\r\n};\r\n"
                              b"// Config holds the settings\r\n",
    "src/main/net.cpp": b'#include "admin/core/store.h"\nstatic Settings cfg;\n'
                        b'const char *k = "Config";\n',
    "test/test_admin/store_test.cpp": b"static void test_x() { Settings c{}; (void)c; }\n",
}


class ToolTestCase(unittest.TestCase):
    def tool(self):
        """The rename_symbols module, or this test fails (not the whole file)."""
        try:
            return importlib.import_module("rename_symbols")
        except ModuleNotFoundError as e:
            if e.name != "rename_symbols":
                raise
            self.fail("tools/rename_symbols.py doesn't exist yet")

    def rename(self, text, pairs, kind):
        new, review = self.tool().rename_text(text, pairs, kind)
        return new, [(line, name) for line, name in review]

    def table(self):
        table = list(self.tool().RENAMES_STAGE_7)
        self.assertGreater(len(table), 0, "RENAMES_STAGE_7 is empty")
        return table


# ---- the scanner ------------------------------------------------------------------------------

class RenameTextTest(ToolTestCase):
    # Scenario: Zmiana nazwy dotyka tylko całego identyfikatora, nie jego części
    def test_only_whole_identifiers_are_renamed(self):
        text = ("void f(const Aircraft &ac, AircraftRow row);\n"
                "auto a = makeAircraft();\n"
                "auto n = extractAircraftFromStates(states);\n"
                "constexpr size_t k = kAircraftCacheCapacity;\n")
        new, _ = self.rename(text, AIRCRAFT, "word")
        self.assertEqual(text.replace("const Aircraft &ac", "const AircraftState &ac"), new)

    # Scenario: Napisy w kodzie i cytowane nagłówki dokumentacji w komentarzach się nie zmieniają
    def test_string_literals_and_quoted_doc_headings_are_kept(self):
        text = ('const char *k = "Config";\n'
                'const char *body = R"({"error":"Aircraft not found."})";\n'
                "const char q = '\"'; Config after_char;\n"
                '// docs/data-sources.md "Aircraft identification"\n'
                'const Rule r = {"web.password", [](Config &c) { (void)c; }, "Config"};\n'
                'const char *u = "http://Config /* Aircraft"; Aircraft a;\n')
        new, _ = self.rename(text, AIRCRAFT + CONFIG, "word")
        expected = ('const char *k = "Config";\n'
                    'const char *body = R"({"error":"Aircraft not found."})";\n'
                    "const char q = '\"'; Settings after_char;\n"
                    '// docs/data-sources.md "Aircraft identification"\n'
                    'const Rule r = {"web.password", [](Settings &c) { (void)c; }, "Config"};\n'
                    'const char *u = "http://Config /* Aircraft"; AircraftState a;\n')
        self.assertEqual(expected, new)

    # Scenario: Słowo z prozy w komentarzu jest zgłaszane do decyzji, a nie zmieniane
    def test_plain_word_in_a_comment_is_reported_not_renamed(self):
        text = ("int x;\n"
                "// Aircraft past the budget render as --\n"
                "// returns an Aircraft list\n"
                "std::vector<Aircraft> list();\n")
        new, review = self.rename(text, AIRCRAFT, "word")
        self.assertEqual(text.replace("std::vector<Aircraft>", "std::vector<AircraftState>"), new)
        self.assertEqual([(2, "Aircraft"), (3, "Aircraft")], review)
        # A compound name in a comment outside quotes is renamed (and not reported).
        text = ("// one AircraftRow per aircraft\n"
                "/* DataSnapshot: the poll's result */\n"
                "AircraftRow r;\n")
        new, review = self.rename(text, [("AircraftRow", "TrackedFlight")], "compound")
        self.assertEqual("// one TrackedFlight per aircraft\n"
                         "/* DataSnapshot: the poll's result */\n"
                         "TrackedFlight r;\n", new)
        self.assertEqual([], review)
        new, _ = self.rename(text, [("DataSnapshot", "AirspaceSnapshot")], "compound")
        self.assertIn("/* AirspaceSnapshot: the poll's result */", new)

    # Scenario: Zmiana przedrostka configPortal na webServer nie dotyka nazw z bibliotek
    def test_prefix_rename_keeps_library_names(self):
        text = ("configPortalBegin();\n"
                "configPortalSetOtaObserver(obs);\n"
                "wm.setConfigPortalBlocking(false);\n"
                "bool on = wm.getConfigPortalActive();\n"
                "wm.setConfigPortalTimeout(180);\n"
                "configTzTime(tz, ntp);\n"
                "// configPortalLoop() runs the routes\n")
        new, _ = self.rename(text, [("configPortal", "webServer")], "prefix")
        self.assertEqual("webServerBegin();\n"
                         "webServerSetOtaObserver(obs);\n"
                         "wm.setConfigPortalBlocking(false);\n"
                         "bool on = wm.getConfigPortalActive();\n"
                         "wm.setConfigPortalTimeout(180);\n"
                         "configTzTime(tz, ntp);\n"
                         "// webServerLoop() runs the routes\n", new)

    # Edge case: a block comment over several lines -- compound renamed on its own line, a word
    # reported with the right line, code after `*/` is code again.
    def test_block_comment_spanning_lines(self):
        text = ("/* first line\n"
                "   AircraftRow in the middle\n"
                "   Aircraft at the end */ Aircraft a;\n"
                "Aircraft b;\n")
        new, review = self.rename(text, AIRCRAFT, "word")
        self.assertEqual("/* first line\n"
                         "   AircraftRow in the middle\n"
                         "   Aircraft at the end */ AircraftState a;\n"
                         "AircraftState b;\n", new)
        self.assertEqual([(3, "Aircraft")], review)
        new, _ = self.rename(text, [("AircraftRow", "TrackedFlight")], "compound")
        self.assertIn("   TrackedFlight in the middle\n", new)

    # Edge case: an escaped quote doesn't end the string.
    def test_escaped_quote_inside_string(self):
        text = 'log("say \\"Config\\" now", Config{});\nconst char *s = "\\\\"; Config c;\n'
        new, _ = self.rename(text, CONFIG, "word")
        self.assertEqual('log("say \\"Config\\" now", Settings{});\n'
                         'const char *s = "\\\\"; Settings c;\n', new)

    # Edge case: a raw string with its own delimiter may hold `)"` and quotes.
    def test_raw_string_with_custom_delimiter(self):
        text = ('const char *p = R"x(Config )" "Config" (Config))x"; Config c;\n'
                'const char *q = R"(\nConfig\n)"; Config d;\n')
        new, _ = self.rename(text, CONFIG, "word")
        self.assertEqual('const char *p = R"x(Config )" "Config" (Config))x"; Settings c;\n'
                         'const char *q = R"(\nConfig\n)"; Settings d;\n', new)

    # Edge case: code right after a closing quote is code.
    def test_identifier_right_after_a_string_on_the_same_line(self):
        text = 'f("a",Config{}); g("Config")+Config::kX; h(\'C\',Config{});\n'
        new, _ = self.rename(text, CONFIG, "word")
        self.assertEqual('f("a",Settings{}); g("Config")+Settings::kX; h(\'C\',Settings{});\n',
                         new)

    # Edge case: an include path is a string -- the file rename is rewrite_includes.py's job.
    def test_preprocessor_include_path_is_not_renamed(self):
        text = ('#include "admin/core/config_store.h"  // config_store\n'
                "#include <vector>\n"
                "int config_store = 0;\n")
        new, _ = self.rename(text, [("config_store", "settings")], "compound")
        self.assertEqual('#include "admin/core/config_store.h"  // settings\n'
                         "#include <vector>\n"
                         "int settings = 0;\n", new)

    # Edge case: digits and underscores belong to the identifier.
    def test_digit_and_underscore_neighbours(self):
        text = "Config2 a; _Config b; Config_t c; makeConfig(); Config d;\n"
        new, _ = self.rename(text, CONFIG, "word")
        self.assertEqual("Config2 a; _Config b; Config_t c; makeConfig(); Settings d;\n", new)

    # Edge case: an apostrophe in a comment is not a quote -- the rest of the line is still
    # renamed (compound) or reported (word).
    def test_apostrophe_in_a_comment_is_not_a_quote(self):
        text = "// AircraftRow's fields, see AircraftRow\nint x;\n"
        new, review = self.rename(text, [("AircraftRow", "TrackedFlight")], "compound")
        self.assertEqual("// TrackedFlight's fields, see TrackedFlight\nint x;\n", new)
        self.assertEqual([], review)
        new, review = self.rename("// don't build an Aircraft here\n", AIRCRAFT, "word")
        self.assertEqual("// don't build an Aircraft here\n", new)
        self.assertEqual([(1, "Aircraft")], review)

    # Edge case: a compound name in "quoted text" of a comment is kept and reported (plan step 7:
    # tracked_flight.h quotes "connect Aircraft + AircraftInfo + RouteInfo").
    def test_compound_name_in_quoted_comment_text_is_kept_and_reported(self):
        text = 'int x;\n// spec 3 said "one AircraftRow per aircraft"; AircraftRow is it\n'
        new, review = self.rename(text, [("AircraftRow", "TrackedFlight")], "compound")
        self.assertEqual('int x;\n// spec 3 said "one AircraftRow per aircraft"; '
                         "TrackedFlight is it\n", new)
        self.assertEqual([(2, "AircraftRow")], review)

    # Edge case: CRLF stays CRLF, a missing final newline stays missing.
    def test_line_endings_are_kept_in_text(self):
        text = "Config a;\r\n// Config here\r\nConfig b;"
        new, review = self.rename(text, CONFIG, "word")
        self.assertEqual("Settings a;\r\n// Config here\r\nSettings b;", new)
        self.assertEqual([(2, "Config")], review)

    # Edge case: renaming twice changes nothing more.
    def test_rename_text_is_idempotent(self):
        text = "Config a; // Config\nAircraftRow r; // AircraftRow\n"
        once, _ = self.rename(text, CONFIG, "word")
        self.assertNotEqual(text, once)
        twice, _ = self.rename(once, CONFIG, "word")
        self.assertEqual(once, twice)


# ---- the table --------------------------------------------------------------------------------

class TableTest(ToolTestCase):
    # Edge case: the table carries every glossary entry of the plan, with the right kinds.
    def test_table_has_every_glossary_entry(self):
        table = self.table()
        pairs = {(old, new) for _, entry_pairs, _ in table for old, new in entry_pairs}
        self.assertEqual(set(), PLAN_PAIRS - pairs, "pairs missing from RENAMES_STAGE_7")
        kinds = {kind for _, _, kind in table}
        self.assertLessEqual(kinds, {"compound", "word", "prefix"})
        prefix = [entry_pairs for _, entry_pairs, kind in table if kind == "prefix"]
        self.assertEqual([[("configPortal", "webServer")]], prefix)
        keys = [key for key, _, _ in table]
        self.assertEqual(len(keys), len(set(keys)), "two entries share a key")
        for key in ("BoundingBox", "OpenSkyHealth", "DataSnapshot", "LookupPorts",
                    "AircraftRow", "Aircraft", "configPortal", "Config"):
            self.assertIn(key, keys)

    # Edge case: no chain renames (A -> B, then B -> C) and no name renamed twice.
    def test_new_names_are_distinct_and_not_old_names(self):
        table = self.table()
        olds = [old for _, pairs, _ in table for old, _ in pairs]
        news = [new for _, pairs, _ in table for _, new in pairs]
        self.assertEqual(len(olds), len(set(olds)), "an old name appears twice")
        self.assertEqual(len(news), len(set(news)), "a new name appears twice")
        self.assertEqual(set(), set(olds) & set(news), "a new name is also an old name")

    # Edge case: a new name doesn't exist in the code yet while its old name is still there.
    def test_new_names_unused_in_the_tree_before_their_rename(self):
        files = repo_sources("src", "test")
        self.assertGreater(len(files), 100)
        for key, pairs, kind in self.table():
            for old, new in pairs:
                if kind == "prefix":
                    old_re, new_re = r"\b" + old + r"(?=[A-Z])", r"\b" + new + r"(?=[A-Z])"
                else:
                    old_re, new_re = word(old), word(new)
                if not code_hits(files, old_re):
                    continue  # already renamed
                with self.subTest(key=key, new=new):
                    hits = code_hits(files, new_re)
                    self.assertEqual([], hits, f"{new} is already used: {hits[:10]}")

    # Edge case: only the plain-word names are code-only.
    def test_word_entries_are_exactly_aircraft_and_config(self):
        words = {old for _, pairs, kind in self.table() if kind == "word" for old, _ in pairs}
        self.assertEqual({"Aircraft", "Config"}, words)


# ---- the command line -------------------------------------------------------------------------

class CliTest(ToolTestCase):
    # Scenario: Tryb --check tylko raportuje, drugi przebieg nic nie zmienia, końce linii zostają
    def test_check_lists_leftovers_without_writing(self):
        self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, CLI_TREE)
            before = read_tree(tmp)
            code, out = run_cli(tmp, "--check", "Config")
            self.assertEqual(1, code, out)
            for where in ("src/admin/core/store.h:2", "src/main/net.cpp:2",
                          "test/test_admin/store_test.cpp:1"):
                self.assertIn(where, out)
            for path in GENERATED + ["tools/helper.py"]:
                self.assertNotIn(path, out)
            self.assertEqual(before, read_tree(tmp), "--check wrote a file")

    # Scenario: Tryb --check tylko raportuje, drugi przebieg nic nie zmienia, końce linii zostają
    def test_second_run_changes_nothing(self):
        self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, CLI_TREE)
            code, out = run_cli(tmp, "Config")
            self.assertEqual(0, code, out)
            first = read_tree(tmp)
            self.assertNotEqual(CLI_TREE, first, "the first run changed nothing")
            # The comment hit is left for review and named in the output.
            self.assertIn("src/admin/core/store.h:5", out)
            code, out = run_cli(tmp, "Config")
            self.assertEqual(0, code, out)
            self.assertEqual(first, read_tree(tmp), "the second run changed a file")
            code, out = run_cli(tmp, "--check", "Config")
            self.assertEqual(0, code, out)

    # Scenario: Tryb --check tylko raportuje, drugi przebieg nic nie zmienia, końce linii zostają
    def test_line_endings_and_generated_files_are_kept(self):
        self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, CLI_TREE)
            code, out = run_cli(tmp, "Config")
            self.assertEqual(0, code, out)
            after = read_tree(tmp)
            for path, data in CLI_TREE_RENAMED.items():
                with self.subTest(path=path):
                    self.assertEqual(data, after[path])

    # Edge case: an unknown key is an error and writes nothing.
    def test_unknown_key_is_an_error(self):
        self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, CLI_TREE)
            before = read_tree(tmp)
            code, out = run_cli(tmp, "NoSuchName")
            self.assertNotEqual(0, code, out)
            self.assertIn("NoSuchName", out)
            self.assertEqual(before, read_tree(tmp))

    # Edge case: --list prints every key of the table.
    def test_list_prints_the_table(self):
        table = self.table()
        proc = subprocess.run([sys.executable, str(TOOL), "--list"], capture_output=True)
        out = proc.stdout.decode("utf-8", "replace")
        self.assertEqual(0, proc.returncode, out)
        for key, _, _ in table:
            self.assertIn(key, out)


# ---- the repository ---------------------------------------------------------------------------

class RepoTest(unittest.TestCase):
    # Scenario Outline: Nazwa ze słownika zastępuje dawną w całym kodzie src/ i test/
    def test_old_names_are_gone_from_code(self):
        files = repo_sources("src", "test")
        self.assertGreater(len(files), 100)
        for old, new, path in GLOSSARY_EXAMPLES:
            with self.subTest(old=old, new=new):
                hits = code_hits(files, word(old))
                self.assertEqual([], hits, f"{len(hits)} use(s) of {old} in code: {hits[:10]}")
                self.assertIn(path, files, f"{path} doesn't exist")
                self.assertTrue(declaration_lines(files[path], new),
                                f"{new} isn't declared or defined in {path}")
        with self.subTest(old="configPortal*"):
            hits = code_hits(files, r"\bconfigPortal[A-Z]\w*")
            self.assertEqual([], hits, f"configPortal... left in code: {hits[:10]}")

    # Scenario: CI uruchamia testy narzędzia zmiany nazw
    def test_ci_runs_the_rename_symbols_tests(self):
        workflow = (ROOT / ".github" / "workflows" / "firmware.yml").read_text(encoding="utf-8")
        self.assertRegex(workflow, r"(?m)^\s*python tools/test_rename_symbols\.py\s*$")


if __name__ == "__main__":
    unittest.main()
