"""Tests for tools/check_layers.py:  python tools/test_check_layers.py

The rule tests feed the pure functions {relpath: text} dicts (the included targets are in the
dict too, so they resolve); LayoutTest / BuildConfigTest / WorkflowTest read this repository
and pin the state spec 12 stage 1 leaves behind.
"""

import configparser
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_layers  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(__file__).resolve().parent / "check_layers.py"
SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}

# `grep -ro "RUN_TEST(" test --include=*.cpp | wc -l` on main before stage 1 (plan step 0).
RUN_TEST_BASELINE = 874


def show(findings, limit=30):
    lines = [check_layers.format_finding(f) for f in findings[:limit]]
    if len(findings) > limit:
        lines.append(f"... and {len(findings) - limit} more")
    return "\n" + "\n".join(lines)


def with_target(source, include, target_path, line=1):
    """{source: text with `#include "include"` on `line`, target_path: ""}."""
    text = "// x\n" * (line - 1) + f'#include "{include}"\n'
    return {source: text, target_path: ""}


def target_of(include):
    """Where a full-path include lives in the dict trees below."""
    return include if include.startswith("include/") else "src/" + include


def run_cli(root, *args):
    proc = subprocess.run([sys.executable, str(TOOL), "--root", str(root), *args],
                          capture_output=True)
    return proc.returncode, proc.stdout.decode("utf-8", "replace"), proc.stdout


def write_tree(root, files):
    for rel, text in files.items():
        p = Path(root) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")


# ---- classification -------------------------------------------------------------------------

class ClassifyTest(unittest.TestCase):
    def test_contexts_layers_and_roots(self):
        cases = {
            "src/shared/sun_times.h": "shared",
            "src/platform/core_link.cpp": "platform",
            "src/app/main.cpp": "app",
            "src/app/net_task.h": "app",
            "src/airspace/domain/flight_phase.h": "airspace/domain",
            "src/airspace/app/poll_pipeline.cpp": "airspace/app",
            "src/enrichment/adapters/hexdb_http.h": "enrichment/adapters",
            "src/opensky/domain/opensky_client.cpp": "opensky/domain",
            "src/display/domain/screens/table_view.h": "display/domain",
            "src/display/domain/theme/fonts/lcars_efont.c": "display/domain",
            "src/display/app/screenshot.cpp": "display/app",
            "src/admin/domain/firmware/version.cpp": "admin/domain",
            "src/admin/app/config_portal.cpp": "admin/app",
            "src/admin/adapters/github_ca.h": "admin/adapters",
            "include/LGFX_CYD.hpp": "display/adapters",
            "include/touch_calibration.h": "display/domain",
            "test/support/fixtures.h": "test",
            "test/test_admin/github_fixtures.h": "test",
            "test/test_display/table_view_test.cpp": "test",
        }
        for path, layer in cases.items():
            with self.subTest(path=path):
                self.assertEqual(layer, check_layers.classify(path))

    def test_unknown_context_or_missing_layer_is_outside(self):
        for path in ("src/stray.cpp", "src/display/stray.cpp", "src/weather/domain/x.h",
                     "src/display/views/x.h", "lib/x.h"):
            with self.subTest(path=path):
                self.assertIsNone(check_layers.classify(path))


# ---- §5 table -------------------------------------------------------------------------------

