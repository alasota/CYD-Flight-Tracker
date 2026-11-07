"""Tests for tools/embed_assets.py:  python tools/test_embed_assets.py

Spec 12 stage 6 (section 4.5): the settings page's CSS, JS and icon live in src/admin/assets/;
embed_assets.py turns them into the C string literals of src/admin/core/web_assets.h, which only
config_page.cpp includes. What the tests expect of the tool:

- importing it has no side effects (no SCons `Import`, no writes); `python tools/embed_assets.py`
  runs the CLI;
- `embed_text(text, kind, name)` (kind "css" / "js"; `name` is the file name used in errors) ->
  the joined text: full-line comments dropped (`/* ... */` in CSS, `// ...` in JS), every line
  trimmed, empty lines dropped, lines joined with no separator. `\\r\\n` reads as `\\n`. Raises
  `EmbedError` naming `<name>:<line>` (the line whose end would be glued) when a line ends in a
  letter / digit / `_` / `$` / quote and the next starts with one, and for a comment after code;
- `svg_data_uri(text)` -> `data:image/svg+xml,` + the markup without whitespace between tags,
  `"` -> `'`, `#` -> `%23`;
- `c_literal(text)` -> one C string literal: `"` and `\\` escaped, everything else (`'`, UTF-8)
  as it is, never a `\\x` escape;
- `generate_header(assets_dir)` -> the text of web_assets.h (LF, deterministic) for style.css,
  app.js, restart.js, check_pending.js, favicon.svg as kWebStyleCss, kWebAppJs, kWebRestartJs,
  kWebCheckPendingJs, kWebFaviconUri; `EmbedError` naming a missing file;
- CLI `[--assets DIR] [--out FILE] [--check]`: writes only when the content differs; `--check`
  exits 1 naming the stale file and writes nothing; an error exits non-zero, writes nothing.

Each test fails on its own while the tool doesn't exist (no import error for the whole file).
"""

import configparser
import importlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
TOOL = TOOLS / "embed_assets.py"
sys.path.insert(0, str(TOOLS))
from test_check_layers import strip_comments  # noqa: E402

ASSETS = ROOT / "src" / "admin" / "assets"
HEADER = ROOT / "src" / "admin" / "core" / "web_assets.h"
ASSET_FILES = ["style.css", "app.js", "restart.js", "check_pending.js", "favicon.svg"]
ASSET_NAMES = ["kWebStyleCss", "kWebAppJs", "kWebRestartJs", "kWebCheckPendingJs",
               "kWebFaviconUri"]

# A small valid set of assets for the CLI tests.
MINIMAL_ASSETS = {
    "style.css": "body{margin:0}\n",
    "app.js": "function f(){return 1}\n",
    "restart.js": "setTimeout(f,5000);\n",
    "check_pending.js": "setTimeout(f,3000);\n",
    "favicon.svg": '<svg xmlns="http://www.w3.org/2000/svg">\n  <rect fill="#000"/>\n</svg>\n',
}

# The panel icon's data URI as config_page.cpp wrote it by hand before stage 6 (kFavicon).
PANEL_ICON_URI = (
    "data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
    "<rect width='32' height='32' rx='8' fill='%231d1f24'/>"
    "<path fill='%23ff9966' d='M5 18.5l2-1 5 1.5 4-7.5-2.5-6.5h2.5l6 6.5 5-.5c1.6 0 2.5.8 2.5 "
    "1.6s-.9 1.6-2.5 1.6l-5-.5-4.5 7h-2.5l1.5-6-5 .5-2 2.5h-2l1-3.5z'/></svg>")


class ToolTestCase(unittest.TestCase):
    def tool(self):
        """The embed_assets module, or this test fails (not the whole file)."""
        try:
            return importlib.import_module("embed_assets")
        except ModuleNotFoundError as e:
            if e.name != "embed_assets":
                raise
            self.fail("tools/embed_assets.py doesn't exist yet")

    def assertEmbedError(self, fn, *args, where):
        tool = self.tool()
        with self.assertRaises(tool.EmbedError) as cm:
            fn(tool)(*args)
        self.assertIn(where, str(cm.exception))


def run_cli(*args):
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True,
                          text=True, encoding="utf-8", errors="replace", cwd=ROOT)


def write_assets(root, overrides=None):
    assets = Path(root) / "assets"
    assets.mkdir()
    for name, text in {**MINIMAL_ASSETS, **(overrides or {})}.items():
        (assets / name).write_bytes(text.encode("utf-8"))
    return assets


