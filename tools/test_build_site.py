"""Tests for tools/build_site.py (the pure part + a static check of site/index.html):
    python tools/test_build_site.py

No git, no network: releases() (the git part) is not called; the grouping, the roadmap
assignment and the HTML are pure functions on lists. The HTML is checked through a small
tree built with the standard library's html.parser (CI has no extra Python packages).
Scenarios: spec/planned/13-project-page-layout.feature.
"""

import re
import sys
import unittest
from html.parser import HTMLParser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_site as bs  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
INDEX = REPO / "site" / "index.html"
GITHUB = bs.GITHUB

# ---------------------------------------------------------------- HTML tree helpers

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
        "source", "track", "wbr"}


class Node:
    def __init__(self, tag, attrs, parent):
        self.tag = tag
        self.attrs = dict(attrs)
        self.children = []
        self.parent = parent

    @property
    def classes(self):
        return (self.attrs.get("class") or "").split()

    def iter(self):
        yield self
        for c in self.children:
            if isinstance(c, Node):
                yield from c.iter()

    def find_all(self, tag=None, cls=None, **attrs):
        """Descendants (not self) matching tag / class / attributes (role, id, ...)."""
        out = []
        for n in self.iter():
            if n is self:
                continue
            if tag and n.tag != tag:
                continue
            if cls and cls not in n.classes:
                continue
            if any(n.attrs.get(k) != v for k, v in attrs.items()):
                continue
            out.append(n)
        return out

    def find(self, tag=None, cls=None, **attrs):
        found = self.find_all(tag, cls, **attrs)
        return found[0] if found else None

    def element_children(self):
        return [c for c in self.children if isinstance(c, Node)]

    def text(self, lang=None):
        """Visible text, whitespace collapsed; lang="en" skips .pl elements and vice versa."""
        other = {"en": "pl", "pl": "en"}.get(lang)
        parts = []

        def walk(n):
            if other and other in n.classes:
                return
            for c in n.children:
                if isinstance(c, Node):
                    walk(c)
                else:
                    parts.append(c)
        walk(self)
        return re.sub(r"\s+", " ", "".join(parts)).strip()

    def ancestors(self):
        n = self.parent
        while n is not None:
            yield n
            n = n.parent


class TreeBuilder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("#root", {}, None)
        self.stack = [self.root]

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs, self.stack[-1])
        self.stack[-1].children.append(node)
        if tag not in VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.stack[-1].children.append(Node(tag, attrs, self.stack[-1]))

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return

    def handle_data(self, data):
        self.stack[-1].children.append(data)


def parse(text):
    b = TreeBuilder()
    b.feed(text)
    b.close()
    return b.root


def changelog_groups(page):
    """The top-level <details class="card"> entries of the changelog, in order."""
    return [d for d in parse(page).find_all("details") if "card" in d.classes]


def summary_of(details):
    return next(c for c in details.element_children() if c.tag == "summary")


def group_named(groups, minor):
    for g in groups:
        b = summary_of(g).find("b")
        if b is not None and b.text() == minor:
            return g
    raise AssertionError("no changelog group %s in %s" % (
        minor, [summary_of(g).text() for g in groups]))


def commit_items(ul):
    """<li> of a commit list without the "…and N more" one."""
    return [li for li in ul.find_all("li") if "more" not in li.classes]


# ---------------------------------------------------------------- test data

def rel(tag, date, n, prefix=None):
    """(tag, date, subjects) as releases() gives it, with n distinct commit subjects."""
    prefix = prefix or tag
    return (tag, date, ["%s change %d" % (prefix, i) for i in range(n, 0, -1)])


def roadmap_md(planned=(), done=()):
    """A small ROADMAP.md; rows are (item_en, item_pl, verdict_en, verdict_pl)."""
    head = ("| # | Item | Value | Effort | Risk | Rating | Verdict |\n"
            "|---|---|---|---|---|---|---|\n")

    def rows(items):
        return "".join(
            "| %d | %s <!-- pl: %s --> | high | small | low | ⭐⭐⭐⭐ | %s <!-- pl: %s --> |\n"
            % ((i + 1,) + tuple(r)) for i, r in enumerate(items))
    text = "# Roadmap\n\nIntro.\n\n"
    if planned:
        text += "## Planned\n\n" + head + rows(planned) + "\n"
    if done:
        text += "## Done\n\n" + head + rows(done) + "\n"
    return text


def done_sections(*done, planned=()):
    return bs.parse_roadmap(roadmap_md(planned=planned, done=done))


V160 = "v1.6.0"
WRONG_CREDS = ("Wrong OpenSky credentials: keep showing planes anonymously",
               "Złe dane logowania OpenSky: dalej pokazywać samoloty anonimowo",
               "Done in v1.6.0: with a wrong secret the planes keep coming anonymously.",
               "Zrobione w v1.6.0: przy złym sekrecie samoloty dalej przychodzą anonimowo.")