class RuleTableTest(unittest.TestCase):
    ALLOWED = [
        ("src/airspace/domain/", "shared/sun_times.h"),
        ("src/opensky/domain/", "airspace/domain/flight_phase.h"),
        ("src/display/domain/", "enrichment/domain/aircraft_names.h"),
        ("src/display/domain/", "admin/domain/device_alerts.h"),
        ("src/admin/domain/", "display/domain/backlight.h"),
        ("src/airspace/app/", "enrichment/domain/aircraft_lookup.h"),
        ("src/display/app/", "platform/core_link.h"),
        ("src/platform/", "display/domain/screens/table_view.h"),
        ("src/enrichment/adapters/", "enrichment/domain/lookup_store.h"),
        ("src/app/", "enrichment/adapters/hexdb_http.h"),
        ("test/test_display/", "display/domain/theme/lcars_theme.h"),
    ]

    FORBIDDEN = [
        ("src/display/domain/", "opensky/domain/opensky_quota.h",
         "display/domain -> opensky/domain"),
        ("src/enrichment/domain/", "opensky/domain/opensky_auth.h",
         "enrichment/domain -> opensky/domain"),
        ("src/airspace/domain/", "display/domain/screens/table_view.h",
         "airspace/domain -> display/domain"),
        ("src/opensky/domain/", "enrichment/adapters/hexdb_http.h",
         "opensky/domain -> enrichment/adapters"),
        ("src/admin/domain/", "admin/app/config_page.h", "admin/domain -> admin/app"),
        ("src/airspace/app/", "enrichment/app/x.h", "airspace/app -> enrichment/app"),
        ("src/display/domain/", "app/net_task.h", "display/domain -> app"),
        ("src/display/domain/", "LGFX_CYD.hpp", "display/domain -> display/adapters"),
        ("src/shared/", "platform/core_link.h", "shared -> platform"),
    ]

    # Scenario Outline: Include dozwolony tabelą z §5 nie jest zgłaszany
    def test_allowed_includes_are_not_reported(self):
        for who, target in self.ALLOWED:
            with self.subTest(who=who, target=target):
                source = who + "x.cpp"
                # Guard against a vacuous pass: the file has to be in a known layer at all.
                self.assertIsNotNone(check_layers.classify(source), source)
                files = with_target(source, target, target_of(target))
                found = check_layers.check_files(files)
                self.assertEqual([], found, show(found))

    # Scenario Outline: Include zakazany tabelą z §5 jest zgłaszany z plikiem, linią i parą warstw
    def test_forbidden_includes_are_reported_with_file_and_line(self):
        for who, target, pair in self.FORBIDDEN:
            with self.subTest(who=who, target=target):
                source = who + "x.cpp"
                path = ("include/" + target) if target.endswith(".hpp") else "src/" + target
                found = check_layers.check_files(with_target(source, target, path, line=3))
                self.assertEqual(1, len(found), show(found))
                self.assertEqual(source, found[0].path)
                self.assertEqual(3, found[0].line)
                text = check_layers.format_finding(found[0])
                self.assertTrue(text.startswith(f"{source}:3: {pair} "), text)
                self.assertIn(target, text)

    # Scenario: Test nie dołącza adapterów ani korzeni kompozycji
    def test_test_files_may_not_include_adapters_or_roots(self):
        files = {
            "test/test_enrichment/x_test.cpp": '#include "enrichment/adapters/hexdb_http.h"\n'
                                               '#include "app/net_task.h"\n',
            "src/enrichment/adapters/hexdb_http.h": "",
            "src/app/net_task.h": "",
        }
        found = check_layers.check_files(files)
        texts = [check_layers.format_finding(f) for f in found]
        self.assertEqual(2, len(found), show(found))
        self.assertTrue(texts[0].startswith("test/test_enrichment/x_test.cpp:1: "
                                            "test -> enrichment/adapters "), texts[0])
        self.assertTrue(texts[1].startswith("test/test_enrichment/x_test.cpp:2: test -> app "),
                        texts[1])

    # Scenario: Plik poza katalogami kontekstów jest zgłaszany
    def test_file_outside_any_context_is_reported(self):
        for path in ("src/stray.cpp", "src/display/stray.cpp"):
            with self.subTest(path=path):
                self.assertIsNone(check_layers.classify(path))
                found = check_layers.check_files({path: "int x;\n"})
                self.assertEqual(1, len(found), show(found))
                self.assertEqual(path, found[0].path)
                self.assertIn("outside", found[0].message)

    def test_unresolved_quoted_include_is_reported(self):
        found = check_layers.check_files(
            {"src/display/domain/x.cpp": '#include "shared/nowhere.h"\n'})
        self.assertEqual(1, len(found), show(found))
        self.assertEqual(1, found[0].line)
        self.assertIn("shared/nowhere.h", check_layers.format_finding(found[0]))

    def test_adapters_see_their_own_context_and_platform_only(self):
        source = "src/enrichment/adapters/x.cpp"
        files = {
            source: '#include "enrichment/app/enrichment_service.h"\n'
                    '#include "enrichment/domain/lookup_store.h"\n'
                    '#include "platform/core_link.h"\n'
                    '#include "shared/sun_times.h"\n'
                    '#include <HTTPClient.h>\n'
                    '#include "opensky/domain/opensky_auth.h"\n'
                    '#include "display/adapters/y.h"\n',
            "src/enrichment/app/enrichment_service.h": "",
            "src/enrichment/domain/lookup_store.h": "",
            "src/platform/core_link.h": "",
            "src/shared/sun_times.h": "",
            "src/opensky/domain/opensky_auth.h": "",
            "src/display/adapters/y.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([6, 7], [f.line for f in found], show(found))
        self.assertIn("enrichment/adapters -> opensky/domain", found[0].message)
        self.assertIn("enrichment/adapters -> display/adapters", found[1].message)

    def test_app_layer_sees_its_own_app_but_not_another_contexts(self):
        files = {
            "src/display/app/x.cpp": '#include "display/app/y.h"\n#include "admin/app/z.h"\n',
            "src/display/app/y.h": "",
            "src/admin/app/z.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([2], [f.line for f in found], show(found))
        self.assertIn("display/app -> admin/app", found[0].message)

    def test_enrichment_app_may_implement_the_airspace_port(self):
        files = {
            "src/enrichment/app/x.cpp": '#include "airspace/app/poll_pipeline.h"\n'
                                        '#include "airspace/domain/flight_phase.h"\n'
                                        '#include "enrichment/domain/lookup_cache.h"\n'
                                        '#include "opensky/domain/opensky_auth.h"\n',
            "src/airspace/app/poll_pipeline.h": "",
            "src/airspace/domain/flight_phase.h": "",
            "src/enrichment/domain/lookup_cache.h": "",
            "src/opensky/domain/opensky_auth.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([4], [f.line for f in found], show(found))
        self.assertIn("enrichment/app -> opensky/domain", found[0].message)

    def test_test_files_may_include_support_and_their_own_headers(self):
        files = {
            "test/test_admin/x_test.cpp": '#include "test_support.h"\n'
                                          '#include "github_fixtures.h"\n'
                                          '#include "admin/app/config_page.h"\n'
                                          '#include "platform/core_link.h"\n',
            "test/support/test_support.h": "",
            "test/test_admin/github_fixtures.h": "",
            "src/admin/app/config_page.h": "",
            "src/platform/core_link.h": "",
        }
        self.assertEqual("test", check_layers.classify("test/test_admin/x_test.cpp"))
        found = check_layers.check_files(files)
        self.assertEqual([], found, show(found))


# ---- SDK includes ---------------------------------------------------------------------------

class SdkIncludeTest(unittest.TestCase):
    # Scenario: platform/ dołącza SDK tylko pod #ifdef ARDUINO
    def test_sdk_include_in_platform_only_inside_ifdef_arduino(self):
        text = ("// platform\n"
                "#ifdef ARDUINO\n"
                "#include <freertos/queue.h>\n"
                "#endif\n"
                "#include <freertos/queue.h>\n")
        found = check_layers.check_files({"src/platform/x.cpp": text})
        self.assertEqual(1, len(found), show(found))
        self.assertEqual(5, found[0].line)
        self.assertTrue(check_layers.format_finding(found[0])
                        .startswith("src/platform/x.cpp:5: platform -> sdk "), show(found))

    # Scenario Outline: domain/ i app/ nie dołączają SDK, także pod #ifdef ARDUINO
    def test_sdk_include_in_domain_is_reported_even_inside_ifdef(self):
        rows = [
            ("src/display/domain/", "Arduino.h", "display/domain"),
            ("src/opensky/domain/", "HTTPClient.h", "opensky/domain"),
            ("src/enrichment/domain/", "Preferences.h", "enrichment/domain"),
            ("src/admin/app/", "WebServer.h", "admin/app"),
            ("src/display/domain/", "LovyanGFX.hpp", "display/domain"),
        ]
        for who, sdk, layer in rows:
            with self.subTest(who=who, sdk=sdk):
                source = who + "x.cpp"
                text = f"#ifdef ARDUINO\n#include <{sdk}>\n#endif\n"
                found = check_layers.check_files({source: text})
                self.assertEqual(1, len(found), show(found))
                self.assertTrue(check_layers.format_finding(found[0])
                                .startswith(f"{source}:2: {layer} -> sdk "), show(found))

    # Scenario: Nagłówki standardowe i ArduinoJson wolno dołączać wszędzie
    def test_standard_and_arduinojson_headers_allowed_everywhere(self):
        text = ("#include <vector>\n#include <cstdint>\n#include <atomic>\n"
                "#include <ArduinoJson.h>\n#include <unity.h>\n")
        for source in ("src/shared/x.cpp", "src/airspace/domain/x.cpp",
                       "test/test_airspace/x_test.cpp"):
            with self.subTest(source=source):
                self.assertIsNotNone(check_layers.classify(source), source)
                found = check_layers.check_files({source: text})
                self.assertEqual([], found, show(found))

    def test_ifndef_arduino_else_branch_counts_as_inside(self):
        text = ("#ifndef ARDUINO\n#include <cstdint>\n#else\n#include <freertos/queue.h>\n"
                "#endif\n")
        self.assertEqual("platform", check_layers.classify("src/platform/x.cpp"))
        found = check_layers.check_files({"src/platform/x.cpp": text})
        self.assertEqual([], found, show(found))

    def test_adapters_and_roots_may_include_the_sdk(self):
        for source in ("src/display/adapters/x.cpp", "src/app/main.cpp"):
            with self.subTest(source=source):
                self.assertIsNotNone(check_layers.classify(source))
                found = check_layers.check_files(
                    {source: "#include <Arduino.h>\n#include <WiFi.h>\n#include <esp_ota_ops.h>\n"})
                self.assertEqual([], found, show(found))

    def test_tests_may_not_include_the_sdk(self):
        found = check_layers.check_files({"test/test_platform/x_test.cpp": "#include <Arduino.h>\n"})
        self.assertEqual(1, len(found), show(found))
        self.assertIn("test -> sdk", found[0].message)

    def test_sdk_header_list(self):
        sdk = ["Arduino.h", "WiFi.h", "WiFiClientSecure.h", "WiFiManager.h", "HTTPClient.h",
               "Preferences.h", "WebServer.h", "ESPmDNS.h", "Update.h", "LovyanGFX.hpp",
               "lgfx/v1/panel/Panel_ILI9341.hpp", "esp_ota_ops.h", "freertos/FreeRTOS.h",
               "mbedtls/sha256.h"]
        for name in sdk:
            with self.subTest(name=name):
                found = check_layers.check_files({"src/shared/x.cpp": f"#include <{name}>\n"})
                self.assertEqual(1, len(found), show(found))
                self.assertIn("shared -> sdk", found[0].message)


# ---- #include parsing -----------------------------------------------------------------------

class FindIncludesTest(unittest.TestCase):
    def names(self, text):
        return [(i.line, i.name, i.angle, i.arduino) for i in check_layers.find_includes(text)]

    def test_quoted_and_angle_includes_with_spaces_after_hash(self):
        text = '#include "shared/sun_times.h"\n#  include <vector>\n  # include "a/b.h"  // c\n'
        self.assertEqual([(1, "shared/sun_times.h", False, False), (2, "vector", True, False),
                          (3, "a/b.h", False, False)], self.names(text))

    def test_commented_out_includes_are_ignored(self):
        text = ('// #include "nowhere.h"\n'
                '/* #include "gone.h" */\n'
                '/*\n#include "also_gone.h"\n*/\n'
                '#include "shared/sun_times.h"\n')
        self.assertEqual([(6, "shared/sun_times.h", False, False)], self.names(text))

    def test_ifdef_arduino_else_endif(self):
        text = ("#ifdef ARDUINO\n#include <A.h>\n#else\n#include <B.h>\n#endif\n"
                "#include <C.h>\n")
        self.assertEqual([(2, "A.h", True, True), (4, "B.h", True, False),
                          (6, "C.h", True, False)], self.names(text))

    def test_ifndef_arduino_and_if_defined(self):
        text = ("#ifndef ARDUINO\n#include <B.h>\n#else\n#include <A.h>\n#endif\n"
                "#if defined(ARDUINO)\n#include <D.h>\n#endif\n")
        self.assertEqual([(2, "B.h", True, False), (4, "A.h", True, True),
                          (7, "D.h", True, True)], self.names(text))

    def test_nested_if_inside_ifdef_arduino_keeps_the_state(self):
        text = ("#ifdef ARDUINO\n#if CONFIG_X\n#include <A.h>\n#else\n#include <E.h>\n#endif\n"
                "#include <B.h>\n#endif\n#include <C.h>\n")
        self.assertEqual([(3, "A.h", True, True), (5, "E.h", True, True),
                          (7, "B.h", True, True), (9, "C.h", True, False)], self.names(text))

    def test_other_conditions_are_not_arduino(self):
        text = "#ifdef UNIT_TEST\n#include <A.h>\n#else\n#include <B.h>\n#endif\n"
        self.assertEqual([(2, "A.h", True, False), (4, "B.h", True, False)], self.names(text))

    def test_unterminated_ifdef_does_not_crash(self):
        self.assertEqual([(2, "A.h", True, True)],
                         self.names("#ifdef ARDUINO\n#include <A.h>\n"))
        self.assertEqual([(2, "A.h", True, False)], self.names("#endif\n#include <A.h>\n"))


class ResolveTest(unittest.TestCase):
    KNOWN = {"src/platform/core_link.h", "include/LGFX_CYD.hpp", "test/support/fixtures.h",
             "test/test_admin/github_fixtures.h", "src/display/domain/screens/table_view.h"}

    def test_full_path_from_src_include_and_test_support(self):
        r = check_layers.resolve_include
        self.assertEqual("src/platform/core_link.h",
                         r("src/app/main.cpp", "platform/core_link.h", self.KNOWN))
        self.assertEqual("include/LGFX_CYD.hpp", r("src/app/main.cpp", "LGFX_CYD.hpp", self.KNOWN))
        self.assertEqual("test/support/fixtures.h",
                         r("test/test_admin/x_test.cpp", "fixtures.h", self.KNOWN))

    def test_own_directory_only_for_tests(self):
        r = check_layers.resolve_include
        self.assertEqual("test/test_admin/github_fixtures.h",
                         r("test/test_admin/x_test.cpp", "github_fixtures.h", self.KNOWN))
        self.assertIsNone(r("test/test_display/x_test.cpp", "github_fixtures.h", self.KNOWN))
        self.assertIsNone(r("src/display/domain/screens/x.cpp", "table_view.h", self.KNOWN))

    def test_short_include_in_src_is_a_full_path_finding(self):
        files = {"src/display/domain/screens/x.cpp": '#include "table_view.h"\n'
                                                     '#include "display/domain/screens/table_view.h"\n',
                 "src/display/domain/screens/table_view.h": ""}
        found = check_layers.full_path_findings(files)
        self.assertEqual([1], [f.line for f in found], show(found))
        self.assertEqual("src/display/domain/screens/x.cpp", found[0].path)


# ---- CLI ------------------------------------------------------------------------------------

TWO_VIOLATIONS = {
    "src/display/domain/a.cpp": '#include "opensky/domain/q.h"\n',
    "src/shared/b.cpp": '#include "shared/b.h"\n#include "platform/core_link.h"\n',
    "src/shared/b.h": "",
    "src/opensky/domain/q.h": "",
    "src/platform/core_link.h": "",
}


class CliTest(unittest.TestCase):
    # Scenario: W trybie raportu naruszenia są wypisane, ale wynik to 0
    def test_report_mode_exits_zero_with_violations(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, TWO_VIOLATIONS)
            code, out, _ = run_cli(tmp)
        out = out.replace("\\", "/")
        self.assertIn("src/display/domain/a.cpp:1: display/domain -> opensky/domain", out)
        self.assertIn("src/shared/b.cpp:2: shared -> platform", out)
        summary = [ln for ln in out.splitlines() if "total" in ln.lower()]
        self.assertEqual(1, len(summary), out)
        self.assertRegex(summary[0], r"\b2\b")
        self.assertEqual(0, code)

    # Scenario: Z --strict naruszenie kończy się kodem 1, czyste drzewo kodem 0
    def test_strict_mode_exits_one_with_violations(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/shared/b.cpp": '#include "platform/core_link.h"\n',
                             "src/platform/core_link.h": ""})
            dirty, out, _ = run_cli(tmp, "--strict")
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/platform/b.cpp": '#include "shared/s.h"\n',
                             "src/shared/s.h": ""})
            clean, out_clean, _ = run_cli(tmp, "--strict")
        self.assertEqual(1, dirty, out)
        self.assertEqual(0, clean, out_clean)

    def test_summary_counts_per_pair_and_output_is_ascii(self):
        files = dict(TWO_VIOLATIONS)
        files["src/display/domain/c.cpp"] = '#include "opensky/domain/q.h"\n'
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, files)
            code, out, raw = run_cli(tmp)
        raw.decode("ascii")  # UnicodeDecodeError = non-ASCII output (Windows console code page)
        pair_lines = [ln for ln in out.splitlines()
                      if "display/domain -> opensky/domain" in ln and not ln.startswith("src")]
        self.assertEqual(1, len(pair_lines), out)
        self.assertRegex(pair_lines[0], r"\b2\b")
        self.assertEqual(0, code)