def c_unescape(code):
    """The text of the C string literal(s) in `code` (adjacent ones concatenated); only the
    escapes \\" and \\\\ are understood -- anything else fails."""
    pieces = re.findall(r'"((?:[^"\\\n]|\\.)*)"', code)
    if not pieces:
        raise ValueError(f"no string literal in {code!r}")
    out = []
    for p in pieces:
        for esc in re.findall(r"\\(.)", p):
            if esc not in ('"', "\\"):
                raise ValueError(f"unexpected escape \\{esc} in {code!r}")
        out.append(re.sub(r"\\(.)", r"\1", p))
    return "".join(out)


class EmbedTextTest(ToolTestCase):
    # Scenario: CSS z pliku traci komentarze, wcięcia i końce linii, a reszta zostaje bez zmian
    def test_css_comments_indentation_and_line_breaks_are_dropped(self):
        css = ("/* Colours follow the display */\n"
               ":root{--accent:#ff9966}\n"
               "\n"
               "body{margin:0;\n"
               "  font:15px/1.5 system-ui,'Segoe UI',sans-serif}\n"
               "    .wrap{max-width:440px}   \n")
        self.assertEqual(":root{--accent:#ff9966}body{margin:0;"
                         "font:15px/1.5 system-ui,'Segoe UI',sans-serif}.wrap{max-width:440px}",
                         self.tool().embed_text(css, "css", "style.css"))

    # Scenario: JS z pliku traci linie komentarza, ale nie // w napisie
    def test_js_comment_lines_are_dropped_and_code_kept(self):
        js = ("// Tab switching\n"
              "function show(t){\n"
              "  var u='http://example.com/a';\n"
              "    // the hidden field follows the tab\n"
              "  $('tab').value=t;\n"
              "}\n"
              "show(1);\n")
        self.assertEqual("function show(t){var u='http://example.com/a';$('tab').value=t;}show(1);",
                         self.tool().embed_text(js, "js", "app.js"))

    # Scenario Outline: Linia, której sklejenie z następną skleiłoby dwa słowa, przerywa
    # generowanie (the CLI writes nothing and names file:line)
    def test_lines_that_would_glue_two_tokens_are_an_error(self):
        examples = [
            ("app.js", "// helpers\nfunction f(x){return\nx}\n"),
            ("app.js", "// counters\nvar a=1\nb=2;\n"),
            ("style.css", "/* box */\np{margin:0\nauto}\n"),
        ]
        for name, text in examples:
            with self.subTest(file=name, text=text):
                self.assertEmbedError(lambda t: t.embed_text, text, Path(name).suffix[1:], name,
                                      where=f"{name}:2")
                with tempfile.TemporaryDirectory() as tmp:
                    assets = write_assets(tmp, {name: text})
                    out = Path(tmp) / "web_assets.h"
                    out.write_text("old\n", encoding="utf-8")
                    r = run_cli("--assets", assets, "--out", out)
                    self.assertNotEqual(0, r.returncode, r.stdout + r.stderr)
                    self.assertIn(f"{name}:2", r.stdout + r.stderr)
                    self.assertEqual("old\n", out.read_text(encoding="utf-8"),
                                     "the header was written despite the error")

    # Edge case: a checkout with CRLF line endings (core.autocrlf) gives the same text.
    def test_crlf_input_gives_the_same_output(self):
        tool = self.tool()
        css = "/* c */\na{b:c;\n  d:e}\n\n.f{g:h}\n"
        js = "// c\nfunction f(){\n  return 1;\n}\n"
        self.assertEqual(tool.embed_text(css, "css", "style.css"),
                         tool.embed_text(css.replace("\n", "\r\n"), "css", "style.css"))
        self.assertEqual(tool.embed_text(js, "js", "app.js"),
                         tool.embed_text(js.replace("\n", "\r\n"), "js", "app.js"))
        with tempfile.TemporaryDirectory() as lf, tempfile.TemporaryDirectory() as crlf:
            write_assets(lf)
            write_assets(crlf, {n: t.replace("\n", "\r\n") for n, t in MINIMAL_ASSETS.items()})
            self.assertEqual(tool.generate_header(Path(lf) / "assets"),
                             tool.generate_header(Path(crlf) / "assets"))

    # Edge case: a comment after code on the same line isn't supported -- an error, not a guess.
    def test_trailing_comment_after_code_is_an_error(self):
        self.assertEmbedError(lambda t: t.embed_text, "var a=1;\nshow(a); // go\n", "js",
                              "app.js", where="app.js:2")
        self.assertEmbedError(lambda t: t.embed_text, "a{b:c} /* note */\n", "css",
                              "style.css", where="style.css:1")

    # Edge case: CSS lines are joined with nothing in between, so a line break inside a selector
    # (`.card` + `.alert` -> `.card.alert`) or a value (`calc(1px` + `+ 2px)`) would change or
    # break the rule without gluing two words. Every kept CSS line but the last ends a rule
    # piece: `;`, `{`, `}` or `,`.
    def test_css_line_not_ending_a_rule_piece_is_an_error(self):
        bad = [
            ("/* cards */\n.card\n.alert{color:red}\n", "style.css:2"),
            ("a{width:calc(1px\n+ 2px)}\n", "style.css:1"),
            ("a{b:c}\n#id\n[x]{d:e}\n", "style.css:2"),
            ("a:hover\n/* note */\n:focus{b:c}\n", "style.css:1"),
            # harmless when joined, but the rule stays simple: end the declaration with `;`
            ("a{\n  b:c\n}\n", "style.css:2"),
        ]
        for text, where in bad:
            with self.subTest(text=text):
                self.assertEmbedError(lambda t: t.embed_text, text, "css", "style.css",
                                      where=where)
        ok = "a,\nb{\n  c:d;\n  e:f;\n}\n/* last */\n.g{h:i}"
        self.assertEqual("a,b{c:d;e:f;}.g{h:i}", self.tool().embed_text(ok, "css", "style.css"))

    # Edge case: a file saved with a UTF-8 BOM (some Windows editors add one) gives the same
    # header as without -- otherwise U+FEFF lands in the icon's data URI or in front of the
    # first CSS rule, and the icon / the rule is lost without an error.
    def test_utf8_bom_is_dropped(self):
        tool = self.tool()
        overrides = {"style.css": ":root{--a:#000}\nbody{margin:0}\n"}
        with tempfile.TemporaryDirectory() as plain, tempfile.TemporaryDirectory() as bom:
            write_assets(plain, overrides)
            write_assets(bom, {n: "﻿" + t
                               for n, t in {**MINIMAL_ASSETS, **overrides}.items()})
            header = tool.generate_header(Path(bom) / "assets")
            self.assertEqual(tool.generate_header(Path(plain) / "assets"), header)
            self.assertNotIn("﻿", header)


