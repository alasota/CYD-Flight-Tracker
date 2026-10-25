"""Tests for tools/release_plan.py (the pure part):  python tools/test_release_plan.py"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_plan as rp  # noqa: E402


class VersionTest(unittest.TestCase):
    def test_parse_only_stable_tags(self):
        self.assertEqual((1, 6, 0), rp.parse_version("v1.6.0"))
        for t in ("v1.5.2-dev.3", "v1.3.0-rc.1", "1.6.0", "v1.6", "v1.4.2-3-gabc", ""):
            self.assertIsNone(rp.parse_version(t), t)

    def test_latest_is_the_highest_version_not_the_newest_or_alphabetical(self):
        tags = ["v1.4.2", "v1.10.0", "v1.9.9", "v1.11.0-dev.4", "v1.5.1"]
        self.assertEqual("v1.10.0", rp.latest_release(tags))
        self.assertIsNone(rp.latest_release(["v1.0.0-rc.1"]))

    def test_next_version(self):
        self.assertEqual("v1.6.1", rp.next_version("v1.6.0", "patch"))
        self.assertEqual("v1.7.0", rp.next_version("v1.6.3", "minor"))
        self.assertEqual("v2.0.0", rp.next_version("v1.6.3", "major"))
        self.assertEqual("v0.1.0", rp.next_version(None, "minor"))
        with self.assertRaises(ValueError):
            rp.next_version("v1.0.0", "huge")

    def test_utc_iso_is_utc_whatever_the_local_zone(self):
        self.assertEqual("2026-10-25T17:30:12Z", rp.utc_iso(1792949412))
        self.assertEqual("1970-01-01T00:00:00Z", rp.utc_iso("0"))


class ClassifyTest(unittest.TestCase):
    def test_kinds(self):
        self.assertEqual("feature", rp.classify_pr("spec/06-opensky-login-fallback", []))
        self.assertEqual("feature", rp.classify_pr("issue/12-dark-mode", []))
        self.assertEqual("fix", rp.classify_pr("issue/14-crash", ["bug"]))
        self.assertEqual("fix", rp.classify_pr("fix/15-table-overflow", []))
        self.assertEqual("fix", rp.classify_pr("anything", ["Bug"]))
        self.assertEqual("release", rp.classify_pr("release/v1.7.0", []))
        self.assertEqual("other", rp.classify_pr("harness/spec-to-pr", []))
        self.assertEqual("feature", rp.classify_pr("harness/x", ["enhancement"]))

    def test_bump(self):
        self.assertEqual("minor", rp.choose_bump(["fix", "feature", "other"]))
        self.assertEqual("patch", rp.choose_bump(["fix", "other"]))
        self.assertEqual("patch", rp.choose_bump(["other"]))
        self.assertIsNone(rp.choose_bump([]))
        self.assertIsNone(rp.choose_bump(["release"]))

    def test_spec_number(self):
        self.assertEqual("06", rp.spec_number("spec/06-opensky-login-fallback"))
        self.assertIsNone(rp.spec_number("issue/12-x"))
        self.assertIsNone(rp.spec_number("spec/6-x"))


class SpecsToMoveTest(unittest.TestCase):
    def test_merged_or_marked_done(self):
        files = {
            "spec/planned/06-login.md": "# Spec 06\nStatus: **w trakcie**",
            "spec/planned/06-login.feature": "Feature: x",
            "spec/planned/09-robust.md": "# Spec 09\nStatus: **do zrobienia**",
            "spec/planned/10-web.md": "# Spec 10\n\nZ przeglądu. Status: **zrobione** 2026-10-26",
            "spec/planned/11-clean.md": "# Spec 11\nStatus: **zaimplementowane 2026-10-25**",
        }
        moved = rp.specs_to_move(list(files), {"06"}, files.__getitem__)
        self.assertEqual(["spec/planned/06-login.md", "spec/planned/10-web.md",
                          "spec/planned/11-clean.md"], moved)


class UnreleasedTest(unittest.TestCase):
    def test_pr_whose_merge_commit_is_in_the_latest_tag_is_not_listed_again(self):
        # Regression: v1.6.2's plan listed PR #4, already in v1.6.1 — a rebase merge's commit
        # time (18:37:46) is a second before the PR's merged_at (18:37:47), so a date window
        # can't tell them apart; ancestry of the merge commit can.
        prs = [{"number": 5, "merge_commit": "a0bd0df"},
               {"number": 4, "merge_commit": "7e87f8c"},
               {"number": 3, "merge_commit": None}]  # not known (API gave none) -> keep it
        released = {"7e87f8c"}
        kept = rp.unreleased(prs, lambda sha: sha in released)
        self.assertEqual([5, 3], [p["number"] for p in kept])

    def test_search_window_starts_a_day_before_the_tag(self):
        self.assertEqual("2026-10-24T18:37:46Z", rp.search_since("2026-10-25T18:37:46Z"))


class PlaceholderTest(unittest.TestCase):
    def test_only_bare_vnext_is_a_placeholder(self):
        self.assertTrue(rp.has_placeholder("| 5 | x | ⭐ | Done in vNEXT: y |"))
        self.assertTrue(rp.has_placeholder("Status: **zrobione** 2026-10-26, wydane w vNEXT."))
        self.assertFalse(rp.has_placeholder("docs write `vNEXT`, /release fills it in"))
        self.assertFalse(rp.has_placeholder("vNEXTGEN and myvNEXT are other words"))

    def test_fill_keeps_the_text_about_it(self):
        text = "Done in vNEXT: x <!-- pl: Zrobione w vNEXT: y --> (write `vNEXT` here)"
        self.assertEqual("Done in v1.7.0: x <!-- pl: Zrobione w v1.7.0: y --> (write `vNEXT` here)",
                         rp.fill_placeholder(text, "v1.7.0"))


if __name__ == "__main__":
    unittest.main()