def group(minor, tags, subjects, prev_tag, date="2026-10-25"):
    return bs.Group(minor=minor, tags=list(tags), date=date, subjects=list(subjects),
                    prev_tag=prev_tag)


# ---------------------------------------------------------------- §1.1 versions, ranges, groups

class VersionTest(unittest.TestCase):
    def test_parse_version_only_stable_tags(self):
        self.assertEqual((1, 6, 0), bs.parse_version("v1.6.0"))
        self.assertEqual((1, 10, 2), bs.parse_version("v1.10.2"))
        for t in ("v1.6.5-dev.13", "v1.3.0-rc.1", "v1.6", "1.6.0", "v1.4.2-3-gabc", ""):
            self.assertIsNone(bs.parse_version(t), t)

    def test_format_date(self):
        self.assertEqual("25.10.2026", bs.format_date("2026-10-25"))
        self.assertEqual("05.10.2026", bs.format_date("2026-10-05"))
        self.assertEqual("", bs.format_date(""))  # git missing -> no date, no crash


class ReleaseRangesTest(unittest.TestCase):
    # Scenario: Wersja deweloperska nie ma własnego wpisu, jej commity trafiają do następnego wydania
    def test_dev_tags_get_no_entry_of_their_own(self):
        ranges = bs.release_ranges(["v1.6.4", "v1.6.4-dev.11", "v1.6.3"])
        self.assertEqual([("v1.6.4", "v1.6.3..v1.6.4"), ("v1.6.3", "v1.6.3")], ranges)
        self.assertNotIn("v1.6.4-dev.11", [t for t, _ in ranges])

    def test_empty_and_only_dev_tags(self):
        self.assertEqual([], bs.release_ranges([]))
        self.assertEqual([], bs.release_ranges(["v1.3.0-rc.1", "v1.4.3-dev.57"]))

    def test_single_tag_covers_all_history(self):
        self.assertEqual([("v1.0.0", "v1.0.0")], bs.release_ranges(["v1.0.0"]))

    def test_dev_tag_older_than_every_release_is_ignored(self):
        self.assertEqual([("v1.0.1", "v1.0.0..v1.0.1"), ("v1.0.0", "v1.0.0")],
                         bs.release_ranges(["v0.9.0-rc.1", "v1.0.0", "v1.0.1"]))

    def test_numeric_order_whatever_the_input_order(self):
        self.assertEqual([("v1.10.0", "v1.9.0..v1.10.0"), ("v1.9.0", "v1.9.0")],
                         bs.release_ranges(["v1.9.0", "v1.10.0"]))