class EmbedSvgTest(ToolTestCase):
    # Scenario: Ikona SVG staje się adresem data: z apostrofami i %23 zamiast #
    def test_svg_becomes_a_single_quoted_data_uri(self):
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">\n'
               '  <rect width="32" height="32" rx="8" fill="#1d1f24"/>\n'
               '  <path fill="#ff9966" d="M5 18.5l2-1"/>\n'
               '</svg>\n')
        self.assertEqual("data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' "
                         "viewBox='0 0 32 32'><rect width='32' height='32' rx='8' "
                         "fill='%231d1f24'/><path fill='%23ff9966' d='M5 18.5l2-1'/></svg>",
                         self.tool().svg_data_uri(svg))

    # Edge case: the project icon gives exactly the URI the panel had before stage 6.
    def test_project_icon_gives_the_panels_current_data_uri(self):
        svg = (ROOT / "site" / "favicon.svg").read_text(encoding="utf-8")
        self.assertEqual(PANEL_ICON_URI, self.tool().svg_data_uri(svg))


class CLiteralTest(ToolTestCase):
    # Scenario: Tekst w nagłówku C jest literałem, który kompilator odczyta z powrotem dokładnie
    def test_quotes_and_backslashes_escaped_utf8_kept(self):
        text = "a\"b\\c'd ≈ · … $('x').y=/-full/i"
        lit = self.tool().c_literal(text)
        self.assertTrue(lit.startswith('"') and lit.endswith('"'), lit)
        self.assertIn('a\\"b', lit)
        self.assertIn("b\\\\c", lit)
        self.assertIn("c'd ≈ · …", lit)  # apostrophe and UTF-8 as they are
        self.assertNotIn("\\x", lit)
        self.assertNotIn("\n", lit)
        self.assertEqual(text, c_unescape(lit))


