"""Tests for tools/check_layers.py:  python tools/test_check_layers.py

The rule tests feed the pure functions {relpath: text} dicts (the included targets are in the
dict too, so they resolve); LayoutTest / BuildConfigTest / WorkflowTest read this repository
and pin the state spec 12 stages 2–6 leave behind.
"""

import configparser
import contextlib
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_comments  # noqa: E402
import check_layers  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(__file__).resolve().parent / "check_layers.py"
SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}

# `grep -ro "RUN_TEST(" test --include=*.cpp | wc -l` on main before stage 1 (plan step 0).
RUN_TEST_BASELINE = 874

# Layer findings (check_files + full_path_findings) on this repository after the last finished
# stage: 51 after stage 2, 47 after stage 3 (-6 includes only the moved types needed, +2
# tracked_flight.h -> enrichment/core), 14 after stage 4 (-38 SDK / adapter / platform includes
# that left core/ with the hardware halves, +5 adapters reaching another context's core), 12
# after stage 6 (-2: opensky_http.cpp and backlight_ldr.cpp take their settings objects instead
# of including config_store.h), 0 after stage 8 (all 12 removed in the code, section 5's table
# unchanged). A limit, not a pin: a stage that lowers the count lowers this number with it, so a
# new finding can't hide under the old limit.
LAYER_FINDINGS_LIMIT = 0