# ---- this repository ------------------------------------------------------------------------

def repo_sources(*dirs):
    return {p.relative_to(ROOT).as_posix(): p.read_text(encoding="utf-8", errors="replace")
            for d in dirs for p in sorted((ROOT / d).rglob("*"))
            if p.is_file() and p.suffix in SOURCE_SUFFIXES}


class LayoutTest(unittest.TestCase):
    # Scenario: Każdy plik z src/ leży w katalogu kontekstu i warstwy z §4
    def test_every_src_file_sits_in_a_context_and_layer(self):
        loose = sorted(p.name for p in (ROOT / "src").iterdir() if p.is_file())
        self.assertEqual([], loose, "files directly in src/")
        files = repo_sources("src")
        self.assertGreater(len(files), 50)
        outside = [p for p in files if check_layers.classify(p) in (None, "test")]
        self.assertEqual([], outside)

    # Scenario: Include'y w src/ i test/ podają pełną ścieżkę od src/
    def test_includes_in_src_and_test_use_full_paths(self):
        files = repo_sources("src", "test", "include")
        known = set(files)
        quoted, unresolved = 0, []
        for path, text in files.items():
            if path.startswith("include/"):
                continue
            for inc in check_layers.find_includes(text):
                if inc.angle:
                    continue
                quoted += 1
                if check_layers.resolve_include(path, inc.name, known) is None:
                    unresolved.append(f"{path}:{inc.line}: \"{inc.name}\"")
        self.assertGreater(quoted, 200, "find_includes found too few quoted includes")
        self.assertEqual([], unresolved, "\n" + "\n".join(unresolved[:40]))
        src_files = {p: t for p, t in files.items() if p.startswith("src/")}
        found = check_layers.full_path_findings(src_files)
        self.assertEqual([], found, show(found))

    # Scenario: Przeniesienie nie gubi testów
    def test_run_test_count_not_below_stage_0_baseline(self):
        count = sum(t.count("RUN_TEST(") for p, t in repo_sources("test").items()
                    if p.endswith(".cpp"))
        self.assertGreaterEqual(count, RUN_TEST_BASELINE)