class CliTest(ToolTestCase):
    # Scenario: Tryb --check zgłasza nieaktualny nagłówek, a zwykły przebieg nie przepisuje
    # aktualnego
    def test_check_reports_a_stale_header_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = write_assets(tmp)
            out = Path(tmp) / "web_assets.h"
            r = run_cli("--assets", assets, "--out", out)
            self.assertEqual(0, r.returncode, r.stdout + r.stderr)
            self.assertTrue(out.is_file(), "the first run writes the header")
            r = run_cli("--assets", assets, "--out", out, "--check")
            self.assertEqual(0, r.returncode, "--check on a fresh header: " + r.stdout + r.stderr)

            (assets / "style.css").write_text("body{margin:0;padding:0}\n", encoding="utf-8")
            before = out.read_bytes()
            stamp = 1_000_000_000 * 10**9
            os.utime(out, ns=(stamp, stamp))
            r = run_cli("--assets", assets, "--out", out, "--check")
            self.assertEqual(1, r.returncode, r.stdout + r.stderr)
            self.assertIn("web_assets.h", r.stdout + r.stderr)
            self.assertEqual(before, out.read_bytes(), "--check changed the header")
            self.assertEqual(stamp, out.stat().st_mtime_ns, "--check touched the header")

    def test_unchanged_header_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = write_assets(tmp)
            out = Path(tmp) / "web_assets.h"
            self.assertEqual(0, run_cli("--assets", assets, "--out", out).returncode)
            (assets / "style.css").write_text("body{margin:0;padding:0}\n", encoding="utf-8")
            before = out.read_bytes()

            r = run_cli("--assets", assets, "--out", out)  # stale: written
            self.assertEqual(0, r.returncode, r.stdout + r.stderr)
            self.assertNotEqual(before, out.read_bytes(), "the stale header wasn't regenerated")
            self.assertIn("padding:0", out.read_text(encoding="utf-8"))

            stamp = 1_000_000_000 * 10**9
            os.utime(out, ns=(stamp, stamp))
            after = out.read_bytes()
            r = run_cli("--assets", assets, "--out", out)  # up to date: not touched
            self.assertEqual(0, r.returncode, r.stdout + r.stderr)
            self.assertEqual(after, out.read_bytes())
            self.assertEqual(stamp, out.stat().st_mtime_ns,
                             "an up-to-date header was rewritten (everything would rebuild)")

    # Edge case: a missing asset is an error naming it; nothing is written.
    def test_missing_asset_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = write_assets(tmp)
            (assets / "app.js").unlink()
            tool = self.tool()
            with self.assertRaises(tool.EmbedError) as cm:
                tool.generate_header(assets)
            self.assertIn("app.js", str(cm.exception))
            out = Path(tmp) / "web_assets.h"
            r = run_cli("--assets", assets, "--out", out)
            self.assertNotEqual(0, r.returncode)
            self.assertIn("app.js", r.stdout + r.stderr)
            self.assertFalse(out.exists(), "a header was written without app.js")


def platformio_ini():
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(ROOT / "platformio.ini", encoding="utf-8")
    return cp


def quoted_includes(text, name):
    return re.findall(r'(?m)^[ \t]*#[ \t]*include[ \t]*"' + re.escape(name) + '"', text)