# Stage 3 (spec 12 section 8 item 3): symbol -> (new header, old header), both from src/. The
# types carry their stage-7 glossary names (AircraftRow, OpenSkyHealth, Aircraft before).
STAGE_3_HOMES = {
    "TrackedFlight": ("airspace/core/tracked_flight.h", "display/core/screens/table_view.h"),
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
    "FeedHealth": ("airspace/core/feed_health.h", "display/core/screens/status_bar.h"),
    "AircraftState": ("airspace/core/aircraft_state.h", "opensky/core/opensky_client.h"),
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

# Stage 4 (spec 12 section 8 item 4): function -> (adapter .cpp that defines it, core .cpp that
# keeps the pure half), both from src/.
STAGE_4_ADAPTERS = {
    "drawStatusBar": ("display/adapters/lgfx/status_bar_draw.cpp",
                      "display/core/screens/status_bar.cpp"),
    "drawOtaScreen": ("display/adapters/lgfx/service_screen_draw.cpp",
                      "display/core/screens/service_screen.cpp"),
    "drawRadarView": ("display/adapters/lgfx/radar_view_draw.cpp",
                      "display/core/screens/radar_view.cpp"),
    "flightScreenReserveSprite": ("display/adapters/lgfx/flight_screen_draw.cpp",
                                  "display/core/screens/flight_screen.cpp"),
    "drawTablePage": ("display/adapters/lgfx/table_view_draw.cpp",
                      "display/core/screens/table_view.cpp"),
    "drawFeaturedPanel": ("display/adapters/lgfx/featured_panel_draw.cpp",
                          "display/core/screens/featured_panel.cpp"),
    "drawAircraftSummary": ("display/adapters/lgfx/aircraft_summary_draw.cpp",
                            "display/core/screens/aircraft_summary.cpp"),
    "drawBottomNav": ("display/adapters/lgfx/lcars_theme_draw.cpp",
                      "display/core/theme/lcars_theme.cpp"),
    "touchInputRead": ("display/adapters/touch_xpt2046.cpp", "display/core/touch_input.cpp"),
    "backlightLoop": ("display/adapters/backlight_ldr.cpp", "display/core/backlight.cpp"),
    "storeLoadAircraft": ("enrichment/adapters/lookup_store_nvs.cpp",
                          "enrichment/core/lookup_store.cpp"),
    "lookupAircraft": ("enrichment/adapters/hexdb_sources.cpp",
                       "enrichment/core/aircraft_lookup.cpp"),
    "lookupRoute": ("enrichment/adapters/hexdb_sources.cpp", "enrichment/core/route_lookup.cpp"),
    "fetchAircraftStates": ("opensky/adapters/opensky_http.cpp",
                            "opensky/core/opensky_client.cpp"),
    "otaFlashSession": ("admin/adapters/ota_flash.cpp", "admin/core/firmware/ota_update.cpp"),
    "releaseCheckLoop": ("admin/adapters/github_http.cpp",
                         "admin/core/firmware/release_check.cpp"),
    "loadSettings": ("admin/adapters/settings_nvs.cpp", "admin/core/settings.cpp"),
    # In the composition root since stage 8: it joins data from three contexts.
    "deviceAlertInputs": ("main/alert_sources.cpp", "admin/core/device_alerts.cpp"),
}

# Stage 4: symbol -> (new header, old header), both from src/. The old home of the first five is
# config_portal.h under its stage-4 name (web_server.h); a missing old header reads as empty.
_WEB = "admin/adapters/web_server.h"
STAGE_4_HOMES = {
    "parseOpenSkyCredentialsJson": ("admin/core/settings_form.h", _WEB),
    "CredentialsFile": ("admin/core/settings_form.h", _WEB),  # OpenSkyCredentials until stage 6
    "isValidFloatString": ("admin/core/settings_form.h", _WEB),
    "isValidUnsignedIntString": ("admin/core/settings_form.h", _WEB),
    "OtaObserver": ("admin/core/firmware/ota_update.h", _WEB),
    "TouchPoint": ("display/adapters/touch_xpt2046.h", "display/core/touch_input.h"),
    "touchInputRead": ("display/adapters/touch_xpt2046.h", "display/core/touch_input.h"),
    "gfxTextMeasure": ("display/adapters/lgfx/lcars_theme_draw.h",
                       "display/core/theme/lcars_theme.h"),
    "drawPhaseIcon": ("display/adapters/lgfx/lcars_theme_draw.h",
                      "display/core/theme/lcars_theme.h"),
    "drawTablePage": ("display/adapters/lgfx/table_view_draw.h",
                      "display/core/screens/table_view.h"),
    "drawStatusBar": ("display/adapters/lgfx/status_bar_draw.h",
                      "display/core/screens/status_bar.h"),
    "backlightBegin": ("display/adapters/backlight_ldr.h", "display/core/backlight.h"),
    "formatDate": ("shared/clock.h", "platform/time_sync.h"),
    "minuteOf": ("shared/clock.h", "platform/time_sync.h"),
}

# Stage 4: the 21 tests that follow the pure functions out of config_portal and time_sync,
# test -> (new file, old file), both from test/. The old files are git-mv'ed away.
_SETTINGS_FORM = "test_admin/settings_form_test.cpp"
_CLOCK = "test_platform/clock_test.cpp"
STAGE_4_MOVED_TESTS = {
    **{name: (_SETTINGS_FORM, "test_admin/config_portal_test.cpp") for name in (
        "test_parse_credentials_json_valid",
        "test_parse_credentials_json_ignores_extra_fields_and_whitespace",
        "test_parse_credentials_json_rejects_malformed_json",
        "test_parse_credentials_json_rejects_missing_field",
        "test_parse_credentials_json_rejects_empty_values",
        "test_parse_credentials_json_rejects_empty_string",
        "test_parse_credentials_json_rejects_oversized_body",
        "test_valid_float_string_accepts_typical_values",
        "test_valid_float_string_rejects_garbage",
        "test_valid_unsigned_int_string_accepts_digits",
        "test_valid_unsigned_int_string_rejects_non_digits")},
    **{name: (_CLOCK, "test_platform/time_sync_test.cpp") for name in (
        "test_format_date_epoch_zero",
        "test_format_date_known_values",
        "test_format_date_pads_day_and_month",
        "test_format_date_leap_year_after_feb29",
        "test_format_date_negative_epoch_clamps",
        "test_format_time_epoch_zero",
        "test_format_time_known_values",
        "test_format_time_pads_single_digits",
        "test_minute_of_same_within_a_minute",
        "test_minute_of_changes_at_minute_boundary")},
}

# Stage 4: (from layer, to layer) pairs no finding may have once the hardware halves left core/.
STAGE_4_GONE_PAIRS = [
    ("display/core", "display/adapters"),
    ("display/core", "sdk"),
    ("display/core", "platform"),
    ("admin/core", "sdk"),
    ("admin/core", "admin/adapters"),
    ("admin/core", "platform"),
    ("enrichment/core", "sdk"),
    ("enrichment/core", "enrichment/adapters"),
    ("opensky/core", "sdk"),
    ("opensky/core", "platform"),
    ("opensky/core", "admin/core"),
    ("test", "admin/adapters"),
]

# Stage 5 (spec 12 section 6, section 8 item 5, section 10 item 3): main.cpp's line limit and
# the state machines that leave it -- function main.cpp no longer defines -> (new symbol, its
# header from src/).
MAIN_CPP_LINE_LIMIT = 250
STAGE_5_FLOWS = [
    ("handleTap", "routeTap", "display/services/ui_flow.h"),
    ("clampPage", "TablePager", "display/services/ui_flow.h"),
    ("maybeShowUpdatePrompt", "UpdatePromptFlow", "display/services/update_prompt_flow.h"),
    ("serviceUpdatePrompt", "UpdatePromptFlow", "display/services/update_prompt_flow.h"),
    ("serviceOtaScreen", "OtaScreenFlow", "display/services/ota_screen_flow.h"),
    ("serviceAlertScreen", "AlertScreenFlow", "display/services/alert_screen_flow.h"),
    ("offerWebPasswordReset", "PasswordResetFlow", "display/services/password_reset_flow.h"),
    ("screenshotPrepare", "screenshotPrepareTarget", "display/services/screenshot_bridge.h"),
    ("serviceScreenshot", "screenshotUiPass", "display/services/screenshot_bridge.h"),
    ("perfLoopTick", "UiPerfMeter", "display/services/ui_perf.h"),
]

# Stage 5: the only plain static variable main.cpp may keep (the last NetState it took).
MAIN_CPP_STATIC_ALLOWED = {"netSeen"}

# Stage 5: what POST /save may no longer call itself -- the form logic is applySettingsForm().
SAVE_ROUTE_FORBIDDEN = ["isValidFloatString", "isValidUnsignedIntString", "decidePasswordChange",
                        "parseClockMinutes", "nightModeFromCode", "languageFromCode", "strtof",
                        "strtoul", "atoi"]
SAVE_ROUTE_LINE_LIMIT = 30

# Stage 5: every new module from section 6 -> (its test file from test/, run_<name>_tests).
STAGE_5_SERVICES = [
    ("display/services/ui_flow", "test_display/ui_flow_test.cpp", "ui_flow"),
    ("display/services/update_prompt_flow", "test_display/update_prompt_flow_test.cpp",
     "update_prompt_flow"),
    ("display/services/ota_screen_flow", "test_display/ota_screen_flow_test.cpp",
     "ota_screen_flow"),
    ("display/services/alert_screen_flow", "test_display/alert_screen_flow_test.cpp",
     "alert_screen_flow"),
    ("display/services/password_reset_flow", "test_display/password_reset_flow_test.cpp",
     "password_reset_flow"),
    ("display/services/screenshot_bridge", "test_display/screenshot_bridge_test.cpp",
     "screenshot_bridge"),
    ("display/services/ui_perf", "test_display/ui_perf_test.cpp", "ui_perf"),
    ("admin/core/settings_form", "test_admin/settings_form_test.cpp", "settings_form"),
]

# Stage 5: screenshot bridge tests follow ScreenshotBridge / ScreenshotReturn to test_display,
# the BMP tests stay -- test -> (new file, old file), both from test/ (the .feature's examples).
_BRIDGE_TESTS = "test_display/screenshot_bridge_test.cpp"
_SHOT_TESTS = "test_admin/screenshot_test.cpp"
STAGE_5_MOVED_TESTS = {
    "test_bridge_normal_capture_fills_every_band": (_BRIDGE_TESTS, _SHOT_TESTS),
    "test_bridge_concurrent_random_timeouts_never_touch_freed_buffers": (_BRIDGE_TESTS,
                                                                         _SHOT_TESTS),
    "test_bridge_idle_rule_across_millis_rollover": (_BRIDGE_TESTS, _SHOT_TESTS),
    "test_screen_returns_to_previous_after_capture": (_BRIDGE_TESTS, _SHOT_TESTS),
    "test_return_not_triggered_without_prepare": (_BRIDGE_TESTS, _SHOT_TESTS),
    "test_header_fields": (_SHOT_TESTS, _BRIDGE_TESTS),
}

# Stage 6 (spec 12 section 7, plan "§7 -> objects"): Settings field -> (its object type,
# the header from src/ that declares it, the object's fields). Nothing else is left in Settings
# (Config until stage 7, in config_store.h).
STAGE_6_SETTINGS = {
    "home": ("Home", "airspace/core/home.h", ["lat", "lon", "radius_deg", "configured"]),
    "phase": ("PhaseThresholds", "airspace/core/flight_phase.h",
              ["near_airport_km", "climb_rate_threshold_mps"]),
    "opensky": ("OpenSkyCredentials", "opensky/core/opensky_settings.h",
                ["client_id", "client_secret"]),
    "poll": ("PollSettings", "opensky/core/opensky_settings.h", ["interval_s"]),
    "display": ("DisplaySettings", "display/core/display_settings.h",
                ["language", "last_screen"]),
    "auto_cycle": ("AutoCycleSettings", "display/core/display_settings.h",
                   ["enabled", "interval_s"]),
    "backlight": ("BacklightSettings", "display/core/backlight.h",
                  ["brightness_auto", "brightness_fixed_pct", "brightness_min_pct",
                   "brightness_max_pct", "night_mode", "night_from_min", "night_to_min",
                   "night_pct", "night_wake_s", "night_pause_polling"]),
    "update": ("UpdateSettings", "admin/core/settings.h",
               ["check_enabled", "show_prerelease", "skipped_tag"]),
    "web": ("WebSettings", "admin/core/settings.h", ["language", "password"]),
}

# Every type a settings parameter can have: the aggregate and its objects.
SETTINGS_TYPES = {"Settings"} | {t for t, _, _ in STAGE_6_SETTINGS.values()}

# Stage 6: the 28 NVS keys of v1.11.3 and the Preferences type each is read and written with
# (namespace "cydsky"); `isKey()` doesn't count.
STAGE_6_NVS_KEYS = {
    "home_lat": "Float", "home_lon": "Float", "radius_deg": "Float", "poll_s": "UInt",
    "os_id": "String", "os_secret": "String", "last_screen": "Int", "auto_cyc": "Bool",
    "auto_cyc_s": "UInt", "near_km": "Float", "climb_mps": "Float", "home_cfg": "Bool",
    "lang": "UChar", "wlang": "UChar", "wpass": "String", "updchk": "Bool", "updpre": "Bool",
    "updskip": "String", "bl_auto": "Bool", "bl_fix": "UChar", "bl_min": "UChar",
    "bl_max": "UChar", "nt_mode": "UChar", "nt_from": "UShort", "nt_to": "UShort",
    "nt_pct": "UChar", "nt_wake": "UShort", "nt_poll": "Bool",
}

# Stage 6: the Settings field each NVS key loads into and saves from (settings_nvs.cpp). The keys
# kept their pre-stage-6 meaning: `lang` is the display's language, `wlang` the page's.
STAGE_6_NVS_FIELDS = {
    "home_lat": "home.lat", "home_lon": "home.lon", "radius_deg": "home.radius_deg",
    "poll_s": "poll.interval_s", "os_id": "opensky.client_id",
    "os_secret": "opensky.client_secret", "last_screen": "display.last_screen",
    "auto_cyc": "auto_cycle.enabled", "auto_cyc_s": "auto_cycle.interval_s",
    "near_km": "phase.near_airport_km", "climb_mps": "phase.climb_rate_threshold_mps",
    "home_cfg": "home.configured", "lang": "display.language", "wlang": "web.language",
    "wpass": "web.password", "updchk": "update.check_enabled",
    "updpre": "update.show_prerelease", "updskip": "update.skipped_tag",
    "bl_auto": "backlight.brightness_auto", "bl_fix": "backlight.brightness_fixed_pct",
    "bl_min": "backlight.brightness_min_pct", "bl_max": "backlight.brightness_max_pct",
    "nt_mode": "backlight.night_mode", "nt_from": "backlight.night_from_min",
    "nt_to": "backlight.night_to_min", "nt_pct": "backlight.night_pct",
    "nt_wake": "backlight.night_wake_s", "nt_poll": "backlight.night_pause_polling",
}

# Stage 6: (header from src/, function or struct, its settings parameters / members) -- the
# .feature's Examples. A struct row lists "<type> <member>".
STAGE_6_SIGNATURES = [
    ("opensky/core/opensky_client.h", "fetchAircraftStates",
     ["const OpenSkyCredentials &", "const Home &"]),
    ("opensky/core/opensky_client.h", "openSkyClientPoll",
     ["const OpenSkyCredentials &", "const Home &", "const PollSettings &"]),
    ("opensky/core/opensky_client.h", "openSkyAuthStatus", ["const OpenSkyCredentials &"]),
    ("opensky/core/opensky_client.h", "openSkyMsUntilNextPoll", ["const PollSettings &"]),
    ("display/core/backlight.h", "backlightHandleTouch", ["const BacklightSettings &"]),
    ("display/core/backlight.h", "backlightStatus", ["const BacklightSettings &"]),
    ("display/adapters/backlight_ldr.h", "backlightLoop",
     ["const BacklightSettings &", "const Home &"]),
    ("airspace/services/poll_pipeline.h", "PipelineParams", ["Home home", "PhaseThresholds phase"]),
    ("admin/core/firmware/release_check.h", "releaseCheckBegin", ["const UpdateSettings &"]),
    ("admin/core/config_page.h", "pausedMinutesPerDay", ["const BacklightSettings &"]),
]

# Stage 6: the settings objects the display may read from NetState (settings->X / cfg().X), and the
# only field of `update` it needs (the update question's skipped release).
STAGE_6_DISPLAY_SETTINGS = {"home", "display", "auto_cycle", "backlight", "update"}
STAGE_6_DISPLAY_UPDATE_FIELDS = {"skipped_tag"}

# Stage 6: adapters that read settings through config_store.h before; afterwards no layer
# finding at all comes from them.
STAGE_6_GONE_FINDINGS = ["opensky/adapters/opensky_http.cpp", "display/adapters/backlight_ldr.cpp"]

# Stage 7 (spec 12 section 3, the glossary): the web server's functions, after webServer.
WEB_SERVER_FUNCTIONS = ["Begin", "Loop", "IsActive", "SaveCount", "SetScreenshotSource",
                        "SetOtaObserver", "SetPerfSource", "RestartPending"]

# Stage 8 (spec 12 section 8 item 8): symbol -> (new header, old header), both from src/ (the
# .feature's Examples).
STAGE_8_HOMES = {
    "Language": ("shared/language.h", "display/core/i18n.h"),
    "AirspaceSnapshot": ("airspace/core/airspace_snapshot.h", "platform/core_link.h"),
    "ScanArea": ("opensky/core/scan_area.h", "opensky/core/opensky_client.h"),
    "computeScanArea": ("opensky/core/scan_area.h", "opensky/core/opensky_client.h"),
    "kUpdatePromptTimeoutMs": ("display/core/screens/service_screen.h",
                               "admin/core/firmware/release_check.h"),
    "AircraftInfo": ("shared/flight_info.h", "enrichment/core/aircraft_lookup.h"),
    "RouteInfo": ("shared/flight_info.h", "enrichment/core/route_lookup.h"),
    "AirportInfo": ("shared/flight_info.h", "enrichment/core/route_lookup.h"),
}

# Stage 8: (file from src/, layer it may no longer include) -- the .feature's Examples.
STAGE_8_DECOUPLED = [
    ("enrichment/core/place_names.h", "display/core"),
    ("opensky/core/opensky_quota.h", "display/core"),
    ("airspace/services/poll_pipeline.h", "platform"),
    ("airspace/core/tracked_flight.h", "enrichment/core"),
    ("display/adapters/lgfx/service_screen_draw.cpp", "admin/core"),
]

# Stage 8 (section 10 item 3): the limits next to MAIN_CPP_LINE_LIMIT.
NET_TASK_LINE_LIMIT = 300
SRC_FILE_LINE_LIMIT = 600

# Stage 8: function net_task.cpp no longer defines -> its file in src/main/.
STAGE_8_NET_TASK_PARTS = {
    "productionPorts": "net_enrichment.cpp",
    "buildReport": "net_perf.cpp",
    "perfIteration": "net_perf.cpp",
    "perfJsonNow": "net_perf.cpp",
    "markAppValidWhenStable": "net_rollback.cpp",
}

# Stage 8: what docs/architecture.md "Source layout" carries from the spec -- the first column
# of the section 5 table and the code names of the section 3 glossary. Written out here: the
# spec moves to spec/done/ when stage 8 ships.
STAGE_8_DEPENDENCY_ROWS = ["shared", "airspace/core", "enrichment/core", "opensky/core",
                           "display/core", "admin/core", "airspace/services", "display/services",
                           "admin/services", "platform", "*/adapters", "main/"]
STAGE_8_GLOSSARY_TERMS = ["Home", "ScanArea", "AircraftState", "TrackedFlight",
                          "AirspaceSnapshot", "Phase", "CpaPrediction", "AircraftInfo",
                          "RouteInfo", "AirportInfo", "OpenSkyQuota", "FeedHealth", "AlertId",
                          "Settings"]

# Stage 8: the module list CLAUDE.md "Code conventions" no longer has (it moved to the docs).
STAGE_8_MODULE_LIST_MARKERS = ["**Data, core 0:**", "**Settings and maintenance:**",
                               "**Shared / platform:**", "**Adapters (device halves):**",
                               "**Sky model", "**Display, core 1:**", "**UI flows"]

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
    return "src/" + include


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
            "src/display/adapters/lgfx/status_bar_draw.cpp": "display/adapters",
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

    # Scenario: Nagłówek w include/ nie należy już do żadnej warstwy
    def test_include_dir_headers_are_outside_any_layer(self):
        for path in ("include/LGFX_CYD.hpp", "include/x.h"):
            with self.subTest(path=path):
                self.assertIsNone(check_layers.classify(path))
        cases = {"src/display/adapters/lgfx/LGFX_CYD.hpp": "display/adapters",
                 "src/display/core/touch_calibration.h": "display/core"}
        for path, layer in cases.items():
            with self.subTest(path=path):
                self.assertEqual(layer, check_layers.classify(path))


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
        ("src/admin/core/", "admin/adapters/web_server.h", "admin/core -> admin/adapters"),
        ("src/admin/core/", "admin/services/x.h", "admin/core -> admin/services"),
        ("src/airspace/services/", "enrichment/services/x.h",
         "airspace/services -> enrichment/services"),
        ("src/admin/adapters/", "display/services/screenshot.h",
         "admin/adapters -> display/services"),
        ("src/display/core/", "main/net_task.h", "display/core -> main"),
        ("src/display/core/", "display/adapters/lgfx/LGFX_CYD.hpp",
         "display/core -> display/adapters"),
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
                found = check_layers.check_files(with_target(source, target, target_of(target),
                                                             line=3))
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

    # Edge case (plan "Test list"): what CI prints when there is nothing to report.
    def test_strict_on_a_clean_tree_prints_a_zero_total(self):
        with tempfile.TemporaryDirectory() as tmp:
            write_tree(tmp, {"src/platform/b.cpp": '#include "shared/s.h"\n',
                             "src/shared/s.h": ""})
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = check_layers.main(["--strict", "--root", tmp])
        self.assertEqual(0, code, out.getvalue())
        self.assertEqual("0 layer finding(s) in total\n", out.getvalue())

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
        # The tool tests name the old paths on purpose.
        paths += [p for p in sorted((ROOT / "tools").glob("*.py"))
                  if not p.name.startswith("test_")]
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
        files = check_layers.load_tree(ROOT)
        findings = check_layers.check_files(files)
        self.assertGreater(len(files), 100, "check_layers loaded almost nothing")
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

    # Scenario: #ifdef ARDUINO zostaje tylko w platform/ i adapters/
    def test_arduino_ifdef_only_in_platform_and_adapters(self):
        files = repo_sources("src")
        allowed, misplaced = [], []
        for path, text in files.items():
            hits = arduino_condition_lines(text)
            if not hits:
                continue
            layer = check_layers.classify(path) or ""
            ok = layer == "platform" or layer.endswith("/adapters")
            (allowed if ok else misplaced).extend(f"{path}:{n}" for n in hits)
        # Guard against a vacuous pass: platform/ keeps its NTP / FreeRTOS halves.
        self.assertTrue([h for h in allowed if h.startswith("src/platform/")],
                        "found no ARDUINO conditional at all, not even in src/platform/")
        self.assertEqual([], misplaced, f"{len(misplaced)} ARDUINO conditional(s) outside "
                         "platform/ and */adapters/:\n" + "\n".join(misplaced))

    # Scenario: Typy LovyanGFX i funkcje rysujące są tylko w display/adapters/
    def test_core_and_services_never_name_lovyangfx(self):
        files = repo_sources("src")
        pure = {p: t for p, t in files.items()
                if (check_layers.classify(p) or "").endswith(("/core", "/services"))
                or check_layers.classify(p) == "shared"}
        self.assertGreater(len(pure), 50)
        named = [f"{p}:{n}: {m}" for p, t in pure.items() for n, m in lovyangfx_names(t)]
        self.assertEqual([], named, f"{len(named)} LovyanGFX name(s) in pure code:\n"
                         + "\n".join(named[:60]))
        headers = {p: t for p, t in files.items() if p.endswith((".h", ".hpp"))}
        takers = sorted({p for p, t in headers.items() if LGFX_PARAM_RE.search(strip_comments(t))})
        self.assertTrue(takers, "no header declares a function taking LGFX &")
        outside = [p for p in takers if not p.startswith("src/display/adapters/")]
        self.assertEqual([], outside, "headers outside src/display/adapters/ with an LGFX & "
                                      "parameter")

    # Scenario Outline: Sprzętowa połowa modułu jest zdefiniowana w adapterze, a czysta zostaje
    # w core/
    def test_hardware_functions_are_defined_in_adapters(self):
        self.assertEqual(18, len(STAGE_4_ADAPTERS))
        sources = {p: t for p, t in repo_sources("src").items() if p.endswith(".cpp")}
        for function, (adapter, core) in STAGE_4_ADAPTERS.items():
            with self.subTest(function=function):
                defs = [f"{p}:{n}" for p, t in sources.items()
                        for n in declaration_lines(t, function, bodies_only=True)]
                self.assertEqual([f"src/{adapter}"], [d.rsplit(":", 1)[0] for d in defs],
                                 f"definitions of {function}: {defs or 'none'}")
                self.assertIn(f"src/{core}", sources, f"src/{core} is gone")
                core_text = sources[f"src/{core}"]
                self.assertEqual([], arduino_condition_lines(core_text),
                                 f"src/{core} still has an ARDUINO conditional")

    # Scenario Outline: Typ lub funkcja wyniesiona przy podziale ma jedną deklarację, w swoim
    # nowym nagłówku
    def test_stage_4_symbols_are_declared_in_their_new_home(self):
        self.assertEqual(14, len(STAGE_4_HOMES))
        headers = {p: t for p, t in repo_sources("src").items() if p.endswith((".h", ".hpp"))}
        self.assertGreater(len(headers), 30)
        for symbol, (new, old) in STAGE_4_HOMES.items():
            with self.subTest(symbol=symbol):
                hits = [f"{p}:{n}" for p, t in headers.items()
                        for n in declaration_lines(t, symbol)]
                self.assertFalse([h for h in hits if h.startswith(f"src/{old}:")],
                                 f"{symbol} still declared in src/{old}: {hits}")
                self.assertEqual([f"src/{new}"], [h.rsplit(":", 1)[0] for h in hits],
                                 f"declarations of {symbol}: {hits or 'none'}")

    # Scenario Outline: Testy czystych funkcji idą za nimi do pliku testów nowego modułu
    def test_stage_4_moved_tests_follow_their_code(self):
        self.assertEqual(21, len(STAGE_4_MOVED_TESTS))
        sources = {p[len("test/"):]: t for p, t in repo_sources("test").items()}
        for name, (new, old) in STAGE_4_MOVED_TESTS.items():
            with self.subTest(test=name):
                self.assertFalse((ROOT / "test" / old).exists(), f"test/{old} still exists")
                defs = [f"{p}:{n}" for p, t in sources.items() for n in defined_test_lines(t, name)]
                self.assertEqual([new], [d.rsplit(":", 1)[0] for d in defs],
                                 f"definitions of {name}: {defs or 'none'}")
                self.assertEqual(1, run_test_calls(sources[new], name),
                                 f"RUN_TEST({name}) in test/{new}")
        # Each new file is registered in its suite's main.cpp, so its RUN_TESTs run.
        for new in sorted({new for new, _ in STAGE_4_MOVED_TESTS.values()}):
            module = Path(new).stem[:-len("_test")]
            suite = Path(new).parent.as_posix()
            with self.subTest(suite_main=suite):
                main = strip_comments(sources.get(f"{suite}/main.cpp", ""))
                self.assertRegex(main, r"\bRUN_MODULE\(\s*" + module + r"\s*\)\s*\)\s*run_"
                                 + module + r"_tests\(\)", f"test/{suite}/main.cpp")

    # Scenario: Konfiguracja LovyanGFX i kalibracja dotyku leżą w kontekście display, nie
    # w include/
    def test_board_config_headers_live_in_the_display_context(self):
        include_dir = ROOT / "include"
        left = sorted(p.relative_to(ROOT).as_posix() for p in include_dir.rglob("*")
                      if p.is_file() and p.suffix in (".h", ".hpp")) if include_dir.is_dir() else []
        self.assertEqual([], left, "headers still in include/")
        lgfx = ROOT / "src/display/adapters/lgfx/LGFX_CYD.hpp"
        calibration = ROOT / "src/display/core/touch_calibration.h"
        self.assertTrue(lgfx.is_file(), "src/display/adapters/lgfx/LGFX_CYD.hpp")
        self.assertTrue(calibration.is_file(), "src/display/core/touch_calibration.h")
        lgfx_text = lgfx.read_text(encoding="utf-8", errors="replace")
        self.assertRegex(strip_comments(lgfx_text), r"\bclass\s+LGFX\s*:\s*public\s+lgfx::",
                         "class LGFX in LGFX_CYD.hpp")
        wanted = "display/core/touch_calibration.h"
        quoted = [i.name for i in check_layers.find_includes(lgfx_text) if not i.angle]
        self.assertIn(wanted, quoted, "LGFX_CYD.hpp includes")
        touch = repo_sources("src/display/core")
        readers = [p for p, t in touch.items() if "mapRawTouch" in strip_comments(t)
                   and wanted in [i.name for i in check_layers.find_includes(t) if not i.angle]]
        self.assertTrue(readers, f"no display/core file with mapRawTouch() includes {wanted}")

    # Scenario Outline: Raport warstw nie zgłasza już SDK, adapterów ani platform/ dołączanych
    # z core/ i z testów
    def test_stage_4_removes_sdk_and_adapter_findings_from_core(self):
        files = check_layers.load_tree(ROOT)
        findings = check_layers.check_files(files)
        self.assertGreater(len(files), 100, "check_layers loaded almost nothing")
        for source, target in STAGE_4_GONE_PAIRS:
            with self.subTest(source=source, target=target):
                hits = [f for f in findings if f.message.startswith(f"{source} -> {target} ")]
                self.assertEqual([], hits, show(hits, limit=len(hits)))


    # Scenario: main.cpp ma mniej niż 250 linii (§8 pkt 5, §10 pkt 3)
    def test_main_cpp_is_under_250_lines(self):
        text = (ROOT / "src" / "main" / "main.cpp").read_text(encoding="utf-8", errors="replace")
        lines = len(text.splitlines())
        self.assertGreater(lines, 50, "src/main/main.cpp looks empty")
        self.assertLess(lines, MAIN_CPP_LINE_LIMIT, f"src/main/main.cpp has {lines} lines")

    # Scenario: main.cpp nie trzyma stanu automatów w zmiennych static
    def test_main_cpp_keeps_no_flow_state(self):
        # The pattern itself: file scope and function scope, const / constexpr and functions
        # don't count.
        sample = ("static bool a = false;\nstatic uint32_t b;\nstatic const int c = 1;\n"
                  "static constexpr uint32_t d = 2;\nstatic bool e() { return true; }\n"
                  "void f() {\n  static long g = -2;\n  static std::string h;\n}\n"
                  "// static int i = 0;\nstatic unsigned long j = 0;\nstatic size_t k{0};\n")
        self.assertEqual(["a", "b", "g", "h", "j", "k"], static_state_names(sample))
        text = (ROOT / "src" / "main" / "main.cpp").read_text(encoding="utf-8", errors="replace")
        state = [n for n in static_state_names(text) if n not in MAIN_CPP_STATIC_ALLOWED]
        self.assertEqual([], state, "plain static variables left in src/main/main.cpp")

    # Scenario Outline: Automat z main.cpp ma nowy dom w display/services/ i nie jest już
    # zdefiniowany w main.cpp
    def test_stage_5_flows_left_main_cpp(self):
        self.assertEqual(10, len(STAGE_5_FLOWS))
        main = (ROOT / "src" / "main" / "main.cpp").read_text(encoding="utf-8", errors="replace")
        headers = {p: t for p, t in repo_sources("src").items() if p.endswith((".h", ".hpp"))}
        self.assertGreater(len(headers), 30)
        for old, symbol, header in STAGE_5_FLOWS:
            with self.subTest(old=old, symbol=symbol):
                self.assertEqual([], declaration_lines(main, old, bodies_only=True),
                                 f"src/main/main.cpp still defines {old}()")
                hits = [f"{p}:{n}" for p, t in headers.items()
                        for n in declaration_lines(t, symbol)]
                self.assertEqual([f"src/{header}"], [h.rsplit(":", 1)[0] for h in hits],
                                 f"declarations of {symbol}: {hits or 'none'}")

    # Scenario: Trasa POST /save tylko zbiera pola, zapisuje i odpowiada — logika formularza
    # jest w admin/core/settings_form
    def test_web_server_save_route_has_no_form_logic(self):
        text = (ROOT / "src" / "admin" / "adapters" / "web_server.cpp").read_text(
            encoding="utf-8", errors="replace")
        body = function_body(text, "handleSave")
        self.assertIsNotNone(body, "no void handleSave() { ... } in web_server.cpp")
        lines = len(body.strip("\n").splitlines())
        self.assertLessEqual(lines, SAVE_ROUTE_LINE_LIMIT, f"handleSave() has {lines} lines")
        called = [name for name in SAVE_ROUTE_FORBIDDEN
                  if re.search(r"\b" + re.escape(name) + r"\s*\(", body)]
        self.assertEqual([], called, "handleSave() still parses the form itself")
        self.assertRegex(body, r"\bapplySettingsForm\s*\(",
                         "handleSave() calls applySettingsForm()")
        form = (ROOT / "src" / "admin" / "core" / "settings_form.h").read_text(
            encoding="utf-8", errors="replace")
        self.assertEqual(1, len(declaration_lines(form, "applySettingsForm")),
                         "applySettingsForm() declared in src/admin/core/settings_form.h")

    # Scenario Outline: Każdy nowy moduł z §6 ma plik testów w grupie swojego kontekstu,
    # zarejestrowany w main.cpp grupy
    def test_stage_5_services_have_their_tests(self):
        self.assertEqual(8, len(STAGE_5_SERVICES))
        for module, test_file, name in STAGE_5_SERVICES:
            with self.subTest(module=module):
                self.assertTrue((ROOT / "src" / f"{module}.h").is_file(), f"src/{module}.h")
                path = ROOT / "test" / test_file
                self.assertTrue(path.is_file(), f"test/{test_file}")
                calls = strip_comments(path.read_text(encoding="utf-8",
                                                      errors="replace")).count("RUN_TEST(")
                self.assertGreaterEqual(calls, 3, f"RUN_TEST calls in test/{test_file}")
                suite = Path(test_file).parent.as_posix()
                main = strip_comments((ROOT / "test" / suite / "main.cpp").read_text(
                    encoding="utf-8", errors="replace"))
                self.assertRegex(main, r"\bRUN_MODULE\(\s*" + name + r"\s*\)\s*\)\s*run_"
                                 + name + r"_tests\(\)", f"test/{suite}/main.cpp")

    # Scenario Outline: Testy mostu zrzutu ekranu idą za kodem do test_display, testy BMP
    # zostają w test_admin
    def test_stage_5_moved_tests_follow_their_code(self):
        self.assertEqual(6, len(STAGE_5_MOVED_TESTS))
        sources = {p[len("test/"):]: t for p, t in repo_sources("test").items()}
        for name, (new, old) in STAGE_5_MOVED_TESTS.items():
            with self.subTest(test=name):
                old_text = sources.get(old, "")
                self.assertEqual(([], 0), (defined_test_lines(old_text, name),
                                           run_test_calls(old_text, name)),
                                 f"{name} still defined / run in test/{old}")
                defs = [f"{p}:{n}" for p, t in sources.items() for n in defined_test_lines(t, name)]
                self.assertEqual([new], [d.rsplit(":", 1)[0] for d in defs],
                                 f"definitions of {name}: {defs or 'none'}")
                self.assertEqual(1, run_test_calls(sources[new], name),
                                 f"RUN_TEST({name}) in test/{new}")

    # Scenario Outline: Settings składa się z obiektów wartości kontekstów, a każdy obiekt leży
    # w swoim kontekście
    def test_config_is_built_from_context_objects(self):
        path = ROOT / "src" / "admin" / "core" / "settings.h"
        self.assertTrue(path.is_file(), "src/admin/core/settings.h")
        store = path.read_text(encoding="utf-8", errors="replace")
        members = struct_members(store, "Settings")
        self.assertIsNotNone(members, "no struct Settings { ... } in src/admin/core/settings.h")
        self.assertEqual({field: t for field, (t, _, _) in STAGE_6_SETTINGS.items()},
                         {name: t for t, name in members}, "the fields of Settings")
        self.assertEqual(len(STAGE_6_SETTINGS), len(members), f"Settings' fields: {members}")
        headers = {p: t for p, t in repo_sources("src").items() if p.endswith((".h", ".hpp"))}
        for field, (type_, header, fields) in STAGE_6_SETTINGS.items():
            with self.subTest(field=field, type=type_):
                hits = [f"{p}:{n}" for p, t in headers.items() for n in declaration_lines(t, type_)]
                self.assertEqual([f"src/{header}"], [h.rsplit(":", 1)[0] for h in hits],
                                 f"declarations of {type_}: {hits or 'none'}")
                inner = struct_members(headers[f"src/{header}"], type_)
                self.assertCountEqual(fields, [name for _, name in inner or []],
                                      f"the fields of {type_}")

    # Scenario: Klucze NVS i ich typy są te same co przed etapem
    def test_settings_nvs_keys_and_types_unchanged(self):
        self.assertEqual(28, len(STAGE_6_NVS_KEYS))
        code = strip_comments((ROOT / "src" / "admin" / "adapters" / "settings_nvs.cpp")
                              .read_text(encoding="utf-8", errors="replace"))
        self.assertRegex(code, r'\bkNamespace\[\]\s*=\s*"cydsky"')
        seen = {}
        for m in re.finditer(r'\bprefs\.(get|put)(\w+)\(\s*"(\w+)"', code):
            seen.setdefault(m.group(3), set()).add((m.group(1), m.group(2)))
        self.assertEqual(sorted(STAGE_6_NVS_KEYS), sorted(seen), "the NVS keys")
        for key, type_ in STAGE_6_NVS_KEYS.items():
            with self.subTest(key=key):
                self.assertEqual({("get", type_), ("put", type_)}, seen[key])

    # Edge case (review finding 2): the keys and types alone don't see a same-type swap
    # (`lang` <-> `wlang`, `bl_min` <-> `bl_max`, `nt_from` <-> `nt_to`) in loadSettings() or
    # saveSettings(), and settings_nvs.cpp has no native test. Each key loads into (`cfg.X.y =
    # ... prefs.get...("key"`) and saves from (`prefs.put...("key", ... sanitized.X.y`) the
    # field the table pins.
    def test_settings_nvs_keys_load_into_and_save_from_their_field(self):
        self.assertEqual(sorted(STAGE_6_NVS_KEYS), sorted(STAGE_6_NVS_FIELDS))
        code = strip_comments((ROOT / "src" / "admin" / "adapters" / "settings_nvs.cpp")
                              .read_text(encoding="utf-8", errors="replace"))
        load = definition_body(code, "Settings loadSettings()")
        save = definition_body(code, "void saveSettings(const Settings &cfg)")
        loaded, saved = {}, {}
        for statement in load.split(";"):
            for key in re.findall(r'\bprefs\.get\w+\(\s*"(\w+)"', statement):
                loaded.setdefault(key, []).extend(
                    re.findall(r"\bcfg\.(\w+\.\w+)\s*=(?!=)", statement))
        for statement in save.split(";"):
            for key in re.findall(r'\bprefs\.put\w+\(\s*"(\w+)"', statement):
                saved.setdefault(key, []).extend(re.findall(r"\bsanitized\.(\w+\.\w+)", statement))
        self.assertEqual(sorted(STAGE_6_NVS_FIELDS), sorted(loaded), "keys loadSettings() reads")
        self.assertEqual(sorted(STAGE_6_NVS_FIELDS), sorted(saved), "keys saveSettings() writes")
        for key, field in STAGE_6_NVS_FIELDS.items():
            with self.subTest(key=key):
                self.assertEqual([field], loaded[key], f"the field loadSettings() fills from {key}")
                self.assertEqual([field], saved[key], f"the field saveSettings() writes to {key}")

    # Scenario: Poza admin/ ustawienia dołączają tylko core_link i korzenie kompozycji
    # (§10 pkt 4)
    def test_only_admin_roots_and_core_link_include_settings(self):
        includers = sorted(p for p, t in repo_sources("src").items()
                           if "admin/core/settings.h"
                           in [i.name for i in check_layers.find_includes(t) if not i.angle])
        self.assertGreater(len(includers), 3, f"includers of settings.h: {includers}")
        outside = [p for p in includers if not p.startswith(("src/admin/", "src/main/"))
                   and p != "src/platform/core_link.h"]
        self.assertEqual([], outside, "settings.h included outside admin/, the roots and "
                                      "core_link.h")
        self.assertIn("src/platform/core_link.h", includers)

    # Scenario: Moduły innych kontekstów nie znają typu Settings, nawet z deklaracji zapowiadającej
    def test_config_type_named_only_in_admin_roots_and_core_link(self):
        files = repo_sources("src/airspace", "src/enrichment", "src/opensky", "src/display",
                             "src/shared")
        self.assertGreater(len(files), 50)
        hits = [f"{p}:{code.count(chr(10), 0, m.start()) + 1}"
                for p, t in files.items() for code in [strip_comments(t)]
                for m in re.finditer(r"\bSettings\b", code)]
        self.assertEqual([], hits, f"{len(hits)} use(s) of Settings outside admin/, the roots and "
                         "core_link:\n" + "\n".join(hits))

    # Scenario Outline: Funkcje innych kontekstów dostają tylko swoje obiekty ustawień
    def test_context_functions_take_their_settings_objects(self):
        self.assertEqual(10, len(STAGE_6_SIGNATURES))
        for header, symbol, expected in STAGE_6_SIGNATURES:
            with self.subTest(symbol=symbol):
                path = ROOT / "src" / header
                self.assertTrue(path.is_file(), f"src/{header}")
                text = path.read_text(encoding="utf-8", errors="replace")
                members = struct_members(text, symbol)
                if members is not None:
                    got = [[f"{t} {n}" for t, n in members if is_settings_type(t)]]
                    flat = {"homeLat", "homeLon", "nearAirportKm", "climbRateMps"}
                    self.assertEqual(set(), flat & {n for _, n in members},
                                     f"flat settings left in {symbol}")
                else:
                    decls = parameter_types(text, symbol)
                    self.assertTrue(decls, f"no declaration of {symbol}() in src/{header}")
                    got = [[t for t in d if is_settings_type(t)] for d in decls]
                for g in got:
                    self.assertEqual(expected, g, f"settings parameters of {symbol}")

    # Scenario: Ekran czyta z NetState tylko obiekty ustawień, które go dotyczą
    def test_display_reads_only_its_settings_from_net_state(self):
        reads = []
        for p, t in repo_sources("src/display").items():
            code = strip_comments(t)
            for m in re.finditer(r"\bsettings(?:\s*\(\s*\))?\s*->\s*(\w+)(?:\s*\.\s*(\w+))?"
                                 r"|\bcfg\s*\(\s*\)\s*\.\s*(\w+)(?:\s*\.\s*(\w+))?", code):
                obj, field = m.group(1) or m.group(3), m.group(2) or m.group(4)
                reads.append((f"{p}:{code.count(chr(10), 0, m.start()) + 1}", obj, field))
        self.assertGreaterEqual(len(reads), 4, "found too few settings-> reads in src/display/")
        wrong = [f"{where}: settings->{obj}" for where, obj, _ in reads
                 if obj not in STAGE_6_DISPLAY_SETTINGS]
        self.assertEqual([], wrong, "the display reads settings that aren't its own")
        update = [f"{where}: settings->update.{field}" for where, obj, field in reads
                  if obj == "update" and field not in STAGE_6_DISPLAY_UPDATE_FIELDS]
        self.assertEqual([], update, "the display reads update settings beyond the skipped tag")

    # Scenario: Adaptery OpenSky i podświetlenia nie sięgają już po ustawienia z admin/core
    def test_stage_6_removes_the_settings_findings_of_adapters(self):
        files = check_layers.load_tree(ROOT)
        findings = check_layers.check_files(files) + check_layers.full_path_findings(files)
        self.assertGreater(len(files), 100, "check_layers loaded almost nothing")
        for path in STAGE_6_GONE_FINDINGS:
            with self.subTest(path=path):
                self.assertTrue((ROOT / "src" / path).is_file(), f"src/{path}")
                hits = [f for f in findings if f.path == f"src/{path}"]
                self.assertEqual([], hits, show(hits, limit=len(hits)))

    # Scenario: Moduł ustawień nazywa się settings, a jego testy settings_test.cpp
    def test_settings_module_is_admin_core_settings(self):
        for name in ("settings.h", "settings.cpp"):
            self.assertTrue((ROOT / "src" / "admin" / "core" / name).is_file(),
                            f"src/admin/core/{name}")
        sources = repo_sources("src", "test")
        self.assertGreater(len(sources), 100)
        left = [p for p in sources if "config_store" in Path(p).name]
        self.assertEqual([], left, "files still named config_store")
        includes = [f"{p}:{i.line}: {i.name}" for p, t in sources.items()
                    for i in check_layers.find_includes(t) if "config_store" in i.name]
        self.assertEqual([], includes, "includes that still name config_store")
        tests = ROOT / "test" / "test_admin" / "settings_test.cpp"
        self.assertTrue(tests.is_file(), "test/test_admin/settings_test.cpp")
        self.assertRegex(strip_comments(tests.read_text(encoding="utf-8", errors="replace")),
                         r"\bvoid\s+run_settings_tests\s*\(\s*\)\s*\{")
        main = strip_comments((ROOT / "test" / "test_admin" / "main.cpp").read_text(
            encoding="utf-8", errors="replace"))
        self.assertRegex(main, r"\bRUN_MODULE\(\s*settings\s*\)\s*\)\s*run_settings_tests\(\)")
        self.assertNotRegex(main, r"\bconfig_store\b")

    # Scenario: NetState niesie ustawienia pod nazwą settings
    def test_net_state_carries_settings(self):
        text = (ROOT / "src" / "platform" / "core_link.h").read_text(encoding="utf-8",
                                                                   errors="replace")
        members = struct_members(text, "NetState")
        self.assertIsNotNone(members, "no struct NetState { ... } in src/platform/core_link.h")
        self.assertIn(("std::shared_ptr<const Settings>", "settings"), members)
        self.assertIn(("uint32_t", "settingsSeq"), members)
        names = [name for _, name in members]
        self.assertNotIn("cfg", names)
        self.assertNotIn("cfgSeq", names)

    # Scenario: Funkcje serwera WWW nazywają się jak jego moduł
    def test_web_server_functions_are_named_after_the_module(self):
        header = strip_comments((ROOT / "src" / "admin" / "adapters" / "web_server.h").read_text(
            encoding="utf-8", errors="replace"))
        # Functions declared at file scope (column 0); struct members are indented.
        declared = re.findall(r"(?m)^(?!using\b|struct\b|class\b|enum\b|#)"
                              r"(?:[A-Za-z_][\w:<>]*[\s*&]+)+([A-Za-z_]\w*)\s*\(", header)
        self.assertGreaterEqual(len(declared), 8, f"functions in web_server.h: {declared}")
        self.assertEqual([], [n for n in declared if not n.startswith("webServer")],
                         "web_server.h functions not named webServer...")
        self.assertCountEqual(["webServer" + s for s in WEB_SERVER_FUNCTIONS], declared)
        hits = [f"{p}:{code.count(chr(10), 0, m.start()) + 1}: {m.group(0)}"
                for p, t in repo_sources("src").items() for code in [strip_comments(t)]
                for m in re.finditer(r"\bconfigPortal[A-Z]\w*", code)]
        self.assertEqual([], hits, "configPortal...() left in src/")
        wifi = strip_comments((ROOT / "src" / "platform" / "wifi_manager.cpp").read_text(
            encoding="utf-8", errors="replace"))
        self.assertRegex(wifi, r"\.setConfigPortalBlocking\s*\(",
                         "wifi_manager.cpp no longer calls WiFiManager's setConfigPortalBlocking()")

    # Scenario Outline: Typ lub stała, przez które zależności szły w złą stronę, leżą w swoim
    # kontekście
    def test_stage_8_symbols_are_declared_in_their_new_home(self):
        self.assertEqual(8, len(STAGE_8_HOMES))
        headers = {p: t for p, t in repo_sources("src").items() if p.endswith((".h", ".hpp"))}
        self.assertGreater(len(headers), 30)
        for symbol, (new, old) in STAGE_8_HOMES.items():
            with self.subTest(symbol=symbol):
                hits = [f"{p}:{n}" for p, t in headers.items()
                        for n in declaration_lines(t, symbol) + constant_lines(t, symbol)]
                self.assertFalse([h for h in hits if h.startswith(f"src/{old}:")],
                                 f"{symbol} still declared in src/{old}: {hits}")
                self.assertEqual([f"src/{new}"], [h.rsplit(":", 1)[0] for h in hits],
                                 f"declarations of {symbol}: {hits or 'none'}")

    # Edge case (plan "Test list"): shared/ sees only shared/, and these two headers hold plain
    # types, so they need nothing but the standard library (spec 12 section 4.6).
    def test_stage_8_shared_headers_include_only_the_standard_library(self):
        for header in ("shared/language.h", "shared/flight_info.h"):
            with self.subTest(header=header):
                path = ROOT / "src" / header
                self.assertTrue(path.is_file(), f"src/{header}")
                includes = check_layers.find_includes(path.read_text(encoding="utf-8",
                                                                     errors="replace"))
                foreign = [i.name for i in includes
                           if not i.angle or check_layers.is_sdk_header(i.name)
                           or i.name in ("ArduinoJson.h", "unity.h")]
                self.assertEqual([], foreign, f"src/{header} includes more than the standard "
                                              "library")

    # Scenario: Pomocnicy /screenshot.bmp (BMP, ?screen=) należą do admin/core, bo używa ich
    # panel WWW
    def test_screenshot_module_is_in_admin_core(self):
        services = sorted(p.name for p in (ROOT / "src/display/services").glob("screenshot*"))
        self.assertEqual(["screenshot_bridge.cpp", "screenshot_bridge.h"], services,
                         "screenshot files in src/display/services/")
        for name in ("screenshot.h", "screenshot.cpp"):
            with self.subTest(file=name):
                self.assertTrue((ROOT / "src/admin/core" / name).is_file(),
                                f"src/admin/core/{name}")
        sources = repo_sources("src", "test")
        self.assertGreater(len(sources), 100)
        old = [f"{p}:{i.line}" for p, t in sources.items() for i in check_layers.find_includes(t)
               if i.name == "display/services/screenshot.h"]
        self.assertEqual([], old, "includes of display/services/screenshot.h")

    # Scenario: Serwer WWW nie sięga po dane innych kontekstów
    def test_web_server_includes_only_its_own_context(self):
        adapters = ROOT / "src/admin/adapters"
        cpp_files = sorted(adapters.glob("web_server*.cpp"))
        self.assertTrue(cpp_files, "no web_server*.cpp in src/admin/adapters/")
        for path in cpp_files:
            with self.subTest(file=path.name):
                text = path.read_text(encoding="utf-8", errors="replace")
                quoted = [i.name for i in check_layers.find_includes(text) if not i.angle]
                self.assertGreater(len(quoted), 3, f"too few includes found in {path.name}")
                foreign = [n for n in quoted
                           if not n.startswith(("admin/", "shared/", "platform/"))]
                self.assertEqual([], foreign, f"includes of other contexts in {path.name}")
        web_server = strip_comments((adapters / "web_server.cpp").read_text(encoding="utf-8",
                                                                             errors="replace"))
        self.assertTrue(re.search(r"\bpanelLiveInputs\s*\(", web_server),
                        "web_server.cpp doesn't call panelLiveInputs()")
        page = (ROOT / "src/admin/core/config_page.h").read_text(encoding="utf-8",
                                                                 errors="replace")
        self.assertEqual([["const Settings &"]], parameter_types(page, "panelLiveInputs"),
                         "panelLiveInputs() in src/admin/core/config_page.h")
        members = struct_members(page, "PanelLiveInputs")
        self.assertIsNotNone(members, "no struct PanelLiveInputs in admin/core/config_page.h")
        self.assertCountEqual(["quota", "auth", "light"], [name for _, name in members],
                              "the fields of PanelLiveInputs")

    # Scenario: Dane z urządzenia z kilku kontekstów składa korzeń kompozycji
    def test_foreign_device_inputs_are_wired_in_the_composition_root(self):
        sources = {p: t for p, t in repo_sources("src").items() if p.endswith(".cpp")}
        self.assertGreater(len(sources), 30)
        for function in ("deviceAlertInputs", "panelLiveInputs"):
            with self.subTest(function=function):
                defs = [p for p, t in sources.items()
                        for _ in declaration_lines(t, function, bodies_only=True)]
                self.assertEqual(["src/main/alert_sources.cpp"], defs,
                                 f"definitions of {function}()")
        self.assertFalse((ROOT / "src/admin/adapters/alert_sources.cpp").exists(),
                         "src/admin/adapters/alert_sources.cpp still exists")

    # Scenario Outline: Plik, który łamał tabelę z §5, dołącza już tylko dozwolone warstwy
    def test_layers_are_decoupled_in_the_named_headers(self):
        self.assertEqual(5, len(STAGE_8_DECOUPLED))
        files = check_layers.load_tree(ROOT)
        known = set(files)
        for name, forbidden in STAGE_8_DECOUPLED:
            with self.subTest(file=name):
                path = f"src/{name}"
                self.assertIn(path, files)
                quoted = [i.name for i in check_layers.find_includes(files[path]) if not i.angle]
                self.assertTrue(quoted, f"no quoted include found in {path}")
                layers = {n: check_layers.classify(check_layers.resolve_include(path, n, known)
                                                   or "") for n in quoted}
                hits = sorted(n for n, layer in layers.items() if layer == forbidden)
                self.assertEqual([], hits, f"{path} includes {forbidden}")

    # Scenario Outline: Pliki w src/ mieszczą się w limitach z §10 pkt 3
    def test_source_files_stay_under_the_section_10_limits(self):
        src = ROOT / "src"
        files = [p for p in sorted(src.rglob("*")) if p.is_file()]
        self.assertGreater(len(files), 100)
        with self.subTest(limit="every file <= 600"):
            over = [f"{p.relative_to(ROOT).as_posix()}: {wc_lines(p)}" for p in files
                    if wc_lines(p) > SRC_FILE_LINE_LIMIT]
            self.assertEqual([], over, "files in src/ over 600 lines")
        for name, limit in (("main.cpp", MAIN_CPP_LINE_LIMIT), ("net_task.cpp", NET_TASK_LINE_LIMIT)):
            with self.subTest(file=name):
                lines = wc_lines(src / "main" / name)
                self.assertGreater(lines, 50, f"src/main/{name} looks empty")
                self.assertLess(lines, limit, f"src/main/{name} has {lines} lines")

    # Scenario: Pomiar, porty hexdb.io i oznaczanie obrazu jako ważnego są w osobnych modułach
    # korzenia kompozycji
    def test_net_task_parts_live_in_the_composition_root(self):
        sources = {p: t for p, t in repo_sources("src/main").items() if p.endswith(".cpp")}
        self.assertIn("src/main/net_task.cpp", sources)
        for function, home in STAGE_8_NET_TASK_PARTS.items():
            with self.subTest(function=function):
                defs = [p for p, t in sources.items()
                        for _ in declaration_lines(t, function, bodies_only=True)]
                self.assertEqual([f"src/main/{home}"], defs, f"definitions of {function}()")
        for module in ("net_perf", "net_enrichment", "net_rollback", "web_server_firmware"):
            with self.subTest(net_module=module):
                self.assertIn(module, check_comments.NET_MODULES)

    # Scenario: Strona ustawień jest podzielona według zakładek
    def test_config_page_is_split_by_tab(self):
        core = ROOT / "src/admin/core"
        page = (core / "config_page.cpp").read_text(encoding="utf-8", errors="replace")
        self.assertEqual([], declaration_lines(page, "Writer"), "Writer still in config_page.cpp")
        writer = core / "config_page_writer.h"
        self.assertTrue(writer.is_file(), "src/admin/core/config_page_writer.h")
        self.assertEqual(1, len(declaration_lines(writer.read_text(encoding="utf-8",
                                                                    errors="replace"), "Writer")),
                         "class Writer in config_page_writer.h")
        tabs = core / "config_page_tabs.cpp"
        self.assertTrue(tabs.is_file(), "src/admin/core/config_page_tabs.cpp")
        tabs_text = tabs.read_text(encoding="utf-8", errors="replace")
        for tab in ("Location", "OpenSky", "General", "Backup", "Firmware"):
            with self.subTest(tab=tab):
                self.assertEqual(1, len(tab_function_names(tabs_text, tab)),
                                 f"functions for the {tab} tab: "
                                 f"{tab_function_names(tabs_text, tab)}")
        includers = sorted(p for p, t in repo_sources("src").items()
                           for i in check_layers.find_includes(t)
                           if i.name == "admin/core/web_assets.h")
        self.assertEqual(["src/admin/core/config_page.cpp"], includers,
                         "includers of admin/core/web_assets.h")

    # Scenario: Po podziale serwera WWW każda trasa ma dalej swoje zabezpieczenia
    def test_web_server_routes_keep_their_guards(self):
        adapters = ROOT / "src/admin/adapters"
        firmware = adapters / "web_server_firmware.cpp"
        texts = [(adapters / "web_server.cpp").read_text(encoding="utf-8", errors="replace")]
        if firmware.is_file():
            texts.append(firmware.read_text(encoding="utf-8", errors="replace"))
        code = strip_comments("\n".join(texts))
        routes = route_registrations(code)
        self.assertGreaterEqual(len(routes), 14, f"routes found: {routes}")
        self.assertGreaterEqual(len([r for r in routes if r[1] == "HTTP_POST"]), 8)
        self.assertGreaterEqual(len([r for r in routes if r[3]]), 3, "upload routes")
        self.assertEqual([], route_guard_problems(code), "routes that lost a guard")
        # Last, so that a failure here means only that the split hasn't happened.
        self.assertTrue(firmware.is_file(), "src/admin/adapters/web_server_firmware.cpp")

    # Scenario: python tools/check_layers.py --strict przechodzi na repozytorium
    def test_strict_layer_check_passes_on_the_repository(self):
        code, out, _ = run_cli(ROOT, "--strict")
        self.assertIn("0 layer finding(s) in total", out)
        self.assertEqual(0, code, out)

    # Scenario: Reguły i słownik są w docs/architecture.md, a CLAUDE.md tylko do nich odsyła
    def test_architecture_doc_carries_the_rules_and_claude_md_points_to_it(self):
        doc = (ROOT / "docs/architecture.md").read_text(encoding="utf-8")
        layout = markdown_section(doc, "## Source layout")
        self.assertIsNotNone(layout, 'no "## Source layout" heading in docs/architecture.md')
        for context in check_layers.CONTEXTS:
            with self.subTest(context=context):
                self.assertIn(f"`{context}`", layout)
        rows = [line for line in layout.splitlines() if line.lstrip().startswith("|")]
        first_cells = [line.strip().strip("|").split("|")[0].replace("`", "").strip()
                       for line in rows]
        for row in STAGE_8_DEPENDENCY_ROWS:
            with self.subTest(dependency_row=row):
                self.assertTrue([c for c in first_cells if c.startswith(row)],
                                f"no row of the dependency table starts with {row}")
        for term in STAGE_8_GLOSSARY_TERMS:
            with self.subTest(glossary=term):
                self.assertTrue([line for line in rows if f"`{term}" in line],
                                f"no table row in Source layout names `{term}`")
        path = ROOT / "CLAUDE.md"
        claude = path.read_text(encoding="utf-8")
        section = markdown_section(claude, "## Code conventions")
        self.assertIsNotNone(section, 'no "## Code conventions" heading in CLAUDE.md')
        self.assertIn('docs/architecture.md "Source layout', section)
        for marker in STAGE_8_MODULE_LIST_MARKERS:
            with self.subTest(module_list=marker):
                self.assertNotIn(marker, section)
        self.assertLess(wc_lines(path), 250, "CLAUDE.md")

    # Scenario: Jednorazowe narzędzia migracji są usunięte
    def test_one_off_migration_tools_are_gone(self):
        tools = ROOT / "tools"
        workflow = (ROOT / ".github/workflows/firmware.yml").read_text(encoding="utf-8")
        for name in ("rewrite_includes", "rename_symbols"):
            with self.subTest(tool=name):
                self.assertFalse((tools / f"{name}.py").exists(), f"tools/{name}.py")
                self.assertFalse((tools / f"test_{name}.py").exists(), f"tools/test_{name}.py")
                self.assertNotIn(name, workflow, ".github/workflows/firmware.yml")
        for name in ("check_layers.py", "check_comments.py", "embed_assets.py"):
            with self.subTest(kept=name):
                self.assertTrue((tools / name).is_file(), f"tools/{name}")
        self.assertTrue(check_comments.RENAMED_SYMBOLS, "check_comments.RENAMED_SYMBOLS")


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


def wc_lines(path):
    """Lines of the file as `wc -l` counts them: its newlines, blank lines included."""
    return Path(path).read_bytes().count(b"\n")


def constant_lines(text, symbol):
    """Line numbers where `text` defines the constant `symbol` (`constexpr uint32_t kX = 1;`,
    `static const int kX{1};`). Comments are ignored; a use of the name doesn't count."""
    code = strip_comments(text)
    return [code.count("\n", 0, m.start()) + 1 for m in
            re.finditer(r"(?m)^[ \t]*(?:(?:static|inline)\s+)*(?:constexpr|const)\s+[\w:]+\s+"
                        + re.escape(symbol) + r"\s*[={]", code)]


def markdown_section(text, heading):
    """The text under the first line of `text` starting with `heading`, up to the next heading
    of the same level, or None."""
    level = heading.split(" ", 1)[0]
    out, inside = [], False
    for line in text.splitlines():
        if inside and line.startswith(level + " "):
            break
        if inside:
            out.append(line)
        elif line.startswith(heading):
            inside = True
    return "\n".join(out) if inside else None


def tab_function_names(text, tab):
    """Names of the functions defined in `text` that are about the settings tab `tab`: the name
    holds both the tab's name and "tab", in any case (`locationTab`, `renderOpenSkyTab`)."""
    names = re.findall(r"(?m)^[ \t]*(?:[\w:<>]+[\s*&]+)+(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{",
                       strip_comments(text))
    return [n for n in names if tab.lower() in n.lower() and "tab" in n.lower()]


_ROUTE_RE = re.compile(r'\bserver\.on\s*\(\s*"([^"]+)"\s*,\s*(HTTP_\w+)\s*,\s*([^;]*?)\)\s*;')


def route_registrations(code):
    """[(url, method, handler expression, upload callback or None)] of the `server.on(...)`
    calls in `code` (comments already blanked)."""
    out = []
    for m in _ROUTE_RE.finditer(code):
        handler, _, upload = m.group(3).partition(",")
        out.append((m.group(1), m.group(2), handler.strip(), upload.strip() or None))
    return out


def route_guard_problems(code):
    """What breaks the web rules in `code`, the routes' source with comments blanked (CLAUDE.md
    "Pitfalls"): a `server.on` this parser can't read or without secured<>, a POST handler
    that doesn't start with requireSameOrigin() before authorize(), an upload callback that
    doesn't start with the multipart check, a not-found handler without secured<>."""
    problems = []
    routes = route_registrations(code)
    calls = len(re.findall(r"\bserver\.on\s*\(", code))
    if calls != len(routes):
        problems.append(f"{calls} server.on() calls, {len(routes)} readable")
    for url, method, handler, upload in routes:
        m = re.fullmatch(r"secured<\s*(\w+)\s*>", handler)
        if m is None:
            problems.append(f"{url}: {handler} without secured<>")
            continue
        if method == "HTTP_POST":
            body = function_body(code, m.group(1))
            if body is None:
                problems.append(f"{url}: no definition of {m.group(1)}()")
            elif (not re.match(r"\s*if\s*\(\s*!\s*requireSameOrigin\s*\(\s*\)", body)
                  or "authorize(" not in body
                  or body.find("authorize(") < body.find("requireSameOrigin(")):
                problems.append(f"{url}: {m.group(1)}() doesn't start with requireSameOrigin() "
                                "before authorize()")
        if upload is not None:
            body = function_body(code, upload)
            if body is None:
                problems.append(f"{url}: no definition of {upload}()")
            elif not re.match(r"\s*if\s*\(\s*!\s*isMultipartRequest\s*\(\s*\)\s*\)\s*return\s*;",
                              body):
                problems.append(f"{url}: {upload}() doesn't start with the multipart check")
    if re.search(r"\bserver\.onNotFound\s*\((?!\s*secured<)", code):
        problems.append("onNotFound without secured<>")
    return problems


_STATIC_STATE_RE = re.compile(
    r"\bstatic\s+(?:(?:unsigned|signed|long|short)\s+)*"
    r"(?:bool|int|long|unsigned|short|u?int\d+_t|size_t|std::string)\s+(\w+)\s*(?:=|;|\{|\[)")


def static_state_names(text):
    """Names of the static variables of plain types (bool, int, long, unsigned, [u]intN_t,
    size_t, std::string) in the code of `text`, at file and function scope -- not static
    const / constexpr ones and not functions. Comments are ignored."""
    return [m.group(1) for m in _STATIC_STATE_RE.finditer(strip_comments(text))]


def function_body(text, name):
    """The body (between its braces, comments blanked) of the first definition `void
    <name>()` in `text`, or None."""
    code = strip_comments(text)
    m = re.search(r"(?m)^[ \t]*void\s+(?:\w+::)*" + re.escape(name) + r"\s*\(\s*\)\s*\{", code)
    if m is None:
        return None
    depth, i = 1, m.end()
    while i < len(code) and depth:
        depth += {"{": 1, "}": -1}.get(code[i], 0)
        i += 1
    return code[m.end():i - 1]



def definition_body(code, signature):
    """The body (between its braces) of the definition that starts with `signature` (exact
    text, e.g. "Config loadConfig()") at the start of a line of `code`; fails if there's none."""
    m = re.search(r"(?m)^" + re.escape(signature) + r"\s*\{", code)
    if m is None:
        raise AssertionError(f"no definition {signature} {{")
    depth, i = 1, m.end()
    while i < len(code) and depth:
        depth += {"{": 1, "}": -1}.get(code[i], 0)
        i += 1
    return code[m.end():i - 1]

# Words that can stand before `name(` without it declaring `name`: statements, and `explicit` (a
# constructor of the class, already declared by its `class name {` line).
_NOT_A_TYPE = {"return", "else", "case", "throw", "delete", "new", "goto", "sizeof", "co_return",
               "explicit"}


def declaration_lines(text, symbol, bodies_only=False):
    """Line numbers where `text` (a header) declares `symbol`: a type (`struct|class|enum
    [class] <symbol>` followed by `{` or `:` -- a forward declaration `struct X;` doesn't count)
    or a function (`<return type> <symbol>(...)` followed by `;` or a body; with bodies_only
    only a function with a body -- a definition -- counts). Comments are ignored; the name must
    match as a whole word (`Aircraft` isn't `AircraftRow`)."""
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
        end = "{" if bodies_only else "[;{]"
        if re.match(r"(?:\s|const\b|noexcept\b|override\b)*" + end, code[j:]):
            lines.add(code.count("\n", 0, m.start(1)) + 1)
    return sorted(lines)


_CONDITIONAL_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$")


def arduino_condition_lines(text):
    """Line numbers of the #if / #ifdef / #ifndef / #elif lines in `text` that test ARDUINO
    (`#ifdef ARDUINO`, `#ifndef ARDUINO`, `#if [!]defined(ARDUINO)`), comments ignored."""
    out = []
    for n, line in enumerate(strip_comments(text).splitlines(), 1):
        m = _CONDITIONAL_RE.match(line)
        if m and check_layers._condition_kind(m.group(1), m.group(2)) in ("ard", "not"):
            out.append(n)
    return out


_LOVYANGFX_RE = re.compile(r"\bLGFX\b|\bLGFX_Sprite\b|\blgfx::")

# A parameter of a LovyanGFX device / sprite type taken by reference.
LGFX_PARAM_RE = re.compile(r"\b(?:LGFX|LGFX_Sprite|lgfx::\w+)\s*&")


def lovyangfx_names(text):
    """[(line, match)] of LGFX, LGFX_Sprite and lgfx:: in the code of `text` (comments ignored;
    `LGFX_CYD.hpp` in an include line isn't `LGFX`)."""
    code = strip_comments(text)
    return [(code.count("\n", 0, m.start()) + 1, m.group(0))
            for m in _LOVYANGFX_RE.finditer(code)]


def defined_test_lines(text, name):
    """Line numbers of `static void <name>(` in `text` (comments ignored)."""
    code = strip_comments(text)
    return [code.count("\n", 0, m.start()) + 1 for m in
            re.finditer(r"(?m)^[ \t]*static\s+void\s+" + re.escape(name) + r"\s*\(", code)]


def run_test_calls(text, name):
    """How many times `text` calls RUN_TEST(<name>) (comments ignored)."""
    return len(re.findall(r"\bRUN_TEST\(\s*" + re.escape(name) + r"\s*\)",
                          strip_comments(text)))


def struct_members(text, name):
    """[(type, member)] of the data members of the first `struct|class <name> {...}` in `text`
    (comments ignored; member functions, access labels and initializers skipped; spaces in the
    type collapsed), or None when `text` has no such definition."""
    code = strip_comments(text)
    m = re.search(r"\b(?:struct|class)\s+" + re.escape(name)
                  + r"\b\s*(?:final\s*)?(?::[^{;]*)?\{", code)
    if m is None:
        return None
    depth, i, start = 1, m.end(), m.end()
    while i < len(code) and depth:
        depth += {"{": 1, "}": -1}.get(code[i], 0)
        i += 1
    body = code[start:i - 1]
    statements, current, depth = [], [], 0
    for ch in body:
        if ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        if depth == 0 and ch == ";":
            statements.append("".join(current))
            current = []
        elif depth == 0 and ch == "}" and "(" in "".join(current).split("{")[0]:
            current = []  # the end of a member function's body
        else:
            current.append(ch)
    members = []
    for s in statements:
        s = re.sub(r"^\s*(?:(?:public|private|protected)\s*:\s*)+", "", " ".join(s.split()))
        if not s or "(" in re.split(r"[={]", s)[0] or re.match(r"(?:static|using|friend|"
                                                               r"typedef)\b", s):
            continue
        d = re.match(r"^(.*?[\w>*&])\s*\b([A-Za-z_]\w*)\s*(?:=.*|\{.*\})?$", s)
        if d:
            members.append((d.group(1).strip(), d.group(2)))
    return members


def parameter_types(text, symbol):
    """For each declaration of the function `symbol` in `text` (declaration_lines()): its
    parameter types, names and default values dropped, written as `const Home &`."""
    code = strip_comments(text)
    starts = [0] + [k + 1 for k, ch in enumerate(code) if ch == "\n"]
    call = re.compile(r"\b" + re.escape(symbol) + r"\s*\(")
    out = []
    for line in declaration_lines(text, symbol):
        m = call.search(code, starts[line - 1])
        depth, j = 1, m.end()
        while j < len(code) and depth:
            depth += {"(": 1, ")": -1}.get(code[j], 0)
            j += 1
        params, current, depth = [], [], 0
        for ch in code[m.end():j - 1]:
            depth += {"(": 1, "<": 1, ")": -1, ">": -1}.get(ch, 0)
            if ch == "," and depth == 0:
                params.append("".join(current))
                current = []
            else:
                current.append(ch)
        params.append("".join(current))
        types = []
        for p in params:
            p = " ".join(p.split("=")[0].split())
            if not p or p == "void":
                continue
            k = max(p.rfind("&"), p.rfind("*"))
            t = p[:k + 1] if k >= 0 else (p.rsplit(" ", 1)[0] if " " in p else p)
            types.append(re.sub(r"\s*([&*])", r" \1", t).strip())
        out.append(types)
    return out


def is_settings_type(type_text):
    """True when a parameter / member type names Settings or one of its stage-6 objects."""
    return bool(set(re.findall(r"[A-Za-z_]\w*", type_text)) & SETTINGS_TYPES)


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

    def test_bodies_only_counts_definitions_not_prototypes(self):
        text = ("void loadConfig(Config &out);\n"
                "void loadConfig(Config &out) {\n  out = {};\n}\n"
                "static void helper() { loadConfig(c); }\n")
        self.assertEqual([1, 2], declaration_lines(text, "loadConfig"))
        self.assertEqual([2], declaration_lines(text, "loadConfig", bodies_only=True))

    def test_arduino_conditions_of_every_spelling_but_not_others(self):
        text = ("#ifdef ARDUINO\n#endif\n"
                "#ifndef ARDUINO\n#endif\n"
                "#if defined(ARDUINO)\n#elif !defined(ARDUINO)\n#endif\n"
                "#ifdef UNIT_TEST\n#endif\n"
                "// #ifdef ARDUINO\n"
                "#ifndef ARDUINO_GUARD_H\n#endif\n")
        self.assertEqual([1, 3, 5, 6], arduino_condition_lines(text))

    def test_struct_members_and_parameter_types(self):
        text = ("struct Home {\n  float lat = 0.0f;  // degrees\n  float radius_deg{2.5f};\n"
                "  bool configured = false;\n  bool valid() const { return lat < 90; }\n"
                " public:\n  std::string name;\n  static constexpr int kMax = 3;\n};\n"
                "struct HomeX { int a; };\n")
        self.assertEqual([("float", "lat"), ("float", "radius_deg"), ("bool", "configured"),
                          ("std::string", "name")], struct_members(text, "Home"))
        self.assertIsNone(struct_members("struct Home;\n", "Home"))
        decl = ("OpenSkyPollResult openSkyClientPoll(uint32_t now_ms, const OpenSkyCredentials &c,\n"
                "                                    const Home& home, const PollSettings &p = {});\n"
                "void backlightLoop(LGFX &tft, const BacklightSettings &b, bool serviceScreen);\n"
                "  x = openSkyClientPoll(now, c, h, p);\n")
        self.assertEqual([["uint32_t", "const OpenSkyCredentials &", "const Home &",
                           "const PollSettings &"]], parameter_types(decl, "openSkyClientPoll"))
        self.assertEqual([["LGFX &", "const BacklightSettings &", "bool"]],
                         parameter_types(decl, "backlightLoop"))
        self.assertTrue(is_settings_type("const Settings &"))
        self.assertFalse(is_settings_type("const ConfigPageModel &"))

    def test_constant_lines_finds_definitions_only(self):
        text = ("constexpr uint32_t kX = 30000;\n"
                "// constexpr uint32_t kX = 1;\n"
                "static const int kXY = 2;\n"
                "const int b = kX;\n"
                "  static constexpr int kX{4};\n")
        self.assertEqual([1, 5], constant_lines(text, "kX"))
        self.assertEqual([], constant_lines(text, "b2"))

    def test_markdown_section_stops_at_the_next_heading_of_its_level(self):
        text = "# t\n## A b\none\n### sub\ntwo\n## C\nthree\n"
        self.assertEqual("one\n### sub\ntwo", markdown_section(text, "## A"))
        self.assertIsNone(markdown_section(text, "## Z"))

    def test_tab_function_names_needs_the_tab_and_its_name(self):
        text = ("void locationTab(Writer &w,\n                 int a) {\n}\n"
                "static void renderOpenSkyTab(Writer &w) {}\n"
                "void locationCard(Writer &w) {}\n"
                "void settingsTabLabel(int t);\n"
                "// void generalTab(Writer &w) {}\n")
        self.assertEqual(["locationTab"], tab_function_names(text, "Location"))
        self.assertEqual(["renderOpenSkyTab"], tab_function_names(text, "OpenSky"))
        self.assertEqual([], tab_function_names(text, "General"))

    GOOD_ROUTES = ("void h() {\n  if (!requireSameOrigin() || !authorize()) return;\n}\n"
                   "void up() {\n  if (!isMultipartRequest()) return;\n}\n"
                   'void b() {\n  server.on("/x", HTTP_POST, secured<h>, up);\n'
                   '  server.on("/g", HTTP_GET, secured<g>);\n  server.onNotFound(secured<n>);\n}\n')

    def test_route_guards_accept_a_route_with_every_guard(self):
        self.assertEqual([("/x", "HTTP_POST", "secured<h>", "up"),
                          ("/g", "HTTP_GET", "secured<g>", None)],
                         route_registrations(self.GOOD_ROUTES))
        self.assertEqual([], route_guard_problems(self.GOOD_ROUTES))

    def test_route_guards_report_each_lost_guard(self):
        cases = {
            "secured<>": ("secured<h>, up", "h, up"),
            "requireSameOrigin": ("!requireSameOrigin() || !authorize()",
                                  "!authorize() || !requireSameOrigin()"),
            "multipart": ("if (!isMultipartRequest()) return;", "int a = 1;"),
            "onNotFound": ("onNotFound(secured<n>)", "onNotFound(n)"),
            "readable": ('server.on("/g", HTTP_GET, secured<g>);', "server.on(url, secured<g>);"),
        }
        for word, (old, new) in cases.items():
            with self.subTest(word=word):
                self.assertIn(old, self.GOOD_ROUTES)
                problems = route_guard_problems(self.GOOD_ROUTES.replace(old, new))
                self.assertEqual(1, len(problems), problems)
                self.assertIn(word, problems[0])

    def test_lovyangfx_names_in_code_but_not_comments_or_include_names(self):
        text = ('#include "display/adapters/lgfx/LGFX_CYD.hpp"\n'
                "// draws on an LGFX & with lgfx::fonts\n"
                "class LGFX;\n"
                "void f(lgfx::LovyanGFX &gfx);\n"
                "LGFX_Sprite s;\n"
                "int LGFXish = 0;\n")
        self.assertEqual([(3, "LGFX"), (4, "lgfx::"), (5, "LGFX_Sprite")], lovyangfx_names(text))
        self.assertIsNotNone(LGFX_PARAM_RE.search("void drawX(LGFX &gfx);"))
        self.assertIsNotNone(LGFX_PARAM_RE.search("TextMeasure m(lgfx::LovyanGFX& gfx);"))
        self.assertIsNone(LGFX_PARAM_RE.search("void hitTest(int x, int y);"))


def platformio_ini():
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(ROOT / "platformio.ini", encoding="utf-8")
    return cp


class BuildConfigTest(unittest.TestCase):
    # Scenario: Środowisko native nie kompiluje korzeni kompozycji, adapterów ani adapterowej
    # części platform/
    def test_native_filter_excludes_roots_and_adapters(self):
        native = platformio_ini()["env:native"]
        tokens = native.get("build_src_filter", "").split()
        self.assertEqual(["+<*>", "-<main/>", "-<*/adapters/>", "-<platform/*_esp32.cpp>"],
                         tokens)
        readded = [t for t in tokens[1:] if t.startswith("+<")]
        self.assertEqual([], readded, "the native filter adds files back")

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
    # Scenario: CI uruchamia sprawdzenie warstw przed testami natywnymi i blokuje build, a testy
    # narzędzi idą razem
    def test_firmware_workflow_blocks_on_layers_before_native_tests(self):
        text = (ROOT / ".github" / "workflows" / "firmware.yml").read_text(encoding="utf-8")
        steps = workflow_steps(text)
        self.assertGreater(len(steps), 5)
        layers = [i for i, (_, b) in enumerate(steps)
                  if re.search(r"python tools/check_layers\.py --strict(?!\S)", b)]
        native = [i for i, (_, b) in enumerate(steps)
                  if re.search(r"pio test -e native(\s|$)", b)]
        self.assertTrue(layers, "no step runs python tools/check_layers.py")
        self.assertTrue(native, "no step runs pio test -e native")
        self.assertLess(layers[0], native[0])
        self.assertNotIn("continue-on-error", steps[layers[0]][1])
        tool_tests = [b for _, b in steps if "python tools/test_check_bdd.py" in b]
        self.assertEqual(1, len(tool_tests))
        for t in ("test_check_layers.py", "test_subset_efont.py"):
            self.assertIn(f"python tools/{t}", tool_tests[0])


if __name__ == "__main__":
    unittest.main()