class GroupByMinorTest(unittest.TestCase):
    def rels_16_15(self):
        return [rel("v1.6.4", "2026-10-25", 2), rel("v1.6.3", "2026-10-25", 2),
                rel("v1.6.2", "2026-10-24", 2), rel("v1.6.1", "2026-10-24", 2),
                rel("v1.6.0", "2026-10-24", 1),
                rel("v1.5.1", "2026-10-23", 1), rel("v1.5.0", "2026-10-22", 1)]

    # Scenario: Wydania patch tej samej wersji minor tworzą jedną grupę
    def test_patches_and_dev_versions_in_one_group(self):
        rels = self.rels_16_15()
        groups = bs.group_by_minor(rels)
        self.assertEqual(["v1.6", "v1.5"], [g.minor for g in groups])
        self.assertEqual(["v1.6.4", "v1.6.3", "v1.6.2", "v1.6.1", "v1.6.0"], groups[0].tags)
        self.assertEqual(["v1.5.1", "v1.5.0"], groups[1].tags)
        expected = [s for tag, _, subjects in rels[:5] for s in subjects]
        self.assertEqual(expected, groups[0].subjects)  # newest release's commits first
        self.assertEqual("2026-10-25", groups[0].date)
        self.assertEqual("2026-10-23", groups[1].date)

    def test_no_releases(self):
        self.assertEqual([], bs.group_by_minor([]))

    # Scenario: Nagłówek grupy pokazuje zakres wydań, datę najnowszego i liczbę zmian
    def test_group_header_range_date_and_count(self):
        groups = bs.group_by_minor(self.rels_16_15()[:5])  # v1.6.0 … v1.6.4, 9 commits
        self.assertEqual(1, len(groups))
        self.assertEqual(9, len(groups[0].subjects))
        head = summary_of(group_named(changelog_groups(bs.changelog_html(groups, {})), "v1.6"))
        text = head.text()
        for part in ("v1.6", "v1.6.0 – v1.6.4", "25.10.2026"):
            self.assertIn(part, text)
        self.assertIn("9 change(s)", head.text("en"))
        self.assertIn("zmian: 9", head.text("pl"))

    # Scenario: Grupa z jednym wydaniem pokazuje tylko jego numer zamiast zakresu
    def test_single_release_group_shows_only_its_number(self):
        groups = bs.group_by_minor([rel("v1.3.0", "2026-10-21", 4)])
        self.assertEqual(["v1.3"], [g.minor for g in groups])
        self.assertEqual(["v1.3.0"], groups[0].tags)
        head = summary_of(group_named(changelog_groups(bs.changelog_html(groups, {})), "v1.3"))
        self.assertIn("v1.3.0", head.text())
        self.assertNotIn("–", head.text())

    # Scenario: Grupy idą od najnowszej wersji, sortowane numerycznie (v1.10 przed v1.9)
    def test_groups_newest_first_by_version_number(self):
        # Input in string order, as a plain sort would leave it: v1.9.x "after" v1.10.0.
        rels = [rel("v1.9.1", "2026-10-20", 1), rel("v1.9.0", "2026-10-19", 1),
                rel("v1.10.0", "2026-10-21", 1)]
        groups = bs.group_by_minor(rels)
        self.assertEqual(["v1.10", "v1.9"], [g.minor for g in groups])
        self.assertEqual(["v1.9.1", "v1.9.0"], groups[1].tags)
        self.assertEqual("v1.9.1", groups[0].prev_tag)

    # Scenario: Linki grupy prowadzą do najnowszego wydania i do różnic od poprzedniej grupy
    def test_release_and_diff_links(self):
        groups = bs.group_by_minor(self.rels_16_15())
        self.assertEqual(["v1.6", "v1.5"], [g.minor for g in groups])
        self.assertEqual("v1.5.1", groups[0].prev_tag)
        self.assertIsNone(groups[1].prev_tag)
        page = changelog_groups(bs.changelog_html(groups, {}))
        hrefs16 = [a.attrs.get("href") for a in summary_of(group_named(page, "v1.6")).find_all("a")]
        self.assertIn(GITHUB + "/releases/tag/v1.6.4", hrefs16)
        self.assertIn(GITHUB + "/compare/v1.5.1...v1.6.4", hrefs16)
        hrefs15 = [a.attrs.get("href") for a in summary_of(group_named(page, "v1.5")).find_all("a")]
        self.assertIn(GITHUB + "/releases/tag/v1.5.1", hrefs15)
        self.assertFalse([h for h in hrefs15 if "/compare/" in h], hrefs15)


# ---------------------------------------------------------------- §1.2 roadmap items

class RoadmapDoneTest(unittest.TestCase):
    # Scenario: Pozycje „Done in vX.Y.Z” z ROADMAP.md trafiają do grupy vX.Y w kolejności wersji
    def test_done_rows_grouped_by_minor_in_version_order(self):
        sections = done_sections(
            ("Memory that stays put", "Pamięć, która nie ucieka",
             "Done in v1.4.3: caches.", "Zrobione w v1.4.3: cache."),
            ("Two CPU cores", "Dwa rdzenie",
             "Done in v1.4.0: no freezes.", "Zrobione w v1.4.0: bez zamrożeń."),
            ("Tests reorganized", "Reorganizacja testów",
             "Done in v1.4.2: 30 s.", "Zrobione w v1.4.2: 30 s."))
        done = bs.roadmap_done_by_version(sections)
        self.assertEqual({"v1.4": [
            ("v1.4.0", ("Two CPU cores", "Dwa rdzenie")),
            ("v1.4.2", ("Tests reorganized", "Reorganizacja testów")),
            ("v1.4.3", ("Memory that stays put", "Pamięć, która nie ucieka")),
        ]}, done)

    def test_versions_sorted_numerically(self):
        sections = done_sections(
            ("Ten", "Dziesięć", "Done in v1.4.10: x.", "Zrobione w v1.4.10: x."),
            ("Nine", "Dziewięć", "Done in v1.4.9: y.", "Zrobione w v1.4.9: y."))
        self.assertEqual(["v1.4.9", "v1.4.10"],
                         [v for v, _ in bs.roadmap_done_by_version(sections).get("v1.4", [])])

    # Scenario: Pozycja jeszcze niewydana („Done in vNEXT”) nie trafia do żadnej grupy
    def test_unreleased_vnext_row_in_no_group(self):
        sections = done_sections(
            ("Project page layout", "Układ strony projektu",
             "Done in vNEXT: shorter page.", "Zrobione w vNEXT: krótsza strona."),
            WRONG_CREDS)
        done = bs.roadmap_done_by_version(sections)
        self.assertEqual({"v1.6": [(V160, WRONG_CREDS[:2])]}, done)
        items = [item for rows in done.values() for _, item in rows]
        self.assertNotIn(("Project page layout", "Układ strony projektu"), items)

    # Scenario: Wiersze spoza sekcji „Done” nie są brane pod uwagę
    def test_only_done_section_counts(self):
        sections = done_sections(
            WRONG_CREDS,
            planned=[("Feeder", "Feeder PL", "Done in v1.7.0 once the receiver arrives.",
                      "Zrobione w v1.7.0, gdy przyjdzie odbiornik.")])
        done = bs.roadmap_done_by_version(sections)
        self.assertIn("v1.6", done)
        self.assertNotIn("v1.7", done)

    def test_other_verdicts_ignored_trailing_punctuation_parsed(self):
        sections = done_sections(
            ("Dropped idea", "Porzucony pomysł", "Dropped: not worth it.", "Porzucone."),
            ("Dotted", "Z kropką", "Done in v1.4.0.", "Zrobione w v1.4.0."))
        self.assertEqual({"v1.4": [("v1.4.0", ("Dotted", "Z kropką"))]},
                         bs.roadmap_done_by_version(sections))

    def test_no_done_section(self):
        sections = bs.parse_roadmap(roadmap_md(planned=[WRONG_CREDS]))
        self.assertEqual({}, bs.roadmap_done_by_version(sections))


