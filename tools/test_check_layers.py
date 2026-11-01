"""Tests for tools/check_layers.py:  python tools/test_check_layers.py

The rule tests feed the pure functions {relpath: text} dicts (the included targets are in the
dict too, so they resolve); LayoutTest / BuildConfigTest / WorkflowTest read this repository
and pin the state spec 12 stages 2 and 3 leave behind.
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

# Layer findings (check_files + full_path_findings) on this repository after the last finished
# stage: 51 after stage 2, 47 after stage 3 (-6 includes only the moved types needed, +2
# tracked_flight.h -> enrichment/core). A limit, not a pin: a stage that lowers the count lowers
# this number with it, so a new finding can't hide under the old limit.
LAYER_FINDINGS_LIMIT = 47

# Stage 3 (spec 12 section 8 item 3): symbol -> (new header, old header), both from src/.
STAGE_3_HOMES = {
    "AircraftRow": ("airspace/core/tracked_flight.h", "display/core/screens/table_view.h"),
    "buildEnrichedRecords": ("airspace/core/tracked_flight.h",
                             "display/core/screens/table_view.h"),
    "annotateDistances": ("airspace/core/tracked_flight.h", "display/core/screens/table_view.h"),
    "classifyPhases": ("airspace/core/tracked_flight.h", "display/core/screens/table_view.h"),
    "sortRowsForDisplay": ("airspace/core/flight_ordering.h",
                           "display/core/screens/table_view.h"),
    "sortRowsByDistance": ("airspace/core/flight_ordering.h",
                           "display/core/screens/table_view.h"),
    "splitFeaturedAndRest": ("airspace/core/flight_ordering.h",
                             "display/core/screens/table_view.h"),
    "pickNowAircraft": ("airspace/core/now_pick.h", "display/core/screens/flight_screen.h"),
    "OpenSkyHealth": ("airspace/core/feed_health.h", "display/core/screens/status_bar.h"),
    "Aircraft": ("airspace/core/aircraft_state.h", "opensky/core/opensky_client.h"),
    "inferOnGround": ("airspace/core/aircraft_state.h", "opensky/core/opensky_client.h"),
    "computeDistanceBearing": ("shared/geo.h", "display/core/screens/table_view.h"),
    "compassPoint": ("shared/geo.h", "display/core/theme/lcars_theme.h"),
    "openSkyCreditCost": ("opensky/core/scan_area.h", "admin/adapters/config_portal.h"),
    "bboxAreaSqDeg": ("opensky/core/scan_area.h", "admin/adapters/config_portal.h"),
    "utf8Length": ("display/core/text/utf8.h", "display/core/theme/lcars_theme.h"),
    "fitToWidth": ("display/core/text/fit_to_width.h", "display/core/theme/lcars_theme.h"),
    "isFontSafeText": ("display/core/text/font_set.h", "display/core/theme/lcars_theme.h"),
}

# Stage 3: the 49 tests that follow their code, test -> (new file, old file), both from test/.
_TV = "test_display/table_view_test.cpp"
_LT = "test_display/lcars_theme_test.cpp"
_FS = "test_display/flight_screen_test.cpp"
_GEO = "test_platform/geo_test.cpp"
_SCAN = "test_opensky/scan_area_test.cpp"
_TRACKED = "test_airspace/tracked_flight_test.cpp"
_ORDER = "test_airspace/flight_ordering_test.cpp"
_NOW = "test_airspace/now_pick_test.cpp"
_FIT = "test_display/fit_to_width_test.cpp"
STAGE_3_MOVED_TESTS = {
    **{name: (_GEO, _TV) for name in (
        "test_distance_zero_when_home_equals_aircraft",
        "test_distance_one_degree_north_at_equator",
        "test_distance_one_degree_east_at_equator",
        "test_distance_london_to_paris_sanity_check")},
    "test_compass_points": (_GEO, _LT),
    **{name: (_SCAN, "test_admin/config_portal_test.cpp") for name in (
        "test_bbox_area_matches_radius",
        "test_credit_cost_boundary_exactly_25_sq_deg",
        "test_credit_cost_boundary_exactly_100_sq_deg",
        "test_credit_cost_boundary_exactly_400_sq_deg",
        "test_credit_cost_just_below_and_above_each_boundary",
        "test_credit_cost_far_over_400_sq_deg",
        "test_credit_cost_at_zero_radius")},
    "test_infer_on_ground_rules": ("test_airspace/aircraft_state_test.cpp",
                                   "test_opensky/opensky_client_test.cpp"),
    **{name: (_TRACKED, _TV) for name in (
        "test_build_enriched_records_found_true",
        "test_build_enriched_records_found_false_when_lookup_missing",
        "test_build_enriched_records_missing_route_renders_as_dash",
        "test_build_enriched_records_found_false_from_cached_miss",
        "test_build_enriched_records_empty_aircraft_list",
        "test_annotate_fills_distance_for_positioned_aircraft",
        "test_annotate_leaves_no_position_aircraft_unset",
        "test_classify_phases_fills_phase_from_distance_and_vertical_rate",
        "test_classify_phases_no_distance_forces_none")},
    **{name: (_ORDER, _TV) for name in (
        "test_sort_orders_ascending_by_distance",
        "test_sort_is_stable_for_equal_distances",
        "test_sort_puts_rows_without_distance_last",
        "test_sort_empty_array_does_not_crash",
        "test_display_order_puts_airborne_before_ground_traffic",
        "test_display_order_puts_positionless_last",
        "test_display_order_all_on_ground_still_nearest_first",
        "test_split_featured_and_rest_multiple_rows",
        "test_split_featured_and_rest_single_row",
        "test_split_featured_and_rest_empty")},
    **{name: (_NOW, _FS) for name in (
        "test_pick_prefers_soonest_overhead_not_nearest",
        "test_pick_skips_ground_traffic_and_long_gone",
        "test_pick_just_passed_still_counts",
        "test_pick_without_overhead_candidate_shows_nearest_airborne",
        "test_pick_empty")},
    **{name: (_FIT, _LT) for name in (
        "test_fit_short_string_unchanged",
        "test_fit_exact_width_unchanged",
        "test_fit_long_string_gets_ellipsis",
        "test_fit_trims_trailing_space_before_ellipsis",
        "test_fit_result_never_exceeds_width",
        "test_fit_too_tight_for_ellipsis_hard_cuts",
        "test_fit_nonpositive_width_is_empty",
        "test_fit_uses_pixel_width_not_char_count",
        "test_fit_ascii_ellipsis_only",
        "test_fit_never_splits_a_utf8_character")},
    "test_utf8_length_counts_characters_not_bytes": ("test_display/utf8_test.cpp", _LT),
    "test_font_safe_accepts_ascii_and_polish_only": ("test_display/font_set_test.cpp", _LT),
}

# Stage 3: includes of these files that only the moved types needed; afterwards no finding from
# src/<file> may point at the layer.
STAGE_3_GONE_FINDINGS = [
    ("airspace/services/poll_pipeline.cpp", "display/core"),
    ("airspace/services/poll_pipeline.h", "opensky/core"),
    ("display/core/screens/table_view.h", "opensky/core"),
    ("admin/core/config_page.cpp", "admin/adapters"),
    ("admin/core/device_alerts.cpp", "admin/adapters"),
]

# Old layer folder names in a path: <ctx>/domain, <ctx>/app, src/app/, the -<app/> filter.
OLD_LAYER_PATH_RE = re.compile(r"\b(airspace|enrichment|opensky|display|admin)/(domain|app)\b"
                               r"|\bsrc/app/|<app/>")


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
            "src/main/main.cpp": "main",
            "src/main/net_task.h": "main",
            "src/airspace/core/flight_phase.h": "airspace/core",
            "src/airspace/services/poll_pipeline.cpp": "airspace/services",
            "src/enrichment/adapters/hexdb_http.h": "enrichment/adapters",
            "src/opensky/core/opensky_client.cpp": "opensky/core",
            "src/display/core/screens/table_view.h": "display/core",
            "src/display/core/theme/fonts/lcars_efont.c": "display/core",
            "src/display/services/screenshot.cpp": "display/services",
            "src/admin/core/firmware/version.cpp": "admin/core",
            "src/admin/core/config_page.h": "admin/core",
            "src/admin/adapters/config_portal.cpp": "admin/adapters",
            "src/admin/adapters/github_ca.h": "admin/adapters",
            "include/LGFX_CYD.hpp": "display/adapters",
            "include/touch_calibration.h": "display/core",
            "test/support/fixtures.h": "test",
            "test/test_admin/github_fixtures.h": "test",
            "test/test_display/table_view_test.cpp": "test",
        }
        for path, layer in cases.items():
            with self.subTest(path=path):
                self.assertEqual(layer, check_layers.classify(path))

    def test_unknown_context_or_missing_layer_is_outside(self):
        for path in ("src/stray.cpp", "src/display/stray.cpp", "src/weather/core/x.h",
                     "src/display/views/x.h", "src/display/domain/x.h", "lib/x.h"):
            with self.subTest(path=path):
                self.assertIsNone(check_layers.classify(path))


# ---- §5 table -------------------------------------------------------------------------------

class RuleTableTest(unittest.TestCase):
    ALLOWED = [
        ("src/airspace/core/", "shared/sun_times.h"),
        ("src/opensky/core/", "airspace/core/flight_phase.h"),
        ("src/display/core/", "enrichment/core/aircraft_names.h"),
        ("src/display/core/", "admin/core/device_alerts.h"),
        ("src/admin/core/", "display/core/backlight.h"),
        ("src/admin/core/", "admin/core/web_i18n.h"),
        ("src/airspace/services/", "enrichment/core/aircraft_lookup.h"),
        ("src/display/services/", "platform/core_link.h"),
        ("src/platform/", "display/core/screens/table_view.h"),
        ("src/enrichment/adapters/", "enrichment/core/lookup_store.h"),
        ("src/admin/adapters/", "admin/core/config_page.h"),
        ("src/main/", "enrichment/adapters/hexdb_http.h"),
        ("test/test_display/", "display/core/theme/lcars_theme.h"),
    ]

    FORBIDDEN = [
        ("src/display/core/", "opensky/core/opensky_quota.h", "display/core -> opensky/core"),
        ("src/enrichment/core/", "opensky/core/opensky_auth.h",
         "enrichment/core -> opensky/core"),
        ("src/airspace/core/", "display/core/screens/table_view.h",
         "airspace/core -> display/core"),
        ("src/opensky/core/", "enrichment/adapters/hexdb_http.h",
         "opensky/core -> enrichment/adapters"),
        ("src/admin/core/", "admin/adapters/config_portal.h", "admin/core -> admin/adapters"),
        ("src/admin/core/", "admin/services/x.h", "admin/core -> admin/services"),
        ("src/airspace/services/", "enrichment/services/x.h",
         "airspace/services -> enrichment/services"),
        ("src/admin/adapters/", "display/services/screenshot.h",
         "admin/adapters -> display/services"),
        ("src/display/core/", "main/net_task.h", "display/core -> main"),
        ("src/display/core/", "LGFX_CYD.hpp", "display/core -> display/adapters"),
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
                                               '#include "main/net_task.h"\n',
            "src/enrichment/adapters/hexdb_http.h": "",
            "src/main/net_task.h": "",
        }
        found = check_layers.check_files(files)
        texts = [check_layers.format_finding(f) for f in found]
        self.assertEqual(2, len(found), show(found))
        self.assertTrue(texts[0].startswith("test/test_enrichment/x_test.cpp:1: "
                                            "test -> enrichment/adapters "), texts[0])
        self.assertTrue(texts[1].startswith("test/test_enrichment/x_test.cpp:2: test -> main "),
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

    # Scenario: Plik w katalogu o starej nazwie warstwy jest zgłaszany jako leżący poza warstwą
    def test_old_layer_names_are_outside_any_layer(self):
        for path in ("src/display/domain/x.h", "src/admin/app/x.cpp", "src/app/main.cpp"):
            with self.subTest(path=path):
                self.assertIsNone(check_layers.classify(path))
                found = check_layers.check_files({path: "int x;\n"})
                self.assertEqual(1, len(found), show(found))
                self.assertEqual(path, found[0].path)
                self.assertIn("outside", found[0].message)
        new = {"src/display/core/x.h": "display/core",
               "src/admin/services/x.cpp": "admin/services",
               "src/main/main.cpp": "main"}
        for path, layer in new.items():
            with self.subTest(path=path):
                self.assertEqual(layer, check_layers.classify(path))
                found = check_layers.check_files({path: "int x;\n"})
                self.assertEqual([], found, show(found))

    def test_main_root_may_include_anything(self):
        self.assertIsNone(check_layers.allowed_targets("main"))

    def test_unresolved_quoted_include_is_reported(self):
        found = check_layers.check_files(
            {"src/display/core/x.cpp": '#include "shared/nowhere.h"\n'})
        self.assertEqual(1, len(found), show(found))
        self.assertEqual(1, found[0].line)
        self.assertIn("shared/nowhere.h", check_layers.format_finding(found[0]))

    def test_adapters_see_their_own_context_and_platform_only(self):
        source = "src/enrichment/adapters/x.cpp"
        files = {
            source: '#include "enrichment/services/enrichment_service.h"\n'
                    '#include "enrichment/core/lookup_store.h"\n'
                    '#include "platform/core_link.h"\n'
                    '#include "shared/sun_times.h"\n'
                    '#include <HTTPClient.h>\n'
                    '#include "opensky/core/opensky_auth.h"\n'
                    '#include "display/adapters/y.h"\n',
            "src/enrichment/services/enrichment_service.h": "",
            "src/enrichment/core/lookup_store.h": "",
            "src/platform/core_link.h": "",
            "src/shared/sun_times.h": "",
            "src/opensky/core/opensky_auth.h": "",
            "src/display/adapters/y.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([6, 7], [f.line for f in found], show(found))
        self.assertIn("enrichment/adapters -> opensky/core", found[0].message)
        self.assertIn("enrichment/adapters -> display/adapters", found[1].message)

    def test_adapter_sees_its_own_core_and_services_but_not_another_contexts_core(self):
        files = {
            "src/display/adapters/x.cpp": '#include "display/core/i18n.h"\n'
                                          '#include "display/services/screenshot.h"\n'
                                          '#include "admin/core/config_store.h"\n',
            "src/display/core/i18n.h": "",
            "src/display/services/screenshot.h": "",
            "src/admin/core/config_store.h": "",
        }
        self.assertEqual({"shared", "platform", "sdk", "display/core", "display/services",
                          "display/adapters"}, check_layers.allowed_targets("display/adapters"))
        found = check_layers.check_files(files)
        self.assertEqual([3], [f.line for f in found], show(found))
        self.assertIn("display/adapters -> admin/core", found[0].message)

    def test_services_layer_sees_its_own_services_but_not_another_contexts(self):
        files = {
            "src/display/services/x.cpp": '#include "display/services/y.h"\n'
                                          '#include "admin/services/z.h"\n',
            "src/display/services/y.h": "",
            "src/admin/services/z.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([2], [f.line for f in found], show(found))
        self.assertIn("display/services -> admin/services", found[0].message)

    def test_enrichment_services_may_implement_the_airspace_port(self):
        files = {
            "src/enrichment/services/x.cpp": '#include "airspace/services/poll_pipeline.h"\n'
                                             '#include "airspace/core/flight_phase.h"\n'
                                             '#include "enrichment/core/lookup_cache.h"\n'
                                             '#include "opensky/core/opensky_auth.h"\n',
            "src/airspace/services/poll_pipeline.h": "",
            "src/airspace/core/flight_phase.h": "",
            "src/enrichment/core/lookup_cache.h": "",
            "src/opensky/core/opensky_auth.h": "",
        }
        found = check_layers.check_files(files)
        self.assertEqual([4], [f.line for f in found], show(found))
        self.assertIn("enrichment/services -> opensky/core", found[0].message)

    def test_test_files_may_include_support_and_their_own_headers(self):
        files = {
            "test/test_admin/x_test.cpp": '#include "test_support.h"\n'
                                          '#include "github_fixtures.h"\n'
                                          '#include "admin/core/config_page.h"\n'
                                          '#include "platform/core_link.h"\n',
            "test/support/test_support.h": "",
            "test/test_admin/github_fixtures.h": "",
            "src/admin/core/config_page.h": "",
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

    # Scenario Outline: core/ i services/ nie dołączają SDK, także pod #ifdef ARDUINO
    def test_sdk_include_in_domain_is_reported_even_inside_ifdef(self):
        rows = [
            ("src/display/core/", "Arduino.h", "display/core"),
            ("src/opensky/core/", "HTTPClient.h", "opensky/core"),
            ("src/enrichment/core/", "Preferences.h", "enrichment/core"),
            ("src/admin/services/", "WebServer.h", "admin/services"),
            ("src/display/core/", "LovyanGFX.hpp", "display/core"),
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
        for source in ("src/shared/x.cpp", "src/airspace/core/x.cpp",
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
        for source in ("src/display/adapters/x.cpp", "src/main/main.cpp"):
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

    def test_strings_and_raw_strings_do_not_hide_or_fake_includes(self):
        # A "/*" inside a string doesn't open a comment; a raw string's lines aren't directives.
        text = ('const char *a = "/* not a comment";\n'
                'const char *b = R"(\n#include "fake.h"\n)";\n'
                '#include "shared/sun_times.h"\n')
        self.assertEqual([(5, "shared/sun_times.h", False, False)], self.names(text))

    def test_opensky_services_sees_its_own_context_only(self):
        self.assertEqual({"shared", "opensky/core", "opensky/services"},
                         check_layers.allowed_targets("opensky/services"))

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
             "test/test_admin/github_fixtures.h", "src/display/core/screens/table_view.h"}

    def test_full_path_from_src_include_and_test_support(self):
        r = check_layers.resolve_include
        self.assertEqual("src/platform/core_link.h",
                         r("src/main/main.cpp", "platform/core_link.h", self.KNOWN))
        self.assertEqual("include/LGFX_CYD.hpp", r("src/main/main.cpp", "LGFX_CYD.hpp", self.KNOWN))
        self.assertEqual("test/support/fixtures.h",
                         r("test/test_admin/x_test.cpp", "fixtures.h", self.KNOWN))

    def test_own_directory_only_for_tests(self):
        r = check_layers.resolve_include
        self.assertEqual("test/test_admin/github_fixtures.h",
                         r("test/test_admin/x_test.cpp", "github_fixtures.h", self.KNOWN))
        self.assertIsNone(r("test/test_display/x_test.cpp", "github_fixtures.h", self.KNOWN))
        self.assertIsNone(r("src/display/core/screens/x.cpp", "table_view.h", self.KNOWN))

    def test_short_include_in_src_is_a_full_path_finding(self):
        files = {"src/display/core/screens/x.cpp": '#include "table_view.h"\n'
                                                   '#include "display/core/screens/table_view.h"\n',
                 "src/display/core/screens/table_view.h": ""}
        found = check_layers.full_path_findings(files)
        self.assertEqual([1], [f.line for f in found], show(found))
        self.assertEqual("src/display/core/screens/x.cpp", found[0].path)


# ---- CLI ------------------------------------------------------------------------------------

TWO_VIOLATIONS = {
    "src/display/core/a.cpp": '#include "opensky/core/q.h"\n',
    "src/shared/b.cpp": '#include "shared/b.h"\n#include "platform/core_link.h"\n',
    "src/shared/b.h": "",
    "src/opensky/core/q.h": "",
    "src/platform/core_link.h": "",
}


class CliTest(unittest.TestCase):
    # Scenario: W trybie raportu naruszenia są wypisane, ale wynik to 0
    def test_report_mode_exits_zero_with_violations(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, TWO_VIOLATIONS)
            code, out, _ = run_cli(tmp)
        out = out.replace("\\", "/")
        self.assertIn("src/display/core/a.cpp:1: display/core -> opensky/core", out)
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
        files["src/display/core/c.cpp"] = '#include "opensky/core/q.h"\n'
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, files)
            code, out, raw = run_cli(tmp)
        self.assertTrue(raw.isascii(), out)  # non-ASCII output breaks the Windows console
        pair_lines = [ln for ln in out.splitlines()
                      if "display/core -> opensky/core" in ln and not ln.startswith("src")]
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

    # Scenario: W src/ nie ma już katalogów warstw o starych nazwach
    def test_old_layer_folders_are_gone(self):
        src = ROOT / "src"
        old = [f"src/{c}/{layer}/" for c in check_layers.CONTEXTS for layer in ("domain", "app")
               if (src / c / layer).exists()]
        if (src / "app").exists():
            old.append("src/app/")
        self.assertEqual([], old, "old layer folders still in src/")
        for name in ("main.cpp", "net_task.h", "net_task.cpp"):
            with self.subTest(file=name):
                self.assertTrue((src / "main" / name).is_file(), f"src/main/{name}")

    # Scenario: Liczba naruszeń w raporcie warstw nie rośnie ponad stan po ostatnim etapie
    def test_layer_findings_do_not_exceed_the_stage_limit(self):
        files = check_layers.load_tree(ROOT)
        findings = check_layers.check_files(files) + check_layers.full_path_findings(files)
        self.assertLessEqual(len(findings), LAYER_FINDINGS_LIMIT,
                             f"{len(findings)} layer findings, limit {LAYER_FINDINGS_LIMIT}:"
                             + show(findings, limit=len(findings)))

    # Scenario: W kodzie, testach, skryptach i plikach budowania nie zostały ścieżki ze starymi
    # nazwami warstw
    def test_no_old_layer_paths_left_in_code_and_build_files(self):
        paths = [p for d in ("src", "test", "include") for p in sorted((ROOT / d).rglob("*"))
                 if p.is_file()]
        paths.append(ROOT / "platformio.ini")
        paths += sorted((ROOT / ".github" / "workflows").glob("*.yml"))
        # The stage-1 move table and the tool tests name the old paths on purpose.
        paths += [p for p in sorted((ROOT / "tools").glob("*.py"))
                  if p.name != "rewrite_includes.py" and not p.name.startswith("test_")]
        self.assertGreater(len(paths), 100)
        hits = []
        for p in paths:
            text = p.read_text(encoding="utf-8", errors="replace")
            for n, line in enumerate(text.splitlines(), 1):
                m = OLD_LAYER_PATH_RE.search(line)
                if m:
                    hits.append(f"{p.relative_to(ROOT).as_posix()}:{n}: {m.group(0)}")
        self.assertEqual([], hits, f"{len(hits)} old layer path(s):\n" + "\n".join(hits[:60]))

    # Scenario Outline: Przeniesiony typ lub funkcja ma jedną deklarację, w pliku swojego kontekstu
    def test_moved_symbols_are_declared_in_their_new_home(self):
        headers = {p: t for p, t in repo_sources("src").items() if p.endswith((".h", ".hpp"))}
        self.assertGreater(len(headers), 30)
        for symbol, (new, old) in STAGE_3_HOMES.items():
            with self.subTest(symbol=symbol):
                hits = [f"{p}:{n}" for p, t in headers.items()
                        for n in declaration_lines(t, symbol)]
                self.assertFalse([h for h in hits if h.startswith(f"src/{old}:")],
                                 f"{symbol} still declared in src/{old}: {hits}")
                self.assertEqual([f"src/{new}"], [h.rsplit(":", 1)[0] for h in hits],
                                 f"declarations of {symbol}: {hits or 'none'}")

    # Scenario Outline: Testy przeniesionego kodu leżą w grupie jego kontekstu, bez kopii w
    # starym pliku
    def test_moved_tests_follow_their_code(self):
        self.assertEqual(49, len(STAGE_3_MOVED_TESTS))
        sources = {p[len("test/"):]: t for p, t in repo_sources("test").items()}
        for name, (new, old) in STAGE_3_MOVED_TESTS.items():
            with self.subTest(test=name):
                defs = [f"{p}:{n}" for p, t in sources.items() for n in defined_test_lines(t, name)]
                old_text = sources.get(old, "")
                self.assertEqual(([], 0), (defined_test_lines(old_text, name),
                                           run_test_calls(old_text, name)),
                                 f"{name} still defined / run in test/{old}")
                self.assertEqual([new], [d.rsplit(":", 1)[0] for d in defs],
                                 f"definitions of {name}: {defs or 'none'}")
                self.assertEqual(1, run_test_calls(sources[new], name),
                                 f"RUN_TEST({name}) in test/{new}")

    # Scenario Outline: Raport warstw nie zgłasza już include'ów, które były potrzebne tylko dla
    # przeniesionych typów
    def test_stage_3_removes_the_misplaced_type_findings(self):
        findings = check_layers.check_files(check_layers.load_tree(ROOT))
        self.assertGreater(len(findings), 0, "check_layers found nothing at all")
        for path, layer in STAGE_3_GONE_FINDINGS:
            with self.subTest(path=path, layer=layer):
                self.assertTrue((ROOT / "src" / path).is_file(), f"src/{path}")
                hits = [f for f in findings if f.path == f"src/{path}"
                        and re.search(r"-> " + re.escape(layer) + r"(\s|$)", f.message)]
                self.assertEqual([], hits, show(hits))

    # Scenario: core_link nie dołącza już widoków dla typów, które przenosi między rdzeniami
    def test_core_link_includes_no_screen_headers(self):
        text = (ROOT / "src" / "platform" / "core_link.h").read_text(encoding="utf-8",
                                                                      errors="replace")
        quoted = [i.name for i in check_layers.find_includes(text) if not i.angle]
        self.assertGreater(len(quoted), 3, "find_includes found too few includes in core_link.h")
        screens = [n for n in quoted if n.startswith("display/core/screens/")]
        self.assertEqual([], screens, "core_link.h includes screen headers")
        for header in ("airspace/core/tracked_flight.h", "airspace/core/feed_health.h"):
            with self.subTest(header=header):
                self.assertIn(header, quoted)


def strip_comments(text):
    """`text` with // and /* */ comments blanked (newlines kept, so line numbers stay); string
    and char literals are kept as they are, so a "//" inside one doesn't start a comment."""
    out, i, n = [], 0, len(text)
    while i < n:
        c, nxt = text[i], text[i + 1:i + 2]
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


