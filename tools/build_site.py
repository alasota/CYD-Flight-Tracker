#!/usr/bin/env python3
"""Assemble the GitHub Pages site into one folder.

    python tools/build_site.py --out _site [--firmware DIR [--boot-app0 PATH]]

Copies site/ (the page itself) plus the README's images (docs/screenshots,
docs/icons), and fills the page's "What's next" section from ROADMAP.md (in
place of the <!-- ROADMAP --> marker in site/index.html) and the current
version + changelog from git (<!-- VERSION --> / <!-- CHANGELOG -->): one entry
per minor version (v1.6 = v1.6.0 … v1.6.4), led by ROADMAP.md's "Done in vX.Y.Z"
items, with the commit subjects since the previous release folded below — so it
needs a checkout with full history and tags. The pure part is tested by
tools/test_build_site.py. With --firmware it also adds firmware/ for the in-browser
installer (ESP Web Tools): the four flash parts and a manifest.json naming the
version.

DIR holds version.txt, bootloader.bin, partitions.bin and
cyd-sky-tracker-<version>.bin, plus boot_app0.bin (or pass --boot-app0) —
that is the layout of the firmware workflow's output / a GitHub Release, and
.pio/build/cyd after `pio run` (tools/pio_version.py writes version.txt and the
versioned .bin there; boot_app0.bin then comes from the Arduino framework).

The manifest lists the parts separately rather than the merged -full.bin on
purpose: the merged image also covers the NVS partition (0x9000), so it would
wipe WiFi / home location / OpenSky settings on every update. With separate
parts NVS is only erased when the user ticks "erase device" (first install).
"""

import argparse
import collections
import html
import json
import os
import re
import shutil
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "cyd-sky-tracker"
ROADMAP = os.path.join(REPO, "ROADMAP.md")
ROADMAP_MARKER = "<!-- ROADMAP -->"
VERSION_MARKER = "<!-- VERSION -->"
CHANGELOG_MARKER = "<!-- CHANGELOG -->"
GITHUB = "https://github.com/alasota/CYD-Flight-Tracker"
CHANGELOG_MAX_ITEMS = 15  # per version; longer lists end with a link to GitHub
# Section headings in ROADMAP.md -> Polish.
SECTION_PL = {"Planned": "Planowane", "In progress": "W trakcie", "Done": "Zrobione"}
# Roadmap table: column headers, and the rating words per column (Polish
# adjectives agree with the column's noun: wartość / wysiłek / ryzyko).
COLUMN_PL = {"Item": "Pozycja", "Value": "Wartość", "Effort": "Wysiłek", "Risk": "Ryzyko",
             "Rating": "Ocena", "Verdict": "Werdykt"}
WORDS_PL = {
    "Value": {"high": "wysoka", "medium": "średnia", "low": "niska"},
    "Effort": {"small": "mały", "medium": "średni", "large": "duży"},
    "Risk": {"low": "niskie", "medium": "średnie", "high": "wysokie"},
}
PL_COMMENT = re.compile(r"<!--\s*pl:\s*(.*?)\s*-->")


def split_cell(cell):
    """(english, polish) of a table cell: text + optional <!-- pl: ... --> comment.
    "vNEXT" (merged, not released yet — /release puts the number in) reads as words."""
    m = PL_COMMENT.search(cell)
    en = PL_COMMENT.sub("", cell).strip()
    pl = m.group(1).strip() if m else en
    return en.replace("vNEXT", "the next release"), pl.replace("vNEXT", "następnej wersji")


def translate_words(column, text):
    """'medium–large' -> 'średni–duży' for the rating columns; anything else as is."""
    words = WORDS_PL.get(column, {})
    return re.sub(r"[A-Za-z]+", lambda w: words.get(w.group(0).lower(), w.group(0)), text)


def parse_roadmap(text):
    """[(section, columns, [row, ...]), ...] from ROADMAP.md, rows in file order;
    each row is a list of (english, polish) cells."""
    # Drop the multi-line format notes (single-line pl: comments stay).
    text = re.sub(r"^<!--\s*\n.*?-->\s*$", "", text, flags=re.S | re.M)
    sections = []
    for line in text.splitlines():
        heading = re.match(r"^##\s+(.+?)\s*$", line)
        if heading:
            sections.append((heading.group(1), None, []))
            continue
        if not sections or not line.strip().startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-+:?", c) for c in cells):
            continue  # |---|---| separator
        name, columns, rows = sections[-1]
        if columns is None:
            sections[-1] = (name, cells, rows)
            continue
        row = []
        for col, cell in zip(columns, cells):
            en, pl = split_cell(cell)
            row.append((en, translate_words(col, pl)))
        rows.append(row)
    return [s for s in sections if s[1] and s[2]]