# ---------------------------------------------------------------- §1.3 chores

CHORE_EXAMPLES = [
    ("Tests: readable assertions in test_admin (spec 07 step 3)", True),
    ("Test reproducing a released spec left 'in progress' in the roadmap", True),
    ("Docs: night mode in README", True),
    ("Plan and BDD for spec 06", True),
    ("Spec 13: project page — changelog grouped by minor version", True),
    ("Spec/plan: OpenSky login fallback", True),
    ("/release: stale docs trigger the release PR", True),
    ("/fix: regression test first", True),
    ("Release plan: finds a released spec the docs still call unfinished", True),
    ("Harness: spec → PR agents", True),
    ("Python bytecode out of the repo", True),
    ("fix", True),
    ("Testing the touch calibration on the board", False),
    ("Fix: table column overflow", False),
    ("OpenSky login failure: keep polling anonymously", False),
]


class ChoreTest(unittest.TestCase):
    # Scenario Outline: Commit „zza kulis” jest rozpoznawany po temacie
    def test_is_chore_examples(self):
        for subject, chore in CHORE_EXAMPLES:
            with self.subTest(subject=subject):
                self.assertIs(chore, bs.is_chore(subject))


# ---------------------------------------------------------------- changelog HTML

class ChangelogHtmlTest(unittest.TestCase):
    def test_no_releases_text_kept(self):
        page = bs.changelog_html([], {})
        self.assertIn("No releases yet.", page)
        self.assertIn("Jeszcze nic nie wydano.", page)

    # Scenario: Grupa z pozycjami z roadmapy pokazuje najpierw ich listę, bez werdyktu
    def test_group_lists_roadmap_items_first(self):
        done = bs.roadmap_done_by_version(done_sections(WRONG_CREDS))
        groups = [group("v1.6", ["v1.6.0"], ["OpenSky login failure: keep polling"], "v1.5.1")]
        page = bs.changelog_html(groups, done)
        g = group_named(changelog_groups(page), "v1.6")
        lists = [ul for ul in g.find_all("ul")
                 if not any(a.tag == "summary" for a in ul.ancestors())]
        self.assertTrue(lists, "no list in the group")
        first = lists[0]
        self.assertIn("items", first.classes)  # plain body font, not the monospace commit list
        self.assertFalse(any(a.tag == "details" and "commits" in a.classes
                             for a in first.ancestors()), "roadmap items inside Commits")
        [li] = first.find_all("li")
        self.assertEqual(WRONG_CREDS[0], li.text("en"))
        self.assertEqual(WRONG_CREDS[1], li.text("pl"))
        self.assertIsNotNone(li.find(cls="en"))
        self.assertIsNotNone(li.find(cls="pl"))
        self.assertEqual(["#roadmap"], [a.attrs.get("href") for a in li.find_all("a")])
        self.assertNotIn("with a wrong secret", page)
        self.assertNotIn("przy złym sekrecie", page)

    # Scenario: Grupa bez pozycji w roadmapie pokazuje tylko commity
    def test_group_without_roadmap_shows_only_commits(self):
        subjects = ["First version of the table", "Radar screen"]
        page = bs.changelog_html([group("v1.0", ["v1.0.0"], subjects, None)], {})
        g = group_named(changelog_groups(page), "v1.0")
        self.assertEqual([], g.find_all("ul", cls="items"))
        # Owner decision 2: no nested "Commits (N)" — the commits sit directly in the group.
        self.assertEqual([], g.find_all("details", cls="commits"))
        direct = [ul for ul in g.element_children() if ul.tag == "ul"]
        self.assertEqual(1, len(direct), "expected the commit list directly in the group")
        self.assertEqual(subjects, [li.text() for li in commit_items(direct[0])])

    # Scenario: Commity grupy są w zwiniętym „Commity (N)” z limitem 15 i linkiem „…i jeszcze N”
    def test_commits_folded_with_limit(self):
        done = {"v1.6": [(V160, WRONG_CREDS[:2])]}
        subjects = ["Change %d" % i for i in range(20, 0, -1)]
        g = group_named(changelog_groups(bs.changelog_html(
            [group("v1.6", ["v1.6.4", "v1.6.0"], subjects, "v1.5.1")], done)), "v1.6")
        [nested] = g.find_all("details", cls="commits") or [None]
        self.assertIsNotNone(nested, "no nested <details class=commits>")
        self.assertNotIn("open", nested.attrs)
        head = summary_of(nested)
        self.assertIn("Commits (20)", head.text("en"))
        self.assertIn("Commity (20)", head.text("pl"))
        [ul] = nested.find_all("ul")
        self.assertEqual(subjects[:15], [li.text() for li in commit_items(ul)])
        [more] = ul.find_all("li", cls="more")
        self.assertEqual("…and 5 more", more.text("en"))
        self.assertEqual("…i jeszcze 5", more.text("pl"))
        self.assertEqual([GITHUB + "/compare/v1.5.1...v1.6.4"],
                         [a.attrs.get("href") for a in more.find_all("a")])

    def commits_list(self, n, prev_tag="v1.5.1"):
        done = {"v1.6": [(V160, WRONG_CREDS[:2])]}
        page = bs.changelog_html(
            [group("v1.6", ["v1.6.0"], ["Change %d" % i for i in range(n)], prev_tag)], done)
        g = group_named(changelog_groups(page), "v1.6")
        uls = [ul for ul in g.find_all("ul") if "items" not in ul.classes]
        self.assertEqual(1, len(uls), "expected one commit list")
        return uls[0]

    def test_exactly_15_commits_have_no_more_link(self):
        ul = self.commits_list(15)
        self.assertEqual(15, len(commit_items(ul)))
        self.assertEqual([], ul.find_all("li", cls="more"))

    def test_16_commits_end_with_and_1_more(self):
        ul = self.commits_list(16)
        self.assertEqual(15, len(commit_items(ul)))
        [more] = ul.find_all("li", cls="more")
        self.assertEqual("…and 1 more", more.text("en"))

    def test_more_link_of_the_oldest_group_goes_to_its_commits(self):
        ul = self.commits_list(20, prev_tag=None)
        [more] = ul.find_all("li", cls="more")
        self.assertEqual([GITHUB + "/commits/v1.6.0"],
                         [a.attrs.get("href") for a in more.find_all("a")])

    # Scenario: Commity zza kulis są wyszarzone, nie ukryte, i liczą się w nagłówku
    def test_chore_commits_greyed_and_counted(self):
        subjects = ["Tests: login fallback", "Docs: login fallback",
                    "OpenSky login failure: keep polling anonymously"]
        done = {"v1.6": [(V160, WRONG_CREDS[:2])]}
        g = group_named(changelog_groups(bs.changelog_html(
            [group("v1.6", ["v1.6.0"], subjects, "v1.5.1")], done)), "v1.6")
        [nested] = g.find_all("details", cls="commits") or [None]
        self.assertIsNotNone(nested, "no nested <details class=commits>")
        lis = commit_items(nested.find("ul"))
        self.assertEqual(subjects, [li.text() for li in lis])
        self.assertEqual([True, True, False], ["chore" in li.classes for li in lis])
        head = summary_of(g)
        self.assertIn("zmian: 3", head.text("pl"))
        self.assertIn("3 change(s)", head.text("en"))

    def test_chores_greyed_in_a_group_without_roadmap_items(self):
        page = bs.changelog_html(
            [group("v1.0", ["v1.0.0"], ["Docs: README", "Radar screen"], None)], {})
        lis = commit_items(group_named(changelog_groups(page), "v1.0").find("ul"))
        self.assertEqual([True, False], ["chore" in li.classes for li in lis])

    # Scenario: Otwarte są dwie najnowsze grupy, starsze zwinięte do jednej linii
    def test_two_newest_groups_open_rest_one_line(self):
        done = {
            "v1.6": [(V160, WRONG_CREDS[:2])],
            "v1.5": [("v1.5.1", ("Warnings on the display", "Ostrzeżenia na wyświetlaczu"))],
            "v1.2": [("v1.2.0", ("The device checks GitHub for new versions",
                                 "Urządzenie samo sprawdza nowe wersje"))],
        }
        groups = [group("v1.6", ["v1.6.0"], ["a"], "v1.5.1"),
                  group("v1.5", ["v1.5.1", "v1.5.0"], ["b", "c"], "v1.2.0"),
                  group("v1.2", ["v1.2.0"], ["d"], "v1.0.0"),
                  group("v1.0", ["v1.0.0"], ["e", "f", "g", "h"], None)]
        page = changelog_groups(bs.changelog_html(groups, done))
        self.assertEqual(4, len(page))
        self.assertEqual([True, True, False, False], ["open" in g.attrs for g in page])
        for minor in ("v1.6", "v1.5"):
            self.assertEqual([], summary_of(group_named(page, minor)).find_all(cls="first-item"),
                             minor + " is open: no one-line preview")
        first12 = summary_of(group_named(page, "v1.2")).find(cls="first-item")
        self.assertIsNotNone(first12, "collapsed v1.2 has no first-item preview")
        self.assertIn("The device checks GitHub for new versions", first12.text("en"))
        self.assertIn("Urządzenie samo sprawdza nowe wersje", first12.text("pl"))
        first10 = summary_of(group_named(page, "v1.0")).find(cls="first-item")
        self.assertIsNotNone(first10, "collapsed v1.0 has no first-item preview")
        self.assertIn("4 change(s)", first10.text("en"))
        self.assertIn("zmian: 4", first10.text("pl"))

    # Scenario: Znaki specjalne w tematach commitów i pozycjach roadmapy są escapowane
    def test_text_is_escaped(self):
        done = {"v1.6": [(V160, ("Fixes <script> & co", "Poprawki <b> & spółka"))]}
        page = bs.changelog_html(
            [group("v1.6", ["v1.6.0"], ["Table <TYP> & route"], "v1.5.1")], done)
        self.assertIn("&lt;TYP&gt;", page)
        self.assertIn("&amp; route", page)
        self.assertNotIn("<TYP>", page)
        self.assertIn("&lt;script&gt; &amp; co", page)
        self.assertNotIn("<script>", page)
        self.assertIn("Poprawki &lt;b&gt; &amp; spółka", page)