_NOT_A_TYPE = {"return", "else", "case", "throw", "delete", "new", "goto", "sizeof", "co_return"}


def declaration_lines(text, symbol):
    """Line numbers where `text` (a header) declares `symbol`: a type (`struct|class|enum
    [class] <symbol>` followed by `{` or `:` -- a forward declaration `struct X;` doesn't count)
    or a function (`<return type> <symbol>(...)` followed by `;` or a body). Comments are
    ignored; the name must match as a whole word (`Aircraft` isn't `AircraftRow`)."""
    code = strip_comments(text)
    name = re.escape(symbol)
    lines = set()
    type_re = re.compile(r"\b(?:struct|class|enum(?:\s+(?:class|struct))?)\s+" + name
                         + r"\b\s*(?:final\s*)?[{:](?!:)")
    for m in type_re.finditer(code):
        lines.add(code.count("\n", 0, m.start()) + 1)
    func_re = re.compile(r"(?m)^[ \t]*((?:[A-Za-z_][\w:]*(?:<[^;(){}]*>)?[\s*&]+)+)"
                         + name + r"\s*\(")
    for m in func_re.finditer(code):
        words = re.findall(r"[A-Za-z_]\w*", m.group(1))
        if not words or words[0] in _NOT_A_TYPE:
            continue
        depth, j = 0, m.end() - 1
        while j < len(code):
            depth += {"(": 1, ")": -1}.get(code[j], 0)
            j += 1
            if depth == 0:
                break
        if re.match(r"(?:\s|const\b|noexcept\b|override\b)*[;{]", code[j:]):
            lines.add(code.count("\n", 0, m.start(1)) + 1)
    return sorted(lines)