def roadmap_html(sections):
    def both(en, pl):
        if en == pl:
            return html.escape(en)
        return '<span class="en">%s</span><span class="pl">%s</span>' % (
            html.escape(en), html.escape(pl))

    blocks = []
    for name, columns, rows in sections:
        head = "".join('<th class="c-%s">%s</th>' % (
            re.sub(r"\W", "", c.lower()) or "n", both(c, COLUMN_PL.get(c, c))) for c in columns)
        body = "".join("<tr>%s</tr>" % "".join(
            '<td class="c-%s">%s</td>' % (re.sub(r"\W", "", c.lower()) or "n", both(en, pl))
            for c, (en, pl) in zip(columns, row)) for row in rows)
        blocks.append('<div class="card"><h3>%s</h3><div class="table-scroll">'
                      '<table class="roadmap-table"><thead><tr>%s</tr></thead>'
                      '<tbody>%s</tbody></table></div></div>' % (
                          both(name, SECTION_PL.get(name, name)), head, body))
    return '<div class="roadmap">%s</div>' % "".join(blocks)


def git(*args):
    try:
        return subprocess.check_output(["git"] + list(args), cwd=REPO,
                                       stderr=subprocess.DEVNULL).decode("utf-8").strip()
    except Exception:
        return ""


def releases():
    """[(tag, date, [commit subjects]), ...], newest first: one entry per v* release
    tag, with the non-merge commits since the previous one (all history for the first).
    Development versions (v1.3.0-rc.1 — any tag with a "-") get no entry of their
    own: their commits land in the next release's (release_ranges())."""
    out = []
    for tag, rng in release_ranges(git("tag", "--list", "v*").splitlines()):
        subjects = [s for s in git("log", "--no-merges", "--format=%s", rng).splitlines() if s]
        out.append((tag, git("log", "-1", "--format=%cs", tag), subjects))
    return out


def both(en, pl):
    return '<span class="en">%s</span><span class="pl">%s</span>' % (en, pl)


# ---- spec 13: changelog grouped by minor version (pure, tools/test_build_site.py)

# One changelog entry per minor version. minor "v1.6"; tags newest first; date = the
# newest release's (ISO, as git gives it); subjects = all its releases' commits, newest
# release first; prev_tag = the newest tag of the next-older group, or None.
Group = collections.namedtuple("Group", "minor tags date subjects prev_tag")


STABLE_TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
DONE_IN = re.compile(r"^Done in (v\d+\.\d+\.\d+)\b")
# Behind-the-scenes commits (tests, docs, specs, harness, release tooling): greyed in the
# changelog, not hidden. Case-sensitive: "Fix: …" is a user-visible fix, "fix" alone isn't.
CHORE = re.compile(r"^(Tests?\b|Docs?\b|Plan and BDD|Specs? ?\d|Spec/|/release|/fix|Release plan"
                   r"|Harness|Python bytecode|fix$)")
CHANGELOG_OPEN = 2  # newest groups shown open; older ones fold to one line


def parse_version(tag):
    """'v1.10.2' -> (1, 10, 2); anything else (dev tags, 'v1.6', '1.6.0') -> None."""
    m = STABLE_TAG.match(tag)
    return tuple(int(x) for x in m.groups()) if m else None


def release_ranges(tags):
    """All v* tags (any order) -> [(tag, git range)], newest first, stable tags only;
    the oldest gets the tag alone (all history). A development tag (v1.6.4-dev.11) gets
    no range of its own: its commits land in the next release's."""
    stable = sorted((t for t in tags if parse_version(t)), key=parse_version, reverse=True)
    return [(t, t if i == len(stable) - 1 else "%s..%s" % (stable[i + 1], t))
            for i, t in enumerate(stable)]


def group_by_minor(rels):
    """[(tag, date, subjects)] -> [Group], newest minor first (numeric order)."""
    rels = sorted((r for r in rels if parse_version(r[0])),
                  key=lambda r: parse_version(r[0]), reverse=True)
    groups = []  # [minor, tags, date, subjects]
    for tag, date, subjects in rels:
        minor = "v%d.%d" % parse_version(tag)[:2]
        if not groups or groups[-1][0] != minor:
            groups.append([minor, [], date, []])
        groups[-1][1].append(tag)
        groups[-1][3].extend(subjects)
    return [Group(minor, tags, date, subjects, groups[i + 1][1][0] if i + 1 < len(groups) else None)
            for i, (minor, tags, date, subjects) in enumerate(groups)]