def platformio_ini():
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(ROOT / "platformio.ini", encoding="utf-8")
    return cp


class BuildConfigTest(unittest.TestCase):
    # Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej
    # części platform/
    def test_native_filter_excludes_roots_and_adapters(self):
        native = platformio_ini()["env:native"]
        self.assertEqual(["+<*>", "-<app/>", "-<*/adapters/>", "-<platform/*_esp32.cpp>"],
                         native.get("build_src_filter", "").split())

    # Scenario: src/ jest na ścieżce include'ów firmware'u i testów
    def test_src_is_on_the_include_path_of_every_env(self):
        ini = platformio_ini()
        has_src = re.compile(r"(^|\s)-I\s*src(\s|$)")
        for env in ("env:cyd", "env:native"):
            with self.subTest(env=env):
                self.assertRegex(ini[env].get("build_flags", ""), has_src)
        # cyd-ota and native-asan inherit theirs.
        self.assertEqual("env:cyd", ini["env:cyd-ota"].get("extends"))
        self.assertNotIn("build_flags", ini["env:cyd-ota"])
        self.assertIn("${env:native.build_flags}", ini["env:native-asan"].get("build_flags"))


def workflow_steps(text):
    """[(name, body)] of the `- name:` steps in a workflow file."""
    steps, name, body = [], None, []
    for line in text.splitlines():
        m = re.match(r"^\s*- name:\s*(.*)$", line)
        if m:
            if name is not None:
                steps.append((name, "\n".join(body)))
            name, body = m.group(1), []
        elif name is not None:
            body.append(line)
    if name is not None:
        steps.append((name, "\n".join(body)))
    return steps


class WorkflowTest(unittest.TestCase):
    # Scenario: CI uruchamia raport warstw przed testami natywnymi, bez blokowania, i testy narzędzi
    def test_firmware_workflow_reports_layers_before_native_tests(self):
        text = (ROOT / ".github" / "workflows" / "firmware.yml").read_text(encoding="utf-8")
        steps = workflow_steps(text)
        self.assertGreater(len(steps), 5)
        layers = [i for i, (_, b) in enumerate(steps)
                  if re.search(r"python tools/check_layers\.py(?!\S)", b)]
        native = [i for i, (_, b) in enumerate(steps)
                  if re.search(r"pio test -e native(\s|$)", b)]
        self.assertTrue(layers, "no step runs python tools/check_layers.py")
        self.assertTrue(native, "no step runs pio test -e native")
        self.assertLess(layers[0], native[0])
        self.assertNotIn("--strict", steps[layers[0]][1])
        tool_tests = [b for _, b in steps if "python tools/test_check_bdd.py" in b]
        self.assertEqual(1, len(tool_tests))
        for t in ("test_check_layers.py", "test_rewrite_includes.py", "test_subset_efont.py"):
            self.assertIn(f"python tools/{t}", tool_tests[0])


if __name__ == "__main__":
    unittest.main()