class RepoTest(ToolTestCase):
    # Scenario: Nagłówek w repozytorium odpowiada plikom z src/admin/assets/
    def test_checked_in_header_matches_the_assets(self):
        for name in ASSET_FILES:
            self.assertTrue((ASSETS / name).is_file(), f"src/admin/assets/{name}")
        expected = self.tool().generate_header(ASSETS)
        actual = HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(expected, actual, "src/admin/core/web_assets.h is stale: "
                                           "run python tools/embed_assets.py")
        self.assertIn("src/admin/assets", "\n".join(actual.splitlines()[:5]),
                      "the generated-file banner")
        for name in ASSET_NAMES:
            self.assertRegex(actual, r"\bstatic const char " + name + r"\[\] =")

    # Edge case: the joined JS parses. The glue check catches word + word only; a missing `;`
    # after a function expression (`}` + `x.onerror=...`) or `a=b` + `++c` is a SyntaxError
    # that native tests and the HTML comparison never run. Each asset is checked on its own,
    # wrapped in a function (the page puts it after `var T={...};`). Skipped without node
    # (GitHub's runners have it).
    @unittest.skipUnless(shutil.which("node"), "node not on PATH")
    def test_joined_js_assets_parse(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / "broken.js"
            broken.write_text("function f(){var x={};x.a=function(){}x.b=1;}\n",
                              encoding="utf-8")
            r = subprocess.run(["node", "--check", str(broken)], capture_output=True, text=True)
            self.assertNotEqual(0, r.returncode, "node --check accepted a SyntaxError")
            for name in ("app.js", "restart.js", "check_pending.js"):
                with self.subTest(asset=name):
                    text = (ASSETS / name).read_bytes().decode("utf-8-sig")
                    joined = tool.embed_text(text, "js", name)
                    out = Path(tmp) / name
                    out.write_text("function page(){var T={};" + joined + "\n}\n",
                                   encoding="utf-8")
                    r = subprocess.run(["node", "--check", str(out)], capture_output=True,
                                       text=True)
                    self.assertEqual(0, r.returncode, f"{name} joined: {r.stderr}")

    # Scenario: Kompilacja firmware'u i testów natywnych odświeża nagłówek, a CI sprawdza go
    # testem narzędzi
    def test_cyd_and_native_generate_the_header_before_the_build(self):
        ini = platformio_ini()
        cyd = ini["env:cyd"].get("extra_scripts", "").split()
        self.assertIn("pre:tools/pio_version.py", cyd)
        self.assertIn("pre:tools/embed_assets.py", cyd)
        self.assertIn("pre:tools/embed_assets.py", ini["env:native"].get("extra_scripts", "").split())
        # cyd-ota and native-asan inherit theirs.
        self.assertEqual("env:cyd", ini["env:cyd-ota"].get("extends"))
        self.assertNotIn("extra_scripts", ini["env:cyd-ota"])
        self.assertEqual("env:native", ini["env:native-asan"].get("extends"))
        self.assertNotIn("extra_scripts", ini["env:native-asan"])

    def test_ci_runs_the_embed_assets_tests(self):
        text = (ROOT / ".github" / "workflows" / "firmware.yml").read_text(encoding="utf-8")
        steps = re.split(r"(?m)^\s*- name:", text)
        tool_tests = [s for s in steps if "python tools/test_check_bdd.py" in s]
        self.assertEqual(1, len(tool_tests), "the tool-test step of firmware.yml")
        self.assertIn("python tools/test_embed_assets.py", tool_tests[0])

    # Scenario: Zasoby są w firmware raz, a config_page.cpp nie ma już własnego CSS ani JS
    def test_only_config_page_includes_the_assets(self):
        def includers(d):
            return sorted(p.relative_to(ROOT).as_posix() for p in sorted((ROOT / d).rglob("*"))
                          if p.is_file() and p.suffix in (".h", ".hpp", ".c", ".cpp")
                          and quoted_includes(p.read_text(encoding="utf-8", errors="replace"),
                                              "admin/core/web_assets.h"))
        self.assertEqual(["src/admin/core/config_page.cpp"], includers("src"))
        self.assertLessEqual(set(includers("test")), {"test/test_admin/config_page_test.cpp"})

    def test_config_page_keeps_no_css_or_js_literals(self):
        text = strip_comments((ROOT / "src" / "admin" / "core" / "config_page.cpp").read_text(
            encoding="utf-8"))
        literals = re.findall(r'"((?:[^"\\\n]|\\.)*)"', text)
        self.assertGreater(len(literals), 100, "found too few string literals in config_page.cpp")
        css = [s for s in literals if re.search(r"[\w\]\)*.#-]\{[\w-]+:", s)]
        js = [s for s in literals if re.search(r"\bfunction\s*\w*\s*\(", s)]
        icon = [s for s in literals if "<svg" in s or "data:image/svg" in s]
        self.assertEqual([], css, "CSS rules left in config_page.cpp")
        self.assertEqual([], js, "JS functions left in config_page.cpp")
        self.assertEqual([], icon, "the icon is still written by hand in config_page.cpp")
        joined = "".join(literals)
        for tag in ("<style>", "</style>", "<script>var T={", "</script>"):
            self.assertIn(tag, joined)

    # Edge case: the panel's icon stays the project page's icon.
    def test_panel_icon_matches_the_project_page_icon(self):
        panel = ASSETS / "favicon.svg"
        self.assertTrue(panel.is_file(), "src/admin/assets/favicon.svg")
        site = ROOT / "site" / "favicon.svg"
        self.assertEqual(site.read_text(encoding="utf-8").replace("\r\n", "\n"),
                         panel.read_text(encoding="utf-8").replace("\r\n", "\n"))


if __name__ == "__main__":
    unittest.main()