def roadmap_done_by_version(sections):
    """parse_roadmap() output -> {"v1.4": [(version, (item_en, item_pl)), ...]}: rows of
    the Done section whose verdict starts with "Done in vX.Y.Z", sorted by version.
    "Done in vNEXT" (split_cell made it "Done in the next release") matches nothing."""
    out = {}
    for name, columns, rows in sections:
        if name != "Done" or "Item" not in columns or "Verdict" not in columns:
            continue
        item_i, verdict_i = columns.index("Item"), columns.index("Verdict")
        for row in rows:
            if len(row) <= max(item_i, verdict_i):
                continue
            m = DONE_IN.match(row[verdict_i][0])
            if m:
                version = m.group(1)
                out.setdefault("v%d.%d" % parse_version(version)[:2], []).append((version, row[item_i]))
    for items in out.values():
        items.sort(key=lambda it: parse_version(it[0]))
    return out


def is_chore(subject):
    """True for behind-the-scenes commits (tests, docs, specs, harness, release)."""
    return bool(CHORE.match(subject))


def format_date(iso):
    """'2026-10-25' -> '25.10.2026'; '' stays ''."""
    parts = iso.split("-")
    return ".".join(reversed(parts)) if len(parts) == 3 else iso


def done_version(verdict_en, verdict_pl):
    """'Done in v1.6.0: …' -> 'v1.6.0'; an unreleased (vNEXT) row -> ('the next release',
    'następna wersja'); no version -> None."""
    m = DONE_IN.match(verdict_en)
    if m:
        return m.group(1)
    if verdict_en.startswith("Done in the next release"):  # split_cell's vNEXT
        return ("the next release", "następna wersja")
    return None


def esc_both(en, pl):
    return both(html.escape(en), html.escape(pl))


def commit_list_html(group):
    """<ul> of a group's commits (chores greyed), max CHANGELOG_MAX_ITEMS + "…and N more"."""
    newest = group.tags[0]
    items = "".join('<li%s>%s</li>' % (' class="chore"' if is_chore(s) else "", html.escape(s))
                    for s in group.subjects[:CHANGELOG_MAX_ITEMS])
    more = len(group.subjects) - CHANGELOG_MAX_ITEMS
    if more > 0:
        url = ("%s/compare/%s...%s" % (GITHUB, group.prev_tag, newest) if group.prev_tag
               else "%s/commits/%s" % (GITHUB, newest))
        items += '<li class="more"><a href="%s">%s</a></li>' % (
            url, both("…and %d more" % more, "…i jeszcze %d" % more))
    return '<ul class="ticks">%s</ul>' % (items or "<li>—</li>")


def changelog_html(groups, done_by_minor=None):
    """HTML of the changelog: one <details class="card"> per minor version. The summary
    has the version, the release range, the newest release's date, the number of commits
    and the release / diff links; a folded group also shows its first roadmap item (or
    the count) in one line. Inside: the group's "Done in vX.Y.Z" roadmap items (linking
    to #roadmap), then its commits folded under "Commits (N)" — or, for a group with no
    roadmap items, the commits directly."""
    if not groups:
        return '<p class="muted center">%s</p>' % both("No releases yet.", "Jeszcze nic nie wydano.")
    done_by_minor = done_by_minor or {}
    blocks = []
    for i, g in enumerate(groups):
        newest, n = g.tags[0], len(g.subjects)
        count = both("%d change(s)" % n, "zmian: %d" % n)
        items = [item for _, item in done_by_minor.get(g.minor, [])]
        is_open = i < CHANGELOG_OPEN
        span = newest if len(g.tags) == 1 else "%s – %s" % (g.tags[-1], newest)
        meta = [html.escape(span)] + ([html.escape(format_date(g.date))] if g.date else [])
        preview = ""
        if is_open or items:
            meta.append(count)
        if not is_open:
            preview = '<span class="first-item">%s</span>' % (esc_both(*items[0]) if items else count)
        links = '<a href="%s/releases/tag/%s">%s</a>' % (GITHUB, newest, both("release", "wydanie"))
        if g.prev_tag:
            links += ' · <a href="%s/compare/%s...%s">%s</a>' % (
                GITHUB, g.prev_tag, newest, both("diff", "różnice"))
        summary = ('<summary><b>%s</b> <span class="muted">%s</span>%s'
                   '<span class="links-r">%s</span></summary>' % (
                       html.escape(g.minor), " · ".join(meta), preview, links))
        if items:
            body = '<ul class="ticks items">%s</ul>' % "".join(
                '<li><a href="#roadmap">%s</a></li>' % esc_both(en, pl) for en, pl in items)
            body += '<details class="commits"><summary>%s</summary>%s</details>' % (
                both("Commits (%d)" % n, "Commity (%d)" % n), commit_list_html(g))
        else:
            body = commit_list_html(g)
        blocks.append('<details class="card"%s>%s%s</details>' % (
            " open" if is_open else "", summary, body))
    return '<div class="changelog">%s</div>' % "".join(blocks)