def defined_test_lines(text, name):
    """Line numbers of `static void <name>(` in `text` (comments ignored)."""
    code = strip_comments(text)
    return [code.count("\n", 0, m.start()) + 1 for m in
            re.finditer(r"(?m)^[ \t]*static\s+void\s+" + re.escape(name) + r"\s*\(", code)]


def run_test_calls(text, name):
    """How many times `text` calls RUN_TEST(<name>) (comments ignored)."""
    return len(re.findall(r"\bRUN_TEST\(\s*" + re.escape(name) + r"\s*\)",
                          strip_comments(text)))


class DeclarationMatchTest(unittest.TestCase):
    def test_forward_declaration_and_longer_names_do_not_count(self):
        self.assertEqual([], declaration_lines("struct AircraftRow;\n", "AircraftRow"))
        self.assertEqual([], declaration_lines("struct AircraftRow {\n  int x;\n};\n",
                                               "Aircraft"))
        self.assertEqual([1], declaration_lines("struct Aircraft {\n  int x;\n};\n", "Aircraft"))

    def test_types_with_a_base_or_an_underlying_type_count(self):
        self.assertEqual([2], declaration_lines("// enum class OpenSkyHealth {\n"
                                                "enum class OpenSkyHealth : uint8_t {\n  Ok\n};\n",
                                                "OpenSkyHealth"))
        self.assertEqual([1], declaration_lines("struct A : B {};\n", "A"))
        self.assertEqual([], declaration_lines("x = A::B;\n", "A"))

    def test_function_prototypes_and_definitions_count_but_calls_do_not(self):
        text = ("const char *compassPoint(float bearingDeg);\n"
                "std::vector<AircraftRow> buildEnrichedRecords(\n"
                "    const std::vector<Aircraft> &aircraft,\n"
                "    int n);\n"
                "inline size_t utf8Length(const std::string &s) { return s.size(); }\n"
                "inline int f() { return utf8Length(\"x\"); }\n"
                "  int n = utf8Length(s);\n"
                "  utf8Length(s);\n"
                "  return utf8Length(s);\n"
                "// size_t utf8Length(const std::string &s);\n"
                "/* size_t utf8Length(const std::string &s); */\n")
        self.assertEqual([1], declaration_lines(text, "compassPoint"))
        self.assertEqual([2], declaration_lines(text, "buildEnrichedRecords"))
        self.assertEqual([5], declaration_lines(text, "utf8Length"))

    def test_comment_marker_inside_a_string_does_not_hide_code(self):
        text = 'const char *k = "http://x";\nbool isFontSafeText(const std::string &t);\n'
        self.assertEqual([2], declaration_lines(text, "isFontSafeText"))

    def test_test_definitions_and_run_test_calls(self):
        text = ("static void test_a(void) {}\n"
                "static void test_ab(void) {}\n"
                "// static void test_a(void) {}\n"
                "void run_x_tests() {\n  RUN_TEST(test_a);\n  RUN_TEST(test_ab);\n"
                "  // RUN_TEST(test_a);\n}\n")
        self.assertEqual([1], defined_test_lines(text, "test_a"))
        self.assertEqual(1, run_test_calls(text, "test_a"))
        self.assertEqual([], defined_test_lines(text, "test_b"))


def platformio_ini():
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(ROOT / "platformio.ini", encoding="utf-8")
    return cp


class BuildConfigTest(unittest.TestCase):
    # Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej
    # części platform/ — poza config_portal do etapu 4
    def test_native_filter_excludes_roots_and_adapters(self):
        native = platformio_ini()["env:native"]
        tokens = native.get("build_src_filter", "").split()
        self.assertEqual(["+<*>", "-<main/>", "-<*/adapters/>", "-<platform/*_esp32.cpp>",
                          "+<admin/adapters/config_portal.cpp>"], tokens)
        readded = [t for t in tokens[1:] if t.startswith("+<")]
        self.assertEqual(["+<admin/adapters/config_portal.cpp>"], readded)
        self.assertTrue((ROOT / "src" / "admin" / "adapters" / "config_portal.cpp").is_file())

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