# ---------------------------------------------------------------- §4 roadmap Done table

def roadmap_card(page, section):
    for card in parse(page).find_all("div", cls="card"):
        h3 = card.find("h3")
        if h3 is not None and h3.text("en") == section:
            return card
    raise AssertionError("no roadmap section " + section)


class RoadmapHtmlTest(unittest.TestCase):
    # Scenario: Tabela „Done” pokazuje numer wersji z linkiem do historii zmian zamiast werdyktu
    def test_done_shows_version_not_verdict(self):
        long_en = ("Done in v1.6.0: with a wrong secret the planes keep coming anonymously "
                   "(green dot, red \"!\"), the login server is asked every 30 min.")
        long_pl = ("Zrobione w v1.6.0: przy złym sekrecie samoloty dalej przychodzą anonimowo "
                   "(zielona kropka, czerwone „!”), serwer logowania co 30 min.")
        page = bs.roadmap_html(done_sections(WRONG_CREDS[:2] + (long_en, long_pl)))
        card = roadmap_card(page, "Done")
        ths = card.find_all("th")
        self.assertEqual(["#", "Item", "Version"], [th.text("en") for th in ths])
        self.assertEqual(["#", "Pozycja", "Wersja"], [th.text("pl") for th in ths])
        [row] = card.find("tbody").find_all("tr")
        cell = row.find_all("td")[-1]
        self.assertEqual([("#changelog", "v1.6.0")],
                         [(a.attrs.get("href"), a.text()) for a in cell.find_all("a")])
        self.assertNotIn("with a wrong secret", page)
        self.assertNotIn("przy złym sekrecie", page)

    # Scenario: Sekcje „Planned” i „In progress” zachowują wszystkie kolumny i pełny werdykt
    def test_other_sections_keep_full_verdict(self):
        verdict = ("When the RTL-SDR arrives: no credits, ~1 s updates. Needs a data-source "
                   "layer first.")
        page = bs.roadmap_html(done_sections(
            WRONG_CREDS,
            planned=[("Local ADS-B feeder", "Własny feeder ADS-B", verdict,
                      "Gdy przyjdzie RTL-SDR: zero kredytów.")]))
        card = roadmap_card(page, "Planned")
        self.assertEqual(["#", "Item", "Value", "Effort", "Risk", "Rating", "Verdict"],
                         [th.text("en") for th in card.find_all("th")])
        self.assertIn(verdict, card.text("en"))
        self.assertIn("Gdy przyjdzie RTL-SDR: zero kredytów.", card.text("pl"))

    # Scenario: Pozycja „Done in vNEXT” w tabeli „Done” mówi „następna wersja”
    def test_done_vnext_reads_next_release(self):
        page = bs.roadmap_html(done_sections(
            ("Project page layout", "Układ strony projektu",
             "Done in vNEXT: a shorter page.", "Zrobione w vNEXT: krótsza strona.")))
        [row] = roadmap_card(page, "Done").find("tbody").find_all("tr")
        cell = row.find_all("td")[-1]
        self.assertEqual("the next release", cell.text("en"))
        self.assertEqual("następna wersja", cell.text("pl"))
        self.assertNotIn("a shorter page", page)

    def test_done_row_without_a_version_keeps_its_verdict(self):
        page = bs.roadmap_html(done_sections(
            ("Dropped idea", "Porzucony pomysł", "Dropped: not worth the flash.",
             "Porzucone: szkoda flasha.")))
        [row] = roadmap_card(page, "Done").find("tbody").find_all("tr")
        cell = row.find_all("td")[-1]
        self.assertEqual("Dropped: not worth the flash.", cell.text("en"))
        self.assertEqual("Porzucone: szkoda flasha.", cell.text("pl"))

    def test_done_version_of_a_verdict(self):
        en, pl = bs.split_cell("Done in v1.6.0: x. <!-- pl: Zrobione w v1.6.0: x. -->")
        self.assertEqual("v1.6.0", bs.done_version(en, pl))
        en, pl = bs.split_cell("Done in vNEXT: x. <!-- pl: Zrobione w vNEXT: x. -->")
        self.assertEqual(("the next release", "następna wersja"), bs.done_version(en, pl))
        self.assertIsNone(bs.done_version("Dropped: no.", "Porzucone."))