def version_html(rels):
    if not rels:
        return ""
    tag, date, _ = rels[0]
    return '<a class="version-badge" href="#changelog">%s <b>%s</b> · %s</a>' % (
        both("Current version", "Aktualna wersja"), html.escape(tag), html.escape(date))


def fill_releases(out):
    index = os.path.join(out, "index.html")
    with open(index, encoding="utf-8") as f:
        page = f.read()
    for marker in (VERSION_MARKER, CHANGELOG_MARKER):
        if marker not in page:
            raise SystemExit("index.html has no %s marker" % marker)
    rels = releases()
    groups = group_by_minor(rels)
    with open(ROADMAP, encoding="utf-8") as f:
        done = roadmap_done_by_version(parse_roadmap(f.read()))
    page = page.replace(VERSION_MARKER, version_html(rels))
    page = page.replace(CHANGELOG_MARKER, changelog_html(groups, done))
    with open(index, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("changelog: %d release(s) in %d group(s)%s" % (
        len(rels), len(groups), ", current " + rels[0][0] if rels else ""))


def fill_roadmap(out):
    index = os.path.join(out, "index.html")
    with open(index, encoding="utf-8") as f:
        page = f.read()
    if ROADMAP_MARKER not in page:
        raise SystemExit("index.html has no %s marker" % ROADMAP_MARKER)
    with open(ROADMAP, encoding="utf-8") as f:
        sections = parse_roadmap(f.read())
    with open(index, "w", encoding="utf-8", newline="\n") as f:
        f.write(page.replace(ROADMAP_MARKER, roadmap_html(sections)))
    print("roadmap: %d item(s)" % sum(len(rows) for _, _, rows in sections))


def copy_tree(src, dst):
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True)


def add_firmware(out, src_dir, boot_app0):
    with open(os.path.join(src_dir, "version.txt")) as f:
        version = f.read().strip()
    fw = os.path.join(out, "firmware")
    os.makedirs(fw, exist_ok=True)

    app = "%s-%s.bin" % (NAME, version)
    boot_app0 = boot_app0 or os.path.join(src_dir, "boot_app0.bin")
    parts = [
        ("bootloader.bin", os.path.join(src_dir, "bootloader.bin"), 0x1000),
        ("partitions.bin", os.path.join(src_dir, "partitions.bin"), 0x8000),
        ("boot_app0.bin", boot_app0, 0xE000),
        (app, os.path.join(src_dir, app), 0x10000),
    ]
    for name, src, _ in parts:
        if not os.path.exists(src):
            raise SystemExit("missing firmware part: " + src)
    for name, src, _ in parts:
        shutil.copyfile(src, os.path.join(fw, name))

    manifest = {
        "name": "CYD Sky Tracker",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,  # the firmware doesn't speak Improv
        "builds": [{
            "chipFamily": "ESP32",
            "parts": [{"path": name, "offset": offset} for name, _, offset in parts],
        }],
    }
    with open(os.path.join(fw, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    return version


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--firmware", help="folder with the firmware files (see above)")
    ap.add_argument("--boot-app0", help="boot_app0.bin, if not in the --firmware folder")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    copy_tree(os.path.join(REPO, "site"), out)
    copy_tree(os.path.join(REPO, "docs", "screenshots"), os.path.join(out, "screenshots"))
    copy_tree(os.path.join(REPO, "docs", "icons"), os.path.join(out, "icons"))
    fill_roadmap(out)
    fill_releases(out)

    if args.firmware:
        print("firmware", add_firmware(out, args.firmware, args.boot_app0))
    else:
        print("no firmware: the page will point to Releases instead of the installer")
    print("site assembled in", out)


if __name__ == "__main__":
    main()