# ---------------------------------------------------------------- §2, §3 site/index.html

PANEL_TABS = [  # (EN label, PL label, screenshot)
    ("Location", "Lokalizacja", "web_location.png"),
    ("OpenSky", "OpenSky", "web_opensky.png"),
    ("General", "Ogólne", "web_general.png"),
    ("Backup", "Kopia", "web_backup.png"),
    ("Firmware", "Firmware", "web_firmware.png"),
    ("!", "!", "web_alert.png"),
]

# One distinctive fragment per language from each former "New:" / backup card (taken from
# site/index.html before spec 13), whitespace collapsed. They must survive the move into
# the tiles' "More".
FORMER_NEWS = [
    ("backup", "saves every setting to one JSON file and restores it later",
     "zapisuje wszystkie ustawienia do jednego pliku JSON i przywraca je później"),
    ("backup", "so treat it like a password", "więc traktuj go jak hasło"),
    ("alert", 'A typo in the OpenSky secret used to surface hours later as "no flights".',
     "Literówka w sekrecie OpenSky wychodziła kiedyś po kilku godzinach jako „brak lotów”."),
    ("alert", 'Fix the cause and the "!" goes away on its own. Nothing to acknowledge.',
     "Usuń przyczynę, a „!” zniknie samo. Niczego nie trzeba potwierdzać."),
    ("updates 1", "Once a day the board asks GitHub whether there's something newer.",
     "Raz na dobę płytka pyta GitHuba, czy jest coś nowszego."),
    ("updates 1", "A version that crashes on start still rolls itself back.",
     "Wersja, która wywraca się przy starcie, nadal sama się wycofuje."),
    ("updates 2", "all it tells GitHub is your IP and the firmware version.",
     "GitHub dowiaduje się z niego tylko Twojego IP i wersji firmware."),
    ("updates 2", "Prefer clicking?", "Wolisz klikać?"),
    ("night 1", "The CYD has a tiny light sensor next to the screen",
     "CYD ma obok ekranu malutki czujnik światła"),
    ("night 1", "It's off by default, because some people do watch planes at 2 a.m.",
     "Domyślnie wyłączone, bo niektórzy oglądają samoloty o drugiej w nocy."),
    ("night 2", "with a live readout of the sensor and today's sunrise and sunset.",
     "razem z podglądem czujnika na żywo i dzisiejszymi godzinami wschodu i zachodu."),
]


class SiteHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = INDEX.read_text(encoding="utf-8")
        cls.flat = re.sub(r"\s+", " ", cls.src)
        cls.root = parse(cls.src)

    def by_id(self, ident):
        node = self.root.find(id=ident)
        self.assertIsNotNone(node, 'no id="%s" in site/index.html' % ident)
        return node

    def menu_links(self):
        nav = self.root.find("nav", cls="links")
        self.assertIsNotNone(nav, "no top menu (nav.links)")
        return [a.attrs.get("href", "") for a in nav.find_all("a")]

    def tiles(self):
        grid = self.by_id("features").find(cls="features")
        self.assertIsNotNone(grid, "no .features grid in #features")
        return grid.element_children()

    def test_build_markers_kept(self):
        for marker in (bs.ROADMAP_MARKER, bs.VERSION_MARKER, bs.CHANGELOG_MARKER):
            self.assertIn(marker, self.src)

    def test_every_menu_anchor_exists(self):
        for href in self.menu_links():
            if href.startswith("#") and len(href) > 1:
                self.assertIsNotNone(self.root.find(id=href[1:]), href)

    # Scenario: Sekcja „Co jeszcze potrafi” ma 4 kafelki, a menu górne linkuje do niej i do panelu
    def test_features_section_and_menu_links(self):
        features = self.by_id("features")
        self.assertEqual("section", features.tag)
        pos = {i: self.src.find('id="%s"' % i) for i in ("screens", "features", "install")}
        self.assertLess(pos["screens"], pos["features"])
        self.assertLess(pos["features"], pos["install"])
        tiles = self.tiles()
        self.assertEqual(4, len(tiles))
        self.assertIn("night-mode", [t.attrs.get("id") for t in tiles])
        links = self.menu_links()
        self.assertIn("#features", links)
        self.assertIn("#panel", links)
        self.assertIsNone(self.root.find(id="dual-core"))
        self.assertNotIn("New:", self.src)
        self.assertNotIn("Nowość:", self.src)

    # Scenario: Przeglądarka panelu ma 6 zakładek, każda z istniejącym zrzutem i podpisem
    def test_panel_tabs_point_to_existing_screenshots(self):
        panel = self.by_id("panel")
        tablist = panel.find(role="tablist")
        self.assertIsNotNone(tablist, "#panel has no role=tablist")
        tabs = tablist.find_all(role="tab")

        def label(tab, lang):
            return tab.text(lang).strip("„”“\"' ")
        self.assertEqual([en for en, _, _ in PANEL_TABS], [label(t, "en") for t in tabs])
        self.assertEqual([pl for _, pl, _ in PANEL_TABS], [label(t, "pl") for t in tabs])
        for tab, (en, _, png) in zip(tabs, PANEL_TABS):
            with self.subTest(tab=en):
                target = tab.attrs.get("aria-controls")
                self.assertTrue(target, "tab without aria-controls")
                tabpanel = panel.find(id=target)
                self.assertIsNotNone(tabpanel, "no tabpanel #" + target)
                self.assertEqual("tabpanel", tabpanel.attrs.get("role"))
                [img] = tabpanel.find_all("img")
                src = "screenshots/" + png
                self.assertEqual(src, img.attrs.get("src"))
                self.assertTrue((REPO / "docs" / "screenshots" / png).is_file(), png)
                self.assertEqual("lazy", img.attrs.get("loading"))
                self.assertTrue(img.attrs.get("width"))
                self.assertTrue(img.attrs.get("height"))
                link = next((a for a in img.ancestors() if a.tag == "a"), None)
                self.assertIsNotNone(link, "screenshot not wrapped in a link")
                self.assertEqual(src, link.attrs.get("href"))
                self.assertEqual("_blank", link.attrs.get("target"))
                self.assertTrue(tabpanel.find(cls="en") and tabpanel.find(cls="en").text())
                self.assertTrue(tabpanel.find(cls="pl") and tabpanel.find(cls="pl").text())
        self.assertNotIn("web_firmware_dev.png", self.src)

    # Scenario: Żaden tekst z dawnych kart „Nowość” nie zginął
    def test_former_news_texts_kept(self):
        for card, en, pl in FORMER_NEWS:
            with self.subTest(card=card, lang="en"):
                self.assertIn(en, self.flat)
            with self.subTest(card=card, lang="pl"):
                self.assertIn(pl, self.flat)
        curious = next((s for s in self.root.find_all("section")
                        if s.find("h2") is not None
                        and "For the curious" in s.find("h2").text("en")), None)
        self.assertIsNotNone(curious, "no 'For the curious' section")
        for part in ("0,1 s", "5,7 s", "0.1 s", "5.7 s", "cyd-sky.local/perf.json"):
            with self.subTest(curious=part):
                self.assertIn(part, curious.text())

    # Scenario: Każdy nowy tekst w sekcjach #features i #panel ma wersję PL i EN
    def test_new_texts_come_in_en_pl_pairs(self):
        for ident in ("features", "panel"):
            with self.subTest(section=ident):
                node = self.by_id(ident)
                en, pl = len(node.find_all(cls="en")), len(node.find_all(cls="pl"))
                self.assertGreater(en, 0)
                self.assertEqual(en, pl)
        tiles = self.tiles()
        self.assertEqual(4, len(tiles))
        for i, tile in enumerate(tiles):
            with self.subTest(tile=i + 1):
                details = tile.find("details")
                self.assertIsNotNone(details, "tile without <details>")
                head = summary_of(details)
                self.assertEqual("More", head.text("en"))
                self.assertEqual("Więcej", head.text("pl"))


if __name__ == "__main__":
    unittest.main()
